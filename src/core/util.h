// util.h - small, dependency-free helpers shared across the app.
#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace util {

// ---------------------------------------------------------------- encoding --
std::string  toUtf8(const std::wstring& w);
std::wstring toUtf16(const std::string& s);
std::wstring toUtf16(const char* s);

// ------------------------------------------------------------------ string --
std::wstring trim(const std::wstring& s);
std::wstring lower(std::wstring s);
std::wstring upper(std::wstring s);
bool iequals(const std::wstring& a, const std::wstring& b);
bool startsWith(const std::wstring& s, const std::wstring& prefix);
bool endsWith(const std::wstring& s, const std::wstring& suffix);
bool contains(const std::wstring& haystack, const std::wstring& needle);
std::vector<std::wstring> split(const std::wstring& s, wchar_t delim);
std::wstring join(const std::vector<std::wstring>& parts, const std::wstring& sep);
std::wstring replaceAll(std::wstring s, const std::wstring& from, const std::wstring& to);
std::wstring format(const wchar_t* fmt, ...);
// Truncates to `maxPx` pixels using the given DC font, appending an ellipsis.
std::wstring ellipsize(HDC dc, const std::wstring& s, int maxPx);
// Same, but measures with an explicit font. GDI measures against whatever is
// currently selected in the DC, so call this (or select the font yourself)
// wherever the drawing font differs from what happens to be in the DC.
std::wstring ellipsize(HDC dc, const std::wstring& s, int maxPx, HFONT font);

// ------------------------------------------------------------------- files --
bool fileExists(const std::wstring& path);
bool dirExists(const std::wstring& path);
bool ensureDir(const std::wstring& path);
bool copyFileRaw(const std::wstring& from, const std::wstring& to);
bool deleteFileRaw(const std::wstring& path);
std::vector<std::wstring> listFiles(const std::wstring& dir, const std::wstring& extFilter);
std::vector<std::wstring> listDirs(const std::wstring& dir);
std::wstring readTextFile(const std::wstring& path);              // UTF-8 on disk
bool writeTextFile(const std::wstring& path, const std::wstring& text);
// Writes through a sibling temp file and renames over the target, so a crash
// mid-write can never leave a truncated config behind.
bool writeTextFileAtomic(const std::wstring& path, const std::wstring& text);
std::wstring fileName(const std::wstring& path);
std::wstring fileStem(const std::wstring& path);
std::wstring parentDir(const std::wstring& path);
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
std::wstring modulePath();
std::wstring exeDir();
std::wstring tempDir();

// ------------------------------------------------------------------- misc ---
std::wstring quoteArg(const std::wstring& arg);   // Windows command-line quoting
std::wstring humanBytes(uint64_t bytes);
std::wstring timestampForFile();                  // 20260922-205301
std::wstring timestampForDisplay();
int64_t      nowSeconds();
std::wstring formatDuration(int64_t seconds);

// ------------------------------------------------------------- process find --
// Returns the pid of `exeName` (case-insensitive) or 0 when not running.
DWORD findProcessByName(const std::wstring& exeName);
// Recursively searches `root` (max depth limited) for a file called `name`.
std::wstring findFileUnder(const std::wstring& root, const std::wstring& name, int maxDepth);

}  // namespace util