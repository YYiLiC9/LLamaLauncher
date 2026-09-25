// store.h - configuration model, persistence and llama.cpp discovery.
#pragma once

#include <string>
#include <vector>

#include "core/catalog.h"

namespace store {

// One launch parameter. `group` is a catalogue group key for built-in rows and
// free text for custom rows, which is what makes grouping extensible.
struct Param {
    std::wstring flag;
    std::wstring value;
    std::wstring group;
    std::wstring desc;
    bool custom = false;
    // Whether this parameter takes part in the launch command at all. A row the
    // user has not switched on is left out entirely, even if it happens to hold
    // a value - and its value box is not editable.
    bool enabled = true;
};

struct Config {
    std::wstring id;
    std::wstring name;
    std::wstring notes;
    std::vector<Param> params;
    int64_t created = 0;
    int64_t modified = 0;

    // Convenience lookups used by the detail view and the command builder.
    std::wstring value(const std::wstring& flag) const;
    int intValue(const std::wstring& flag, int fallback) const;
    void setValue(const std::wstring& flag, const std::wstring& value);
    std::wstring modelFile() const;
    int port() const;
};

struct Settings {
    std::wstring llamaDir;        // folder that contains llama-server.exe
    std::wstring llamaExe;        // resolved executable, cached from last detection
    bool backupEnabled = true;
    std::wstring backupDir;       // empty means <dataRoot>\backups
    std::wstring language;        // "zh" or "en"
    std::wstring theme;           // "system" (default), "light" or "dark"
};

// Result of an auto-detection sweep, so the settings dialog can show what was
// searched when nothing is found.
struct DetectResult {
    bool found = false;
    std::wstring exePath;
    std::vector<std::wstring> searched;
};

class Store {
public:
    // Loads settings and every configuration file. Safe to call once at start.
    bool load();

    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }

    // Mutable on purpose, but note: upsert()/remove()/importConfig() rebuild
    // the vector internally (loadConfigs), so EVERY reference or pointer handed
    // out by configs()/find() is invalidated across those calls. Copy what you
    // need before calling them.
    std::vector<Config>& configs() { return configs_; }
    const std::vector<Config>& configs() const { return configs_; }

    Config* find(const std::wstring& id);
    const Config* find(const std::wstring& id) const;

    // Writes `cfg` to disk (assigning an id when it has none) and reloads the
    // in-memory list so the sidebar order stays correct.
    // Takes the config by reference so the generated id, and the bookkeeping
    // timestamps, come back to the caller. Passing by value meant a brand new
    // configuration was written to disk but the caller still saw an empty id -
    // so the UI never selected it and it looked like the save had failed.
    bool upsert(Config& cfg, std::wstring* errorOut = nullptr);

    bool remove(const std::wstring& id);
    bool saveSettings();

    // ---- llama.cpp discovery ----
    // Returns the folder currently in use, or an empty string.
    std::wstring effectiveLlamaDir() const;
    // Full path to llama-server.exe, or empty when it cannot be resolved.
    std::wstring serverExe() const;
    DetectResult autoDetectLlama();

    // ---- backup ----
    std::wstring effectiveBackupDir() const;
    // Copies the given configuration file into the backup folder when backup is
    // enabled. Returns the written path, or empty when nothing was written.
    std::wstring backupConfig(const Config& cfg, const std::wstring& label);
    int backupAll();

    // ---- import / export ----
    bool exportConfig(const Config& cfg, const std::wstring& path, std::wstring& error) const;
    bool importConfig(const std::wstring& path, std::wstring& assignedId, std::wstring& error);

    std::wstring newId() const;

private:
    Settings settings_;
    std::vector<Config> configs_;

    bool loadConfigs();
    bool writeConfigFile(const Config& cfg) const;
    // Appends -1, -2, ... until the id is free. Call after newId() whenever a
    // fresh id must not displace an existing configuration file.
    void ensureUniqueId(std::wstring& id) const;
};

// ------------------------------------------------------------------ helpers --
// Assembles the full command line for llama-server (excluding the program
// itself). A leading space is not included.
std::wstring buildArgs(const Config& cfg);

// Same, but rendered for display: exe path plus arguments, ready to paste into
// a terminal.
std::wstring buildDisplayCommand(const Config& cfg, const std::wstring& exePath);

// Canonical, human readable flag label for a row.
std::wstring flagLabel(const std::wstring& flag);

// A fresh configuration seeded with one entry per built-in catalogue parameter
// (values left at their defaults). This is the single place the catalogue and
// the stored model meet, which keeps `catalog::ParamValue` out of the UI code.
std::vector<Param> defaultParams();

// Resolves the primary port for a configuration (its "--port" value or 8080).
int configPort(const Config& cfg);

}  // namespace store