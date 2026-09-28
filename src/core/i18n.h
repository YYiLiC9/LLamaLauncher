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
    ModelWeights,
    // Memory card: the KV slice is computed from the GGUF metadata, so it is
    // labelled as exactly that - not as "KV + compute" any more.
    KvCache,
    WeightsCompute,
    OtherProcs,
    FreeSpace,
    // "(disk)" qualifier for the model-weights footnote.
    OnDisk,
    // Legend label for the unsplit slice when the KV size cannot be computed.
    ServerProc,
    // Title of the two-bar capacity card (VRAM + RAM).
    Capacity,
    TrayOpen,
    TrayQuit,
    // Resource monitor: shrink-to-ball button; empty-log placeholders.
    MonitorMinimize,
    LogEmpty,
    LogExternal,
    // Line counter in the log viewer header ("%zu lines").
    LogLineCount,
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
    // ---- memory card slices and their hover bubbles ----
    // A slice is only ever named for something that was measured or computed;
    // the rest stays in the "remaining" slice, which is what these labels say.
    SliceWeights,
    SliceKv,
    SliceCompute,
    SliceWeightsCompute,
    SliceWorkingSetRam,
    SliceOtherProcs,
    SliceFree,
    // Bubble bodies: one line on where the number came from, so a figure that
    // had to be derived is never presented as if it had been read off a meter.
    TipWeightsBody,
    TipWeightsPartialBody,
    TipKvBody,
    TipComputeBody,
    TipWeightsComputeBody,
    TipWorkingSetBody,
    TipOtherBody,
    TipFreeBody,
    // CPU tile: the secondary line is the server's own CPU share, so the
    // figure under the total CPU% is the same quantity scoped to llama-server.
    MetricCpuProc,
    // Log viewer: the log is drawn by hand, so copying needs an explicit
    // button, a hint that text can be selected, and feedback after a copy.
    LogCopy,
    LogCopiedChars,
    LogSelectHint,
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