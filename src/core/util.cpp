#include "core/util.h"

#include <shlobj.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdarg>
#include <regex>

namespace util {

// ---------------------------------------------------------------- encoding --
std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring toUtf16(const std::string& s) {
    if (s.empty()) return {};
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), n);
    return out;
}

std::wstring toUtf16(const char* s) { return s ? toUtf16(std::string(s)) : std::wstring(); }

// ------------------------------------------------------------------ string --
static bool isSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; }

std::wstring trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)::towlower(c); });
    return s;
}

std::wstring upper(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)::towupper(c); });
    return s;
}

bool iequals(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (::towlower(a[i]) != ::towlower(b[i])) return false;
    return true;
}

bool startsWith(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool endsWith(const std::wstring& s, const std::wstring& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool contains(const std::wstring& haystack, const std::wstring& needle) {
    return haystack.find(needle) != std::wstring::npos;
}

std::vector<std::wstring> split(const std::wstring& s, wchar_t delim) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == delim) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& sep) {
    std::wstring out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::wstring replaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::wstring format(const wchar_t* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    std::vector<wchar_t> buf(256);
    for (;;) {
        va_list copy;
        va_copy(copy, args);
        int n = _vsnwprintf_s(buf.data(), buf.size(), _TRUNCATE, fmt, copy);
        va_end(copy);
        if (n >= 0) {
            va_end(args);
            return std::wstring(buf.data(), (size_t)n);
        }
        if (buf.size() > 65536) break;
        buf.resize(buf.size() * 2);
    }
    va_end(args);
    return {};
}

std::wstring ellipsize(HDC dc, const std::wstring& s, int maxPx, HFONT font) {
    HGDIOBJ oldFont = ::SelectObject(dc, font);
    std::wstring out = ellipsize(dc, s, maxPx);
    ::SelectObject(dc, oldFont);
    return out;
}

std::wstring ellipsize(HDC dc, const std::wstring& s, int maxPx) {
    SIZE sz{};
    if (!dc || maxPx <= 0) return s;
    ::GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    if (sz.cx <= maxPx) return s;
    const std::wstring dots = L"...";
    SIZE dotSz{};
    ::GetTextExtentPoint32W(dc, dots.c_str(), 3, &dotSz);
    int budget = maxPx - dotSz.cx;
    if (budget <= 0) return dots;
    size_t lo = 0, hi = s.size();
    while (lo < hi) {
        size_t mid = (lo + hi + 1) / 2;
        ::GetTextExtentPoint32W(dc, s.c_str(), (int)mid, &sz);
        if (sz.cx <= budget)
            lo = mid;
        else
            hi = mid - 1;
    }
    return s.substr(0, lo) + dots;
}

// ------------------------------------------------------------------- files --
bool fileExists(const std::wstring& path) {
    DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

// Size of the model on disk: the -m file itself, and for sharded GGUFs
// (xxx-00001-of-00003.gguf) every sibling shard. Returns 0 when the file
// does not exist.
uint64_t modelFileBytes(const std::wstring& modelPath) {
    auto sizeOf = [](const std::wstring& p) -> uint64_t {
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        if (!::GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) return 0;
        return ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    };
    uint64_t total = sizeOf(modelPath);
    if (!total) return 0;
    // Sharded? "...-00001-of-00003.gguf" -> sum shards 1..N.
    std::wstring low = util::lower(modelPath);
    static const std::wregex shard(L"-(\\d+)-of-(\\d+)\\.gguf$");
    std::wsmatch m;
    if (!std::regex_search(low, m, shard)) return total;
    int first = ::_wtoi(m[1].str().c_str());
    int count = ::_wtoi(m[2].str().c_str());
    if (count <= 1 || first != 1) return total;
    std::wstring stem = modelPath.substr(0, modelPath.size() - m.length(0));
    for (int i = 1; i <= count; ++i) {
        if (i == first) continue;
        wchar_t idx[16];
        ::swprintf(idx, 16, L"-%05d-of-%05d.gguf", i, count);
        total += sizeOf(stem + idx);
    }
    return total;
}

bool dirExists(const std::wstring& path) {
    DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensureDir(const std::wstring& path) {
    if (path.empty()) return false;
    if (dirExists(path)) return true;
    std::wstring parent = parentDir(path);
    if (!parent.empty() && !dirExists(parent)) ensureDir(parent);
    if (::CreateDirectoryW(path.c_str(), nullptr)) return true;
    return ::GetLastError() == ERROR_ALREADY_EXISTS;
}

bool copyFileRaw(const std::wstring& from, const std::wstring& to) {
    ensureDir(parentDir(to));
    return ::CopyFileW(from.c_str(), to.c_str(), FALSE) != 0;
}

bool deleteFileRaw(const std::wstring& path) {
    if (!fileExists(path)) return false;
    ::SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    return ::DeleteFileW(path.c_str()) != 0;
}

std::vector<std::wstring> listFiles(const std::wstring& dir, const std::wstring& extFilter) {
    std::vector<std::wstring> out;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        if (!extFilter.empty()) {
            if (extFilter[0] == L'*') {
                // The '*' is a wildcard, not part of the text. Comparing the
                // name against the pattern verbatim made every match fail, so
                // listFiles("*.yaml") returned nothing and the configuration
                // list stayed empty however many files were on disk.
                std::wstring suffix = lower(extFilter.substr(1));
                if (!endsWith(lower(name), suffix)) continue;
            } else if (!contains(lower(name), lower(extFilter))) {
                continue;
            }
        }
        out.push_back(dir + L"\\" + name);
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::wstring> listDirs(const std::wstring& dir) {
    std::vector<std::wstring> out;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        out.push_back(dir + L"\\" + fd.cFileName);
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

std::wstring readTextFile(const std::wstring& path) {
    // Share write access too: another process (or a sync client) holding the
    // file for writing should not make us read a truncated view silently.
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    ::GetFileSizeEx(f, &size);
    std::string data;
    data.resize((size_t)size.QuadPart);
    DWORD read = 0;
    // A failed read is not "an empty file": returning {} here would make the
    // configuration vanish from the list with no hint anything went wrong, so
    // the result is checked and a short read is reported by resizes below.
    BOOL ok = TRUE;
    if (!data.empty())
        ok = ::ReadFile(f, data.data(), (DWORD)data.size(), &read, nullptr) != 0;
    ::CloseHandle(f);
    if (!ok) return {};
    data.resize(read);

    // Strip a UTF-8 BOM when present, otherwise assume UTF-8.
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);

    // Detect UTF-16LE content so files saved by other tools still load.
    if (data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE) {
        std::wstring w((const wchar_t*)(data.data() + 2), (data.size() - 2) / 2);
        return w;
    }
    return toUtf16(data);
}

static bool writeUtf8File(const std::wstring& path, const std::wstring& text, DWORD extraFlags) {
    ensureDir(parentDir(path));
    HANDLE f = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL | extraFlags, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::string data = toUtf8(text);
    DWORD written = 0;
    bool ok = true;
    if (!data.empty()) ok = ::WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr) != 0;
    if (ok && (extraFlags & FILE_FLAG_WRITE_THROUGH)) {
        // Only the atomic path pays for the flush: without it the data can sit
        // in the cache while the rename below publishes an empty file.
        ok = ::FlushFileBuffers(f) != 0;
    }
    ::CloseHandle(f);
    return ok;
}

bool writeTextFile(const std::wstring& path, const std::wstring& text) {
    // Every text write in the app (settings, configurations, caches) protects
    // files that must survive a crash, so the safe path is the default.
    return writeTextFileAtomic(path, text);
}

bool writeTextFileAtomic(const std::wstring& path, const std::wstring& text) {
    // CREATE_ALWAYS on the target itself is the hazard: it truncates first and
    // writes second, so any crash in between leaves a zero-byte config behind.
    // Writing a sibling temp file and renaming over the target keeps the old
    // content on disk until the new content is complete and flushed.
    std::wstring tmp = path + L".tmp";
    if (!writeUtf8File(tmp, text, FILE_FLAG_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    if (!::MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

std::wstring fileName(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring fileStem(const std::wstring& path) {
    std::wstring n = fileName(path);
    size_t p = n.find_last_of(L'.');
    return p == std::wstring::npos ? n : n.substr(0, p);
}

std::wstring parentDir(const std::wstring& path) {
    size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return {};
    if (p == 2 && path[1] == L':') return path.substr(0, 3);   // "C:\"
    return path.substr(0, p);
}

std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') out += L'\\';
    std::wstring tail = b;
    while (!tail.empty() && (tail.front() == L'\\' || tail.front() == L'/')) tail.erase(0, 1);
    return out + tail;
}

std::wstring modulePath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, (DWORD)std::size(buf));
    return std::wstring(buf, n);
}

std::wstring exeDir() { return parentDir(modulePath()); }

std::wstring tempDir() {
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetTempPathW(MAX_PATH, buf);
    return std::wstring(buf, n);
}

// ------------------------------------------------------------------- misc ---
std::wstring quoteArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'"') {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'"');
            slashes = 0;
            continue;
        }
        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

std::wstring humanBytes(uint64_t bytes) {
    const wchar_t* units[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) {
        v /= 1024.0;
        ++u;
    }
    return format(u == 0 ? L"%.0f %s" : L"%.2f %s", v, units[u]);
}

std::wstring timestampForFile() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return format(L"%04d%02d%02d-%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
}

std::wstring timestampForDisplay() {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);
    return format(L"%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
}

int64_t nowSeconds() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (int64_t)(u.QuadPart / 10000000ULL) - 11644473600LL;
}

std::wstring formatDuration(int64_t seconds) {
    if (seconds < 0) seconds = 0;
    int64_t h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    if (h > 0) return format(L"%lld:%02lld:%02lld", h, m, s);
    return format(L"%02lld:%02lld", m, s);
}

// ------------------------------------------------------------- process find --
DWORD findProcessByName(const std::wstring& exeName) {
    DWORD pid = 0;
    HANDLE snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (::Process32FirstW(snap, &pe)) {
        do {
            if (iequals(pe.szExeFile, exeName)) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (::Process32NextW(snap, &pe));
    }
    ::CloseHandle(snap);
    return pid;
}

static bool findFileRec(const std::wstring& dir, const std::wstring& name, int depth,
                        std::wstring& out) {
    if (depth < 0) return false;
    WIN32_FIND_DATAW fd{};
    HANDLE h = ::FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::vector<std::wstring> subDirs;
    bool found = false;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            subDirs.push_back(dir + L"\\" + fd.cFileName);
        } else if (iequals(fd.cFileName, name)) {
            out = dir + L"\\" + fd.cFileName;
            found = true;
            break;
        }
    } while (::FindNextFileW(h, &fd));
    ::FindClose(h);
    if (found) return true;
    for (const auto& sub : subDirs) {
        if (findFileRec(sub, name, depth - 1, out)) return true;
    }
    return false;
}

std::wstring findFileUnder(const std::wstring& root, const std::wstring& name, int maxDepth) {
    std::wstring out;
    if (findFileRec(root, name, maxDepth, out)) return out;
    return {};
}

}  // namespace util