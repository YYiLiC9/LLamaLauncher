// paths.h - every on-disk location the application uses.
#pragma once

#include <string>
#include <vector>

namespace paths {

// Root of all user data. Defaults to %APPDATA%\LlamaLauncher, but if a file
// called `portable.txt` sits next to the executable the app keeps everything in
// a `data` folder beside the exe instead, so the whole thing can live on a USB
// stick.
std::wstring dataRoot();

std::wstring settingsFile();                       // <root>\settings.yaml
std::wstring configsDir();                         // <root>\configs
std::wstring configFile(const std::wstring& id);   // <root>\configs\<id>.yaml
std::wstring backupsDir();                         // <root>\backups (default target)
std::wstring logDir();                             // <root>\logs
std::wstring autoFindCacheFile();                  // remembers where llama.cpp was found

// Picks a non-clashing file name inside `dir` for a new export.
std::wstring suggestExportFile(const std::wstring& dir, const std::wstring& stem);

}  // namespace paths