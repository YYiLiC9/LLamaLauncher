// app.h - application shell: main window, view routing and shared state.
#pragma once

#include <windows.h>
#include <shellapi.h>   // NOTIFYICONDATAW / Shell_NotifyIconW

#include <string>
#include <vector>

#include "core/gguf.h"
#include "core/monitor.h"
#include "core/process.h"
#include "core/store.h"
#include "ui/shell.h"
#include "ui/views.h"
#include "ui/webview.h"

namespace app {

// Which view fills the content area (main-3).
enum class View {
    Welcome,    // nothing selected: the create button and the intro copy
    Detail,     // main 3-1: parameters of the selected configuration
    Running,    // main 3-2: live resource usage
    Chat,       // main 3-3: the llama-server chat page in an embedded browser
};

// Every clickable thing is registered as a hit region while painting. Mouse
// events are matched against the regions from the last frame, so hit testing
// and drawing can never drift apart.
enum class Action {
    None,
    SelectConfig,
    NewConfig,
    OpenSettings,
    OpenImport,       // import & export share one dialog, so one action
    OpenWeb,          // switches to the embedded chat view
    OpenInBrowser,    // hands the chat URL to the system browser
    OpenHelp,
    Start,
    Stop,
    Modify,
    Delete,
    OpenChatPage,
    CopyCommand,
    OpenLog,
    OpenDataDir,
    ToggleNotes,
    ClearSearch,
    CloseChat,
    ReloadChat,
    MinimizeMonitor,  // shrink the resource view to the corner ball
    RestoreMonitor,   // ball click: bring the resource view back
};

struct Hit {
    shell::Rect rect;
    Action action = Action::None;
    std::wstring payload;
    bool enabled = true;
};

// What the running server's memory is made of, as far as it can be proven from
// outside the process. Every field is either measured by the monitor, read from
// the GGUF header, or the remainder of those two - nothing here is a guess, and
// a component that cannot be pinned down stays inside `other*`.
struct MemorySlices {
    uint64_t weightsGpu = 0;   // model weights resident in VRAM
    uint64_t kvGpu = 0;        // share of the KV cache held in VRAM
    uint64_t otherGpu = 0;     // rest of the server's VRAM: compute buffers,
                               // backend runtime, and any weights whose share
                               // could not be proven
    uint64_t kvRam = 0;        // share of the cache that did not fit in VRAM
    uint64_t otherRam = 0;     // rest of the server's working set
    bool weightsKnown = false; // weightsGpu is a real figure, not a remainder
    bool kvKnown = false;      // the cache size could be computed at all
    uint64_t kvTotal = 0;      // whole cache, regardless of where it lives
    // Inputs behind `kvTotal`, kept so the hover bubble can show its working
    // instead of just the answer.
    uint64_t context = 0;      // tokens the cache was sized for
    std::wstring cacheType;    // quantisation of the K/V tensors
};

// One hoverable slice of the memory card: where it was drawn plus what the
// bubble says. Registered while painting, hit-tested on the next mouse move -
// the same one-frame-late contract as Hit.
struct SliceTip {
    shell::Rect rect;
    std::wstring title;
    std::wstring body;
};

class App {
public:
    static App& instance();

    bool init(HINSTANCE inst);
    int run();

    // ------------------------------------------------------------------ state
    HWND hwnd() const { return hwnd_; }
    store::Store& store() { return store_; }

    // Reloads everything from disk and repaints. Called after any dialog that
    // may have changed settings or configurations.
    void refresh();
    void setLanguage(Lang lang);

    bool startConfig(const std::wstring& id);
    void stopServer();
    void clearSelection();

    void openChatPage();
    void openChatInBrowser();
    void openLogWindow();
    void copyCommandToClipboard();
    void openDataDir();

    const store::Config* selected() const;
    const std::wstring& selectedId() const { return selectedId_; }
    View view() const { return view_; }
    void setView(View v);

    // ------------------------------------------------------------------ paint
    void paint(HDC dc, const shell::Rect& client);

    // Layout regions, recomputed on demand from the current client size.
    struct Frame {
        shell::Rect client;
        shell::Rect topBar;
        shell::Rect topTitle;
        shell::Rect topTools;      // toolbar strip on the right of the top bar
        shell::Rect sidebar;
        shell::Rect sidebarHeader;
        shell::Rect search;
        shell::Rect list;
        shell::Rect content;
        shell::Rect chatBar;       // strip above the embedded browser
        shell::Rect chatPage;      // area the browser fills
        shell::Rect bottomBar;
    };
    Frame layout(const shell::Rect& client) const;
    Frame currentFrame() const;

private:
    App() = default;

    // ------------------------------------------------------------------ paint
    void paintTopBar(shell::Canvas& c, const Frame& f);
    void paintSidebar(shell::Canvas& c, const Frame& f);
    void paintContent(shell::Canvas& c, const Frame& f);
    void paintWelcome(shell::Canvas& c, const shell::Rect& area, const Frame& f);
    void paintDetail(shell::Canvas& c, const shell::Rect& area, const store::Config& cfg);
    void paintRunning(shell::Canvas& c, const shell::Rect& area, const store::Config& cfg);
    void paintChat(shell::Canvas& c, const Frame& f);
    void paintBottomBar(shell::Canvas& c, const Frame& f);

    // ------------------------------------------------------------------ input
    void onMouseMove(int x, int y);
    void onMouseLeave();
    // Memory-card slice under the client point, or -1.
    int sliceTipAt(int x, int y) const;
    // Sidebar entry under the client point, or -1. Used by the right-click menu.
    int configItemAt(const shell::Rect& listArea, POINT clientPt) const;
    void onLButtonDown(int x, int y);
    void onLButtonUp(int x, int y);
    void onMouseWheel(int delta, int x, int y);
    void onTimer();
    void onSize();
    void dispatch(const Hit& hit);
    void addHit(const shell::Rect& r, Action a, const std::wstring& payload = L"",
                bool enabled = true);

    // ------------------------------------------------------------------ win32
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK SearchProc(HWND, UINT, WPARAM, LPARAM);
    static App* fromWindow(HWND hwnd);

    // Re-applies the native chrome (caption colours, dark-title-bar flag) after
    // the palette changes.
    void applyChrome();

    void ensureSearchEdit();
    void placeSearchEdit();
    // Syncs the embedded browser pane with the current view and layout. Called
    // from onSize and whenever the view changes.
    void syncWebView();
    int  chatPort() const;
    // Port the embedded browser was last pointed at; a WM_SIZE must not
    // re-navigate (that reloads the page and loses the chat).
    int webviewNavPort_ = -1;
    std::vector<const store::Config*> filteredConfigs() const;
    bool anyServerRunning() const;
    DWORD activePid() const;
    bool processAlive() const;

    HINSTANCE inst_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND searchEdit_ = nullptr;

    store::Store store_;
    std::wstring selectedId_;
    View view_ = View::Welcome;

    proc::ServerProcess server_;
    monitor::Monitor monitor_;
    // Notification-area icon. Always present: it is the way back in when the
    // window is hidden to the tray on close, and a quit affordance otherwise.
    NOTIFYICONDATAW tray_{};
    bool trayAdded_ = false;
    // Configuration the running server belongs to. Empty while an adopted
    // external llama-server runs (its origin is unknown) or nothing runs.
    std::wstring runningConfigId_;
    // Created lazily the first time the chat view is opened, so an unused
    // launcher never spawns a browser process.
    web::WebView webView_;
    DWORD externalPid_ = 0;        // llama-server started outside the launcher
    bool lastRunning_ = false;
    int  lastExitCode_ = -1;

    std::vector<std::wstring> logTail_;
    bool logDirty_ = false;

    // ---- model facts for the running server ----
    // The KV cache cannot be measured from outside the process, but it can be
    // computed: the GGUF header supplies the layer and head counts, and the
    // startup log says how many layers reached the GPU. Read once per run so
    // the memory card shows a calculated split rather than a guess.
    gguf::Meta modelMeta_;
    uint32_t offloadedGpu_ = 0;     // X of "offloaded X/Y layers to GPU"
    uint32_t offloadedTotal_ = 0;   // Y of the same line
    bool offloadKnown_ = false;
    // Recent builds stopped printing the offload line, but they do print
    // "n_slots = N, n_ctx_slot = M", which is the cache capacity the server
    // really allocated - better evidence than the -c value we passed it.
    uint32_t ctxSlots_ = 0;
    uint32_t ctxPerSlot_ = 0;
    bool ctxKnown_ = false;

    // The configuration the running server belongs to (null for an adopted
    // external process, whose model we cannot know).
    const store::Config* runningConfig() const;
    void loadModelMeta(const store::Config& cfg);
    void resetModelMeta();
    // Pulls the model facts out of one captured log line: the offload split
    // and the cache capacity actually allocated.
    void scanLogLine(const std::wstring& line);
    // Composition of the running server's VRAM and RAM for the memory card.
    // A component that cannot be proven is left folded into the `other*`
    // remainder rather than spread over the named slices.
    MemorySlices memorySlices() const;
    // Size of the running model on disk (all shards), 0 when unknown.
    uint64_t modelWeightBytes() const;

    std::vector<Hit> hits_;
    int hoverIndex_ = -1;
    int pressIndex_ = -1;
    bool trackingLeave_ = false;

    // Hover bubbles for the memory card slices. Filled in while painting and
    // read by the next mouse move; `sliceHover_` is -1 when the pointer is not
    // over a slice.
    std::vector<SliceTip> sliceTips_;
    int sliceHover_ = -1;

    int sidebarScroll_ = 0;
    int contentScroll_ = 0;
    int contentScrollMax_ = 0;
    bool notesOpen_ = false;

    std::wstring searchText_;
    std::wstring toast_;
    int64_t toastUntil_ = 0;
    int64_t runStarted_ = 0;

    HICON icon_ = nullptr;
};

// Shared by the paint and input translation units.
constexpr wchar_t kWindowClass[] = L"LlamaLauncherMainWindow";
constexpr int kBaseWindowW = 1180;
constexpr int kBaseWindowH = 760;
// Tray callback message. Distinct from the webview's init message so the two
// custom message streams can never collide.
constexpr UINT kTrayMessage = WM_APP + 0x2C5;

}  // namespace app