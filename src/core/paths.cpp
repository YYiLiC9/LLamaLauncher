#include "core/paths.h"

#include <shlobj.h>

#include "core/util.h"

namespace paths {

std::wstring dataRoot() {
    static std::wstring cached;
    if (!cached.empty()) return cached;

    // Portable mode: a marker file next to the executable wins.
    if (util::fileExists(util::joinPath(util::exeDir(), L"portable.txt"))) {
        cached = util::joinPath(util::exeDir(), L"data");
        util::ensureDir(cached);
        return cached;
    }

    wchar_t* roaming = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming)) && roaming) {
        cached = util::joinPath(roaming, L"LlamaLauncher");
        ::CoTaskMemFree(roaming);
    } else {
        cached = util::joinPath(util::exeDir(), L"data");
    }
    util::ensureDir(cached);
    return cached;
}

std::wstring settingsFile() { return util::joinPath(dataRoot(), L"settings.yaml"); }
std::wstring configsDir() { return util::joinPath(dataRoot(), L"configs"); }
std::wstring configFile(const std::wstring& id) {
    return util::joinPath(configsDir(), id + L".yaml");
}
std::wstring backupsDir() { return util::joinPath(dataRoot(), L"backups"); }
std::wstring logDir() { return util::joinPath(dataRoot(), L"logs"); }
std::wstring autoFindCacheFile() { return util::joinPath(dataRoot(), L"llama-path.txt"); }

std::wstring suggestExportFile(const std::wstring& dir, const std::wstring& stem) {
    std::wstring base = util::joinPath(dir, stem + L".yaml");
    if (!util::fileExists(base)) return base;
    for (int i = 2; i < 999; ++i) {
        std::wstring candidate = util::joinPath(dir, util::format(L"%s (%d).yaml", stem.c_str(), i));
        if (!util::fileExists(candidate)) return candidate;
    }
    return base;
}

}  // namespace paths