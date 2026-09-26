#include "core/process.h"

#include <shellapi.h>

#include <algorithm>

#include "core/paths.h"
#include "core/util.h"

namespace proc {

// ServerProcess ---------------------------------------------------------------
//
// The child is created with CREATE_NO_WINDOW so no console flashes up, while
// stdout/stderr are redirected into a pipe that a background thread drains.
ServerProcess::~ServerProcess() { stop(); }

bool ServerProcess::start(const std::wstring& exe, const std::wstring& args,
                          const std::wstring& workDir, const std::wstring& logPath,
                          std::wstring& error) {
    stop();

    if (!util::fileExists(exe)) {
        error = L"llama-server.exe not found: " + exe;
        return false;
    }

    // ---- pipes -------------------------------------------------------------
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        error = L"CreatePipe failed";
        return false;
    }
    ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    // ---- command line ------------------------------------------------------
    // CreateProcessW may modify the buffer, so it must be writable.
    std::wstring cmd = util::quoteArg(exe);
    if (!args.empty()) cmd += L" " + args;
    commandLine_ = cmd;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    std::vector<wchar_t> cwdBuf;
    if (!workDir.empty()) {
        cwdBuf.assign(workDir.begin(), workDir.end());
        cwdBuf.push_back(L'\0');
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    // CREATE_NO_WINDOW keeps the console from flashing up; the pipe still
    // receives everything the server prints. CREATE_DEFAULT_ERROR_MODE +
    // SEM_FAILCRITICALERRORS: when the configured llama-server.exe is broken
    // (a DLL next to it is missing, say), the loader's "找不到 xxx.dll"
    // hard-error box must not pop up on top of the launcher - the process
    // just fails to start, and the exit code surfaces that in the UI instead.
    UINT oldErrorMode = ::SetErrorMode(SEM_FAILCRITICALERRORS);
    BOOL ok = ::CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                               CREATE_NO_WINDOW | CREATE_DEFAULT_ERROR_MODE, nullptr,
                               cwdBuf.empty() ? nullptr : cwdBuf.data(), &si, &pi);
    ::SetErrorMode(oldErrorMode);
    ::CloseHandle(writePipe);

    if (!ok) {
        DWORD err = ::GetLastError();
        ::CloseHandle(readPipe);
        error = util::format(L"CreateProcess failed (error %lu)", err);
        return false;
    }

    process_ = pi.hProcess;
    thread_ = pi.hThread;
    pid_ = pi.dwProcessId;
    startedAt_ = util::nowSeconds();
    exited_ = false;
    exitCode_ = -1;
    ready_ = false;
    // Invalidate any threads still running from a previous run on this object
    // (a detached reaper may hold a slow death for seconds).
    generation_.fetch_add(1);

    // A job object guarantees the whole process tree dies with us, even if the
    // user force-closes the launcher.
    job_ = ::CreateJobObjectW(nullptr, nullptr);
    if (job_) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info));
        ::AssignProcessToJobObject(job_, process_);
    }

    // ---- reader thread -----------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.clear();
        if (!logPath.empty())
            pending_.push_back(util::format(L"[%s] %s",
                                            util::timestampForDisplay().c_str(),
                                            cmd.c_str()));
    }

    reader_ = std::thread([this, gen = generation_.load(), readPipe, logPath] {
        std::string buffer;
        char chunk[4096];
        FILE* log = nullptr;
        if (!logPath.empty()) {
            util::ensureDir(util::parentDir(logPath));
            // The log is a diagnostic aid, so a failure to open it is not fatal.
            log = _wfsopen(logPath.c_str(), L"ab", _SH_DENYWR);
        }
        for (;;) {
            DWORD got = 0;
            BOOL readOk = ::ReadFile(readPipe, chunk, sizeof(chunk), &got, nullptr);
            if (!readOk || got == 0) break;
            if (log) ::fwrite(chunk, 1, got, log);
            buffer.append(chunk, got);
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                // A retired reader (stop() moved on without us) must not feed
                // a successor run's output buffer.
                if (gen == generation_.load()) appendLine(line);
            }
        }
        if (!buffer.empty() && gen == generation_.load()) appendLine(buffer);
        if (log) ::fclose(log);
        ::CloseHandle(readPipe);
    });

    // Watchdog: notices the process exiting and records its code. It only ever
    // touches the shared exit flags, never the handle - stop() owns that.
    // Retired watchdogs go silent: a slow GPU teardown may outlive stop() by
    // seconds, and must not flag a successor run as exited.
    HANDLE watchTarget = process_;
    watchdog_ = std::thread([this, gen = generation_.load(), watchTarget] {
        ::WaitForSingleObject(watchTarget, INFINITE);
        if (gen != generation_.load()) return;
        DWORD code = 0;
        ::GetExitCodeProcess(watchTarget, &code);
        exitCode_ = (int)code;
        exited_ = true;
        ready_ = false;
    });

    return true;
}

void ServerProcess::appendLine(const std::string& utf8Line) {
    std::wstring line = util::toUtf16(utf8Line);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back(line);
        // Keep memory bounded on very chatty runs.
        while (pending_.size() > 4000) pending_.pop_front();
    }
    std::wstring low = util::lower(line);
    if (util::contains(low, L"listening on") || util::contains(low, L"all slots are idle") ||
        util::contains(low, L"server is listening")) {
        ready_ = true;
    }
}

std::vector<std::wstring> ServerProcess::takeOutput() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::wstring> out(pending_.begin(), pending_.end());
    pending_.clear();
    return out;
}

void ServerProcess::clearOutput() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

bool ServerProcess::isRunning() const {
    if (!process_ || exited_.load()) return false;
    DWORD code = 0;
    if (!::GetExitCodeProcess(process_, &code)) return false;
    return code == STILL_ACTIVE;
}

void ServerProcess::stop() {
    // Terminate first: the watchdog may be blocked on the process handle, so
    // the handles can only be closed once it has been joined.
    if (process_ && isRunning()) {
        if (job_) ::TerminateJobObject(job_, 0);
        ::TerminateProcess(process_, 0);
    }

    HANDLE proc = process_;
    HANDLE thr = thread_;
    HANDLE job = job_;
    pid_ = 0;
    ready_ = false;
    exited_ = true;
    // Retire this run's reader/watchdog: whatever they still observe must not
    // leak into a successor run's state.
    generation_.fetch_add(1);

    if (!proc) {
        // Nothing was running; just unwind any leftover threads.
        if (reader_.joinable()) reader_.join();
        if (watchdog_.joinable()) watchdog_.join();
    } else if (::WaitForSingleObject(proc, 500) == WAIT_OBJECT_0) {
        // Quick death: unwind inline as before.
        if (reader_.joinable()) reader_.join();
        if (watchdog_.joinable()) watchdog_.join();
        if (thr) ::CloseHandle(thr);
        if (job) ::CloseHandle(job);
        ::CloseHandle(proc);
    } else {
        // Still tearing down. Unmapping a large VRAM allocation can hold a GPU
        // process for seconds, and waiting for that here froze the UI on
        // every stop. Hand everything to a detached reaper: both threads have
        // been retired via the generation counter, so touching shared state
        // is no longer possible, and the reaper only joins and closes.
        std::thread reaper([proc, thr, job](std::thread rd, std::thread wd) {
            if (proc) ::WaitForSingleObject(proc, 15000);
            if (rd.joinable()) rd.join();
            if (wd.joinable()) wd.join();
            if (thr) ::CloseHandle(thr);
            if (job) ::CloseHandle(job);
        }, std::move(reader_), std::move(watchdog_));
        reaper.detach();
    }

    process_ = nullptr;
    thread_ = nullptr;
    job_ = nullptr;
}

bool openInBrowser(const std::wstring& url) {
    HINSTANCE r = ::ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

}  // namespace proc