// i18n.h - minimal two-language string table (English / Simplified Chinese).
//
// A flat enum keeps lookups branch-free: `T(Str::Save)` is just an array index,
// so language switching costs nothing at runtime.
#pragma once

#include <string>

namespace i18n {

enum class Lang { Zh, En };

enum class Str {
    // ---- app chrome ----
    AppTitle,
    AppTagline,
    // ---- top bar ----
    Settings,
    ImportConfig,
    ExportConfig,
    Help,
    WebChat,
    // ---- sidebar ----
    Configurations,
    NewConfiguration,
    SearchPlaceholder,
    NoConfigs,
    NoConfigsHint,
    // ---- content: empty state ----
    WelcomeTitle,
    WelcomeBody,
    CreateConfiguration,
    // ---- content: config detail ----
    DetailParams,
    DetailCommand,
    DetailNotes,
    Start,
    Modify,
    Delete,
    Stop,          // shown when the process is running
    CopyCommand,
    Copied,
    CoreParams,
    CustomParams,
    // ---- running view ----
    RunningTitle,
    RunLog,
    OpenInBrowser,
    StopServer,
    Port,
    Uptime,
    MetricCpu,
    MetricGpu,
    MetricMemory,
    MetricVram,
    MetricModel,
    MetricThreads,
    ServerReady,
    ServerStarting,
    ServerStopped,
    ServerFailed,
    NotAvailable,
    TotalLabel,
    // ---- settings dialog ----
    SettingsGeneral,
    SettingsLlama,
    SettingsBackup,
    Language,
    LangZh,
    LangEn,
    LlamaPath,
    LlamaPathHint,
    AutoDetect,
    AutoDetected,
    AutoDetectFailed,
    Browse,
    UsingFolders,           // text before the list, so translators can reorder
    BackupEnabled,
    BackupDir,
    BackupDirHint,
    BackupNow,
    BackupDone,
    Theme,
    // ---- param editor ----
    CreateConfigTitle,
    EditConfigTitle,
    ConfigName,
    ConfigNamePlaceholder,
    ParamGroups,
    GroupBasic,
    GroupSampling,
    GroupServer,
    GroupPerformance,
    GroupGpu,
    GroupChat,
    GroupCustom,
    AddCustomParam,
    ParamName,
    ParamValue,
    ParamDesc,
    Remove,
    Add,
    BrowseFile,
    Save,
    Cancel,
    PleaseSelectParam,
    ValueRequired,
    NameRequired,
    InvalidParam,
    // ---- import / export ----
    ImportExportTitle,
    ImportSection,
    ImportHint,
    ChooseFiles,
    ExportSection,
    ExportHint,
    SelectAll,
    ExportSelected,
    Exported,
    ImportedCount,
    ImportError,
    ExportError,
    DeleteConfirmTitle,
    DeleteConfirmBody,
    Confirm,
    // ---- help ----
    HelpTitle,
    HelpQuickStart,
    HelpQuickStartBody,
    HelpParams,
    HelpParamsBody,
    HelpTroubleshoot,
    HelpTroubleshootBody,
    HelpAbout,
    HelpVersion,
    HelpDataDir,
    OpenDataDir,
    // ---- bottom bar ----
    StatusReady,
    StatusRunning,
    StatusLlamaReady,
    StatusLlamaMissing,
    AutoBackupOn,
    // ---- common ----
    Ok,
    Close,
    Yes,
    No,
    SaveFailed,
    LoadFailed,
    UnnamedConfig,
    // Embedded web view (chat page).
    WebViewLoading,
    WebViewFailed,
    Reload,
    // The chat page was asked for while no llama-server is running.
    ChatNeedsServer,
    CloseToTray,
    ModelMapped,
    RuntimeCommit,
    TrayOpen,
    TrayQuit,
    // Resource monitor: shrink-to-ball button; empty-log placeholders.
    MonitorMinimize,
    LogEmpty,
    LogExternal,
    ServerBusy,
    // Read-only strip showing the command the launch would run.
    CommandPreview,
    // Rejected when the user picks a name a configuration already uses.
    NameDuplicate,
    // Group rail entry that clears the filter and shows every parameter.
    AllParams,
    // Theme (light / dark / follow Windows).
    ThemeMode,
    ThemeSystem,
    ThemeLight,
    ThemeDark,
    // Short form used as the toolbar caption, where the button is only a few
    // dozen pixels wide. Placed last-but-one so `ConfigsCount` stays the
    // sentinel the static_assert sanity-checks against.
    ImportExportShort,
    ConfigsCount,
};

Lang current();
void set(Lang lang);
// Falls back to English whenever a translation is missing.
const wchar_t* raw(Str id);
// Convenience wrapper so call sites read as `T(Str::Start)`.
inline const wchar_t* T(Str id) { return raw(id); }

}  // namespace i18n

// Pulled into the global namespace on purpose: string lookups appear all over
// the UI code and the `T(Str::Start)` form keeps those call sites readable.
using i18n::Lang;
using i18n::Str;
using i18n::T;