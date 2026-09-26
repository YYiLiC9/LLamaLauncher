// process.h - launches llama-server.exe and captures its console output.
#pragma once

#include <windows.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace proc {

// Owns one llama-server child process. Output is drained on background threads
// into an in-memory ring buffer so the UI can show a live log without ever
// blocking on read().
class ServerProcess {
public:
    ServerProcess() = default;
    ~ServerProcess();
    ServerProcess(const ServerProcess&) = delete;
    ServerProcess& operator=(const ServerProcess&) = delete;

    bool start(const std::wstring& exe, const std::wstring& args, const std::wstring& workDir,
               const std::wstring& logPath, std::wstring& error);
    void stop();

    bool isRunning() const;
    DWORD pid() const { return pid_; }
    int64_t startedAt() const { return startedAt_; }

    // Empties the pending output buffer. Called once per UI tick.
    std::vector<std::wstring> takeOutput();
    // Drops output captured so far without handing it to the caller.
    void clearOutput();

    // True once llama-server reports that it is accepting connections.
    bool ready() const { return ready_.load(); }
    // Exit code when the process has finished, otherwise -1.
    int exitCode() const { return exitCode_.load(); }
    bool hasExited() const { return exited_.load(); }

    const std::wstring& commandLine() const { return commandLine_; }

private:
    void appendLine(const std::string& utf8Line);

    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE job_ = nullptr;
    DWORD pid_ = 0;
    int64_t startedAt_ = 0;

    std::thread reader_;
    std::thread watchdog_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> exited_{false};
    std::atomic<int> exitCode_{-1};
    // Bumped on every start()/stop(). Reader and watchdog capture the value
    // they were born with and go silent once it no longer matches, which is
    // what makes handing a slow death to a detached reaper thread safe.
    std::atomic<uint64_t> generation_{0};

    std::mutex mutex_;
    std::deque<std::wstring> pending_;
    std::wstring commandLine_;
};

// Opens `url` in the user's default browser.
bool openInBrowser(const std::wstring& url);

}  // namespace proc