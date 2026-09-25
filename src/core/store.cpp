#include "core/store.h"

#include <shlobj.h>

#include <algorithm>

#include "core/i18n.h"
#include "core/paths.h"
#include "core/util.h"
#include "core/yaml.h"

namespace store {

// --------------------------------------------------------------- Config ----
std::wstring Config::value(const std::wstring& flag) const {
    std::wstring want = util::lower(util::trim(flag));
    for (const Param& p : params)
        if (util::lower(p.flag) == want) return p.value;
    return {};
}

int Config::intValue(const std::wstring& flag, int fallback) const {
    std::wstring v = util::trim(value(flag));
    if (v.empty()) return fallback;
    wchar_t* end = nullptr;
    long r = ::wcstol(v.c_str(), &end, 10);
    if (end == v.c_str()) return fallback;
    return (int)r;
}

void Config::setValue(const std::wstring& flag, const std::wstring& v) {
    std::wstring want = util::lower(util::trim(flag));
    for (Param& p : params) {
        if (util::lower(p.flag) == want) {
            p.value = v;
            return;
        }
    }
    Param p;
    p.flag = flag;
    p.value = v;
    p.group = L"custom";
    p.custom = true;
    params.push_back(std::move(p));
}

std::wstring Config::modelFile() const { return util::trim(value(L"-m")); }
int Config::port() const { return intValue(L"--port", 8080); }

// ------------------------------------------------------------------ Store ---
std::wstring Store::newId() const {
    return util::timestampForFile();
}

void Store::ensureUniqueId(std::wstring& id) const {
    for (int guard = 0; guard < 50 && find(id); ++guard)
        id = newId() + util::format(L"-%d", guard + 1);
}

bool Store::load() {
    util::ensureDir(paths::configsDir());
    util::ensureDir(paths::backupsDir());
    util::ensureDir(paths::logDir());

    // ---- settings ----
    settings_ = Settings{};
    std::wstring settingsText = util::readTextFile(paths::settingsFile());
    if (!settingsText.empty()) {
        yaml::Node root;
        std::wstring err;
        if (yaml::parse(settingsText, root, err)) {
            settings_.llamaDir = root.str(L"llama_dir");
            settings_.llamaExe = root.str(L"llama_exe");
            settings_.backupEnabled = root.boolean(L"backup_enabled", true);
            settings_.backupDir = root.str(L"backup_dir");
            settings_.language = root.str(L"language", L"zh");
            settings_.theme = root.str(L"theme", L"system");
        }
    }
    if (settings_.language.empty()) settings_.language = L"zh";
    if (settings_.backupDir.empty()) settings_.backupDir = paths::backupsDir();

    // Remember where llama.cpp was found last time, so a fresh settings file
    // does not force another search.
    if (settings_.llamaDir.empty()) {
        std::wstring cached = util::trim(util::readTextFile(paths::autoFindCacheFile()));
        if (!cached.empty() && util::fileExists(util::joinPath(cached, L"llama-server.exe")))
            settings_.llamaDir = cached;
    }

    return loadConfigs();
}

bool Store::loadConfigs() {
    configs_.clear();
    for (const std::wstring& path : util::listFiles(paths::configsDir(), L"*.yaml")) {
        std::wstring text = util::readTextFile(path);
        if (text.empty()) continue;
        yaml::Node root;
        std::wstring err;
        if (!yaml::parse(text, root, err)) continue;

        Config cfg;
        cfg.id = util::fileStem(path);
        // A file literally named ".yaml" yields an empty stem; keeping it would
        // build paths like "configs\.yaml" and corrupt later saves.
        if (cfg.id.empty()) continue;
        cfg.name = root.str(L"name", cfg.id);
        cfg.notes = root.str(L"notes");
        cfg.created = root.i64(L"created", 0);
        cfg.modified = root.i64(L"modified", 0);

        const yaml::Node& params = root[L"params"];
        if (params.isSeq()) {
            for (const yaml::Node& item : params.items()) {
                Param p;
                p.flag = item.str(L"flag");
                p.value = item.str(L"value");
                p.group = item.str(L"group", L"custom");
                p.desc = item.str(L"desc");
                p.custom = item.boolean(L"custom", false);
                p.enabled = item.boolean(L"enabled", true);
                if (p.flag.empty()) continue;
                cfg.params.push_back(std::move(p));
            }
        }

        // Merge in any built-in parameter the file does not mention so the
        // editor always shows the complete set. The merged row must start
        // switched off when it has no value - defaulting to on made flags like
        // --mlock display as enabled while never reaching the command line.
        for (const auto& pv : catalog::defaultParams()) {
            bool present = false;
            for (const Param& p : cfg.params)
                if (util::iequals(p.flag, pv.flag)) {
                    present = true;
                    break;
                }
            if (present) continue;
            const catalog::Spec* spec = catalog::find(pv.flag);
            Param p;
            p.flag = pv.flag;
            p.value = spec ? (spec->defaultVal ? spec->defaultVal : L"") : L"";
            p.group = pv.group;
            p.custom = false;
            // Same rule as defaultParams(): -m ships checked.
            p.enabled = util::iequals(pv.flag, L"-m") ? true : !util::trim(p.value).empty();
            cfg.params.push_back(std::move(p));
        }

        configs_.push_back(std::move(cfg));
    }

    std::sort(configs_.begin(), configs_.end(), [](const Config& a, const Config& b) {
        return a.id < b.id;
    });
    return true;
}

Config* Store::find(const std::wstring& id) {
    for (Config& c : configs_)
        if (c.id == id) return &c;
    return nullptr;
}

const Config* Store::find(const std::wstring& id) const {
    for (const Config& c : configs_)
        if (c.id == id) return &c;
    return nullptr;
}

bool Store::writeConfigFile(const Config& cfg) const {
    yaml::Node root = yaml::Node::map();
    root.set(L"version", L"1");
    root.set(L"id", cfg.id);
    root.set(L"name", cfg.name);
    root.set(L"created", util::format(L"%lld", (long long)cfg.created));
    root.set(L"modified", util::format(L"%lld", (long long)cfg.modified));
    root.set(L"notes", cfg.notes);

    yaml::Node params = yaml::Node::seq();
    for (const Param& p : cfg.params) {
        // Skip rows the user left untouched so exported files stay tidy: a
        // built-in row with an empty value means "not passed" and is still
        // written, because the editor relies on it being present.
        yaml::Node item = yaml::Node::map();
        item.set(L"flag", p.flag);
        item.set(L"value", p.value);
        item.set(L"group", p.group);
        if (!p.desc.empty()) item.set(L"desc", p.desc);
        item.set(L"custom", p.custom ? L"true" : L"false");
        item.set(L"enabled", p.enabled ? L"true" : L"false");
        params.push(item);
    }
    root.set(L"params", params);

    std::wstring text = util::format(L"# LlamaLauncher configuration\n# %s\n", cfg.name.c_str());
    text += yaml::dump(root);
    return util::writeTextFile(paths::configFile(cfg.id), text);
}

bool Store::upsert(Config& cfg, std::wstring* errorOut) {
    if (cfg.id.empty()) {
        // Timestamps have second resolution, so an unguarded newId() can hand
        // out an id that already exists - and writeConfigFile would then
        // replace that configuration instead of creating a new one. Importing
        // has always guarded against this; the editor path now does too.
        cfg.id = newId();
        ensureUniqueId(cfg.id);
    }
    int64_t now = util::nowSeconds();
    if (cfg.created == 0) cfg.created = now;
    cfg.modified = now;
    if (cfg.name.empty()) cfg.name = T(Str::UnnamedConfig);

    if (!writeConfigFile(cfg)) {
        if (errorOut) *errorOut = T(Str::SaveFailed);
        return false;
    }
    backupConfig(cfg, L"save");
    loadConfigs();
    return true;
}

bool Store::remove(const std::wstring& id) {
    const Config* cfg = find(id);
    if (cfg) backupConfig(*cfg, L"delete");
    return util::deleteFileRaw(paths::configFile(id)) && loadConfigs();
}

bool Store::saveSettings() {
    yaml::Node root = yaml::Node::map();
    root.set(L"version", L"1");
    root.set(L"language", settings_.language);
    root.set(L"theme", settings_.theme.empty() ? std::wstring(L"system") : settings_.theme);
    root.set(L"llama_dir", settings_.llamaDir);
    root.set(L"llama_exe", settings_.llamaExe);
    root.set(L"backup_enabled", settings_.backupEnabled ? L"true" : L"false");
    root.set(L"backup_dir", settings_.backupDir);
    if (!util::writeTextFile(paths::settingsFile(), yaml::dump(root))) return false;

    if (!settings_.llamaDir.empty()) {
        util::writeTextFile(paths::autoFindCacheFile(), settings_.llamaDir);
    }
    return true;
}

// ------------------------------------------------------- llama.cpp lookup ---
std::wstring Store::effectiveLlamaDir() const {
    if (!settings_.llamaDir.empty() && util::fileExists(util::joinPath(settings_.llamaDir,
                                                                       L"llama-server.exe")))
        return settings_.llamaDir;
    return {};
}

std::wstring Store::serverExe() const {
    std::wstring dir = effectiveLlamaDir();
    if (dir.empty()) return {};
    return util::joinPath(dir, L"llama-server.exe");
}

DetectResult Store::autoDetectLlama() {
    DetectResult r;

    std::vector<std::wstring> roots;
    auto addRoot = [&](const std::wstring& p) {
        if (p.empty()) return;
        for (const auto& existing : roots)
            if (util::iequals(existing, p)) return;
        roots.push_back(p);
    };

    // 1. Known llama.cpp vendor folders under %LOCALAPPDATA%\Programs.
    wchar_t* local = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local) {
        addRoot(util::joinPath(local, L"Programs"));
        // 2. winget keeps every package in a generated folder whose name starts
        //    with the package id - this is where `winget install ggml.llamacpp`
        //    puts the binaries on this machine.
        std::wstring winget = util::joinPath(local, L"Microsoft\\WinGet\\Packages");
        for (const auto& pkg : util::listDirs(winget)) {
            std::wstring name = util::lower(util::fileName(pkg));
            if (util::contains(name, L"llamacpp") || util::contains(name, L"llama.cpp"))
                addRoot(pkg);
        }
        ::CoTaskMemFree(local);
    }

    // 3. Common manual install locations.
    wchar_t* pf = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &pf)) && pf) {
        addRoot(util::joinPath(pf, L"llama.cpp"));
        ::CoTaskMemFree(pf);
    }
    wchar_t* home = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &home)) && home) {
        addRoot(util::joinPath(home, L"llama.cpp"));
        addRoot(util::joinPath(home, L"scoop\\apps\\llama.cpp\\current"));
        addRoot(util::joinPath(home, L"AppData\\Local\\llama.cpp"));
        ::CoTaskMemFree(home);
    }
    addRoot(L"C:\\llama.cpp");
    addRoot(util::exeDir());

    for (const std::wstring& root : roots) {
        r.searched.push_back(root);

        // Direct hit first.
        if (util::fileExists(util::joinPath(root, L"llama-server.exe"))) {
            r.found = true;
            r.exePath = util::joinPath(root, L"llama-server.exe");
            break;
        }
        // Otherwise walk a couple of levels: release archives unpack into a
        // versioned subfolder.
        std::wstring hit = util::findFileUnder(root, L"llama-server.exe", 2);
        if (!hit.empty()) {
            r.found = true;
            r.exePath = hit;
            break;
        }
    }

    if (r.found) {
        settings_.llamaDir = util::parentDir(r.exePath);
        settings_.llamaExe = r.exePath;
        saveSettings();
    }
    return r;
}

// ---------------------------------------------------------------- backup ----
std::wstring Store::effectiveBackupDir() const {
    if (!settings_.backupDir.empty()) return settings_.backupDir;
    return paths::backupsDir();
}

std::wstring Store::backupConfig(const Config& cfg, const std::wstring& label) {
    if (!settings_.backupEnabled) return {};
    std::wstring dir = effectiveBackupDir();
    util::ensureDir(dir);
    std::wstring name = util::format(L"%s_%s_%s.yaml", cfg.id.c_str(), label.c_str(),
                                     util::timestampForFile().c_str());
    std::wstring dest = util::joinPath(dir, name);
    if (!util::copyFileRaw(paths::configFile(cfg.id), dest)) return {};
    return dest;
}

int Store::backupAll() {
    int n = 0;
    std::wstring dir = effectiveBackupDir();
    util::ensureDir(dir);
    std::wstring stamp = util::timestampForFile();
    for (const Config& cfg : configs_) {
        std::wstring dest =
            util::joinPath(dir, util::format(L"%s_manual_%s.yaml", cfg.id.c_str(), stamp.c_str()));
        if (util::copyFileRaw(paths::configFile(cfg.id), dest)) ++n;
    }
    return n;
}

// ---------------------------------------------------------- import/export ---
bool Store::exportConfig(const Config& cfg, const std::wstring& path, std::wstring& error) const {
    yaml::Node root = yaml::Node::map();
    root.set(L"version", L"1");
    root.set(L"id", cfg.id);
    root.set(L"name", cfg.name);
    root.set(L"created", util::format(L"%lld", (long long)cfg.created));
    root.set(L"modified", util::format(L"%lld", (long long)cfg.modified));
    root.set(L"notes", cfg.notes);

    yaml::Node params = yaml::Node::seq();
    for (const Param& p : cfg.params) {
        yaml::Node item = yaml::Node::map();
        item.set(L"flag", p.flag);
        item.set(L"value", p.value);
        item.set(L"group", p.group);
        if (!p.desc.empty()) item.set(L"desc", p.desc);
        item.set(L"custom", p.custom ? L"true" : L"false");
        item.set(L"enabled", p.enabled ? L"true" : L"false");
        params.push(item);
    }
    root.set(L"params", params);

    std::wstring text = util::format(L"# LlamaLauncher configuration export\n# %s\n",
                                     cfg.name.c_str());
    text += yaml::dump(root);
    if (!util::writeTextFile(path, text)) {
        error = T(Str::ExportError);
        return false;
    }
    return true;
}

bool Store::importConfig(const std::wstring& path, std::wstring& assignedId,
                         std::wstring& error) {
    std::wstring text = util::readTextFile(path);
    if (text.empty()) {
        error = T(Str::LoadFailed);
        return false;
    }
    yaml::Node root;
    if (!yaml::parse(text, root, error)) {
        if (error.empty()) error = T(Str::ImportError);
        return false;
    }

    Config cfg;
    cfg.id = root.str(L"id");
    cfg.name = root.str(L"name");
    cfg.notes = root.str(L"notes");
    cfg.created = root.i64(L"created", 0);
    cfg.modified = root.i64(L"modified", 0);

    const yaml::Node& params = root[L"params"];
    if (!params.isSeq()) {
        error = T(Str::ImportError);
        return false;
    }
    for (const yaml::Node& item : params.items()) {
        Param p;
        p.flag = item.str(L"flag");
        p.value = item.str(L"value");
        p.group = item.str(L"group", L"custom");
        p.desc = item.str(L"desc");
        p.custom = item.boolean(L"custom", false);
        p.enabled = item.boolean(L"enabled", true);
        if (p.flag.empty()) continue;
        cfg.params.push_back(std::move(p));
    }

    if (cfg.name.empty()) cfg.name = util::fileStem(path);

    // A file without an id gets a fresh one, and an id that already exists on
    // disk is replaced as well: importing never overwrites an existing
    // configuration, it always lands as a new one. (The old comment claimed
    // same-id imports overwrite; the uniqueness loop below has always renamed
    // them, so the code and the comment disagreed - the loop wins.)
    if (cfg.id.empty()) cfg.id = newId();
    ensureUniqueId(cfg.id);
    if (find(cfg.id)) {
        // Fifty attempts without a free id: report failure rather than write
        // over a configuration the user did not mean to replace.
        error = T(Str::ImportError);
        return false;
    }

    // Top up missing built-ins so the editor stays complete.
    for (const auto& pv : catalog::defaultParams()) {
        bool present = false;
        for (const Param& p : cfg.params)
            if (util::iequals(p.flag, pv.flag)) {
                present = true;
                break;
            }
        if (!present) {
            Param p;
            p.flag = pv.flag;
            p.group = pv.group;
            cfg.params.push_back(std::move(p));
        }
    }

    std::wstring upsertError;
    if (!upsert(cfg, &upsertError)) {
        error = upsertError.empty() ? T(Str::ImportError) : upsertError;
        return false;
    }
    assignedId = cfg.id;
    return true;
}

// ----------------------------------------------------------------- helpers --
std::wstring buildArgs(const Config& cfg) {
    std::vector<std::wstring> parts;
    for (const Param& p : cfg.params) {
        if (p.flag.empty() || !p.enabled) continue;
        const catalog::Spec* spec = catalog::find(p.flag);
        bool toggle = spec && spec->isToggle;
        std::wstring v = util::trim(p.value);

        if (toggle) {
            // The row switch is the single source of truth for flag-style
            // parameters - their value carries no information. Deriving the
            // state from the value instead left switches that were on in the
            // editor but silently dropped from the command line.
            if (p.enabled) parts.push_back(p.flag);
            continue;
        }
        if (v.empty()) continue;
        parts.push_back(p.flag);
        parts.push_back(util::quoteArg(v));
    }
    return util::join(parts, L" ");
}

std::wstring buildDisplayCommand(const Config& cfg, const std::wstring& exePath) {
    std::wstring head = util::quoteArg(exePath.empty() ? L"llama-server.exe" : exePath);
    std::wstring args = buildArgs(cfg);
    return args.empty() ? head : head + L" " + args;
}

std::wstring flagLabel(const std::wstring& flag) {
    const catalog::Spec* spec = catalog::find(flag);
    if (!spec) return flag;
    std::wstring out = spec->flag;
    if (spec->aliases && *spec->aliases) out += util::format(L"  (%s)", spec->aliases);
    return out;
}

std::vector<Param> defaultParams() {
    std::vector<Param> out;
    const std::vector<catalog::ParamValue> seed = catalog::defaultParams();
    out.reserve(seed.size());
    for (const catalog::ParamValue& pv : seed) {
        Param p;
        p.flag = pv.flag;
        p.value = pv.value;
        p.group = pv.group;
        p.desc = pv.desc;
        p.custom = pv.custom;
        // A row that arrives with a value is on; an empty one starts off,
        // which is what keeps untouched parameters out of the command. -m is
        // the exception: a launch needs a model, and with the row off its
        // browse button is disabled too, so a fresh config read as broken.
        p.enabled = util::iequals(pv.flag, L"-m") ? true : !util::trim(p.value).empty();
        out.push_back(std::move(p));
    }
    return out;
}

int configPort(const Config& cfg) {
    int p = cfg.port();
    if (p <= 0 || p > 65535) return 8080;
    return p;
}

}  // namespace store