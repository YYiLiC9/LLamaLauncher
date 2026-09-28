// app.cpp - window lifetime, state changes and the periodic refresh.
#include "app.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>   // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <cstdio>
#include <cwchar>

#include "core/i18n.h"
#include "core/paths.h"
#include "core/util.h"
#include "ui/theme.h"

using shell::Rect;

namespace app {

namespace {

constexpr wchar_t kSearchClass[] = L"LlamaLauncherSearchBox";
constexpr UINT_PTR kTimerId = 1;
constexpr int kTimerMs = 1000;      // resource sampling cadence

// The search box superclasses the standard EDIT control; everything the custom
// handler does not touch is chained back to EDIT itself.
WNDPROC g_searchEditBase = nullptr;

// Windows 11 non-client attributes. Setting these through DwmSetWindowAttribute
// is what gives the window rounded corners and a light title bar that matches
// the app surface.
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

void applyWindowChrome(HWND hwnd) {
    int corner = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    // A caption bar that follows the live palette. No Mica: the app paints its
    // own surface (see theme::applyCaptionTheme for why). The caption colours
    // are set there too, so dialogs get identical chrome.
    theme::applyCaptionTheme(hwnd);
}

}  // namespace

// ---------------------------------------------------------------- singleton --
App& App::instance() {
    static App app;
    return app;
}

App* App::fromWindow(HWND hwnd) {
    return (App*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

// --------------------------------------------------------------------- init --
void App::applyChrome() { applyWindowChrome(hwnd_); }

bool App::init(HINSTANCE inst) {
    inst_ = inst;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&icc);

    store_.load();
    i18n::set(util::iequals(store_.settings().language, L"en") ? Lang::En : Lang::Zh);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = &App::WndProc;
    wc.hInstance = inst_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;   // the whole client area is painted by hand
    wc.lpszClassName = kWindowClass;
    // The compiled-in resource icon (res/app.rc, id 1) for the title bar, the
    // taskbar and Alt-Tab; fall back to the generic one if the resource is
    // somehow missing.
    wc.hIcon = ::LoadIconW(inst_, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = (HICON)::LoadImageW(inst_, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                     ::GetSystemMetrics(SM_CXSMICON),
                                     ::GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!wc.hIconSm) wc.hIconSm = wc.hIcon;
    if (!::RegisterClassExW(&wc)) return false;

    // The search box superclasses the standard EDIT control. A hand-rolled
    // class cannot work here: a non-EDIT child never sends WM_COMMAND with
    // EN_CHANGE (nor WM_CTLCOLOREDIT), which left the sidebar filter dead.
    // Superclassing keeps real text editing, the caret and those notifications
    // while SearchProc still provides the themed background and Escape handling.
    WNDCLASSEXW ec{};
    ec.cbSize = sizeof(ec);
    if (!::GetClassInfoExW(nullptr, L"EDIT", &ec)) return false;
    g_searchEditBase = ec.lpfnWndProc;      // chain here for everything else
    ec.lpfnWndProc = &App::SearchProc;
    ec.hInstance = inst_;
    ec.lpszClassName = kSearchClass;
    if (!::RegisterClassExW(&ec)) return false;

    hwnd_ = ::CreateWindowExW(0, kWindowClass, L"LlamaLauncher",
                              WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                              kBaseWindowW, kBaseWindowH, nullptr, nullptr, inst_, this);
    if (!hwnd_) return false;

    // The palette has to exist before the chrome reads it, otherwise the
    // caption colours are computed from an empty palette.
    theme::init(hwnd_);
    // Honour a pinned light/dark choice from the settings before the first
    // paint, so the window never flashes the wrong theme.
    theme::setThemeMode(theme::modeFromSetting(store_.settings().theme));
    applyWindowChrome(hwnd_);

    // Centre the window on the work area at the size the design assumes, but
    // never larger than the screen: on a high-DPI display a 175% scaling of the
    // design size can exceed the desktop, which would push the right-hand end of
    // the toolbar off-screen.
    RECT wa{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0)) {
        int workW = wa.right - wa.left;
        int workH = wa.bottom - wa.top;
        int margin = theme::M.px(16);
        int w = std::min(theme::M.px(kBaseWindowW), std::max(theme::M.px(640), workW - margin * 2));
        int h = std::min(theme::M.px(kBaseWindowH), std::max(theme::M.px(480), workH - margin * 2));
        int x = wa.left + (workW - w) / 2;
        int y = wa.top + (workH - h) / 2;
        ::SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    onSize();

    // Notification-area icon (always on): it restores a window hidden to the
    // tray on close, and offers a quit path while the title bar is gone.
    ::memset(&tray_, 0, sizeof(tray_));
    tray_.cbSize = sizeof(tray_);
    tray_.hWnd = hwnd_;
    tray_.uID = 1;
    tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray_.uCallbackMessage = kTrayMessage;
    tray_.hIcon = (HICON)::LoadImageW(inst_, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                      ::GetSystemMetrics(SM_CXSMICON),
                                      ::GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!tray_.hIcon) tray_.hIcon = wc.hIcon;
    ::lstrcpynW(tray_.szTip, L"LlamaLauncher", (int)_countof(tray_.szTip));
    trayAdded_ = ::Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;

    // A server left over from a previous run is adopted so the running view
    // still reports something useful.
    DWORD existing = util::findProcessByName(L"llama-server.exe");
    if (existing) externalPid_ = existing;

    return true;
}

int App::run() {
    ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);

    applyWindowChrome(hwnd_);

    ::SetTimer(hwnd_, kTimerId, kTimerMs, nullptr);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0)) {
        // Escape and Return are handled globally so the search box behaves the
        // way people expect without stealing keystrokes from future inputs.
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            if (::GetFocus() == searchEdit_) {
                searchText_.clear();
                ::SetWindowTextW(searchEdit_, L"");
                ::SetFocus(hwnd_);
                clearSelection();
                refresh();
                continue;
            }
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    return 0;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App* self = fromWindow(hwnd);

    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (App*)cs->lpCreateParams;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->hwnd_ = hwnd;
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = ::BeginPaint(hwnd, &ps);
            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            self->paint(dc, Rect{0, 0, rc.right, rc.bottom});
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;   // fully covered by WM_PAINT

        case WM_ENABLE:
            // EnableWindow(FALSE/TRUE) around every modal dialog asks
            // DefWindowProc to repaint this window. The client is hand-drawn
            // and looks identical either way, so those full repaints are pure
            // churn - and the pair of them (open + close of the dialog) is
            // what made the main window flash whenever a child window went
            // away. Nothing needs repainting here; the real paint is driven
            // by invalidation as usual.
            return 0;

        // The search box is a real EDIT and paints its own white background
        // whatever the palette says, so it has to be answered here too.
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wp;
            ::SetBkMode(dc, OPAQUE);
            ::SetTextColor(dc, theme::TextPrimary);
            ::SetBkColor(dc, theme::fieldBack(true));
            return (LRESULT)theme::fieldBrush(true);
        }

        case WM_SIZE:
            self->onSize();
            return 0;

        case WM_GETMINMAXINFO: {
            auto* mmi = (MINMAXINFO*)lp;
            mmi->ptMinTrackSize.x = theme::M.px(920);
            mmi->ptMinTrackSize.y = theme::M.px(580);
            return 0;
        }

        case WM_SETTINGCHANGE:
            // Fired when the user switches Windows between light and dark, or
            // changes the accent colour.
            theme::refreshSystemTheme();
            applyWindowChrome(hwnd);
            // The embedded chat page follows the app's resolved palette too.
            self->webView_.applyTheme();
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;

        case WM_CONTEXTMENU:
            // Right-click on a configuration entry: quick start / modify /
            // delete without walking into the detail page first.
            if ((HWND)wp == hwnd) {
                POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
                POINT client = pt;
                bool keyboard = (pt.x == -1 && pt.y == -1);
                if (keyboard) {
                    if (!::GetCursorPos(&pt)) return 0;
                    client = pt;
                    ::ScreenToClient(hwnd, &client);
                } else {
                    ::ScreenToClient(hwnd, &client);
                }

                Frame f = self->currentFrame();
                if (!f.list.contains(client.x, client.y)) return 0;
                int idx = self->configItemAt(f.list, client);
                if (idx < 0) return 0;
                const store::Config* cfg = self->filteredConfigs()[(size_t)idx];
                if (!cfg) return 0;

                HMENU menu = ::CreatePopupMenu();
                ::AppendMenuW(menu, MF_STRING, 1, T(Str::Start));
                ::AppendMenuW(menu, MF_STRING, 2, T(Str::Modify));
                ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                ::AppendMenuW(menu, MF_STRING, 3, T(Str::Delete));
                int cmd = ::TrackPopupMenu(menu,
                                           TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                           pt.x, pt.y, 0, hwnd, nullptr);
                ::DestroyMenu(menu);

                Action action = cmd == 1   ? Action::Start
                                : cmd == 2 ? Action::Modify
                                : cmd == 3 ? Action::Delete
                                           : Action::None;
                if (action != Action::None) {
                    self->dispatch(Hit{shell::Rect{}, action, cfg->id, true});
                }
                return 0;
            }
            break;

        case WM_DPICHANGED: {
            theme::onDpiChanged(HIWORD(wp));
            auto* suggested = (RECT*)lp;
            ::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                           suggested->right - suggested->left, suggested->bottom - suggested->top,
                           SWP_NOZORDER | SWP_NOACTIVATE);
            self->placeSearchEdit();
            // theme::onDpiChanged rebuilt the font handles; the search box was
            // created with the old one and would render with a stale HFONT.
            ::SendMessageW(self->searchEdit_, WM_SETFONT, (WPARAM)theme::fontBody(), TRUE);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (!self->trackingLeave_) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                ::TrackMouseEvent(&tme);
                self->trackingLeave_ = true;
            }
            self->onMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_MOUSELEAVE:
            self->trackingLeave_ = false;
            self->onMouseLeave();
            return 0;

        case WM_LBUTTONDOWN:
            ::SetFocus(hwnd);
            self->onLButtonDown((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_LBUTTONUP:
            self->onLButtonUp((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_LBUTTONDBLCLK:
            // Double clicking a configuration row launches it.
            self->onMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
            if (self->hoverIndex_ >= 0 && self->hoverIndex_ < (int)self->hits_.size()) {
                const Hit& h = self->hits_[(size_t)self->hoverIndex_];
                if (h.action == Action::SelectConfig && !h.payload.empty())
                    self->startConfig(h.payload);
            }
            return 0;

        case WM_MOUSEWHEEL: {
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ::ScreenToClient(hwnd, &pt);
            self->onMouseWheel(GET_WHEEL_DELTA_WPARAM(wp), pt.x, pt.y);
            return 0;
        }

        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                POINT pt{};
                ::GetCursorPos(&pt);
                ::ScreenToClient(hwnd, &pt);
                for (const Hit& h : self->hits_) {
                    if (h.enabled && h.action != Action::None && h.rect.contains(pt.x, pt.y)) {
                        ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
            break;

        case WM_COMMAND:
            if (HIWORD(wp) == EN_CHANGE && (HWND)lp == self->searchEdit_) {
                int len = ::GetWindowTextLengthW(self->searchEdit_);
                std::wstring text((size_t)len + 1, L'\0');
                ::GetWindowTextW(self->searchEdit_, text.data(), len + 1);
                text.resize((size_t)len);
                self->searchText_ = text;
                self->sidebarScroll_ = 0;
                ::InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_TIMER:
            if (wp == kTimerId) {
                self->onTimer();
                return 0;
            }
            break;

        case WM_APP + 0x2C5: {   // kTrayMessage - notification-area callback
            if (lp == WM_LBUTTONUP || lp == NIN_SELECT) {
                // Left click: bring the window back from the tray.
                ::ShowWindow(hwnd, SW_SHOW);
                if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
                ::SetForegroundWindow(hwnd);
            } else if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                HMENU menu = ::CreatePopupMenu();
                ::AppendMenuW(menu, MF_STRING, 1, T(Str::TrayOpen));
                ::AppendMenuW(menu, MF_STRING, 2, T(Str::TrayQuit));
                // The menu needs its owner foreground or it will not dismiss
                // on an outside click.
                ::SetForegroundWindow(hwnd);
                POINT pt{};
                ::GetCursorPos(&pt);
                int cmd = ::TrackPopupMenu(menu,
                                           TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                           pt.x, pt.y, 0, hwnd, nullptr);
                ::DestroyMenu(menu);
                if (cmd == 1) {
                    ::ShowWindow(hwnd, SW_SHOW);
                    if (::IsIconic(hwnd)) ::ShowWindow(hwnd, SW_RESTORE);
                    ::SetForegroundWindow(hwnd);
                } else if (cmd == 2) {
                    ::DestroyWindow(hwnd);
                }
            }
            return 0;
        }

        case WM_CLOSE:
            // The tray setting turns the X button into "hide and keep
            // running" - a live server survives a stray click on X. The tray
            // icon's 退出 menu item calls DestroyWindow directly.
            if (self->store_.settings().closeToTray) {
                ::ShowWindow(hwnd, SW_HIDE);
                return 0;
            }
            break;

        case WM_DESTROY:
            ::KillTimer(hwnd, kTimerId);
            if (self->trayAdded_) {
                ::Shell_NotifyIconW(NIM_DELETE, &self->tray_);
                self->trayAdded_ = false;
            }
            self->server_.stop();
            // Shut the embedded browser down explicitly so its helper processes
            // do not outlive the window.
            self->webView_.destroy();
            ::PostQuitMessage(0);
            return 0;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK App::SearchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC dc = (HDC)wp;
            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            HBRUSH b = ::CreateSolidBrush(theme::CardBg);
            ::FillRect(dc, &rc, b);
            ::DeleteObject(b);
            return 1;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            ::InvalidateRect(::GetParent(hwnd), nullptr, FALSE);
            break;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                ::SetWindowTextW(hwnd, L"");
                ::SetFocus(::GetParent(hwnd));
                return 0;
            }
            break;
    }
    // Text storage, caret and the EN_CHANGE notifications the sidebar filter
    // depends on all live in the EDIT implementation we superclassed.
    return ::CallWindowProcW(g_searchEditBase, hwnd, msg, wp, lp);
}

void App::ensureSearchEdit() {
    if (!searchEdit_) {
        searchEdit_ = ::CreateWindowExW(0, kSearchClass, L"",
                                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd_,
                                        nullptr, inst_, nullptr);
    }
    if (searchEdit_) {
        ::SendMessageW(searchEdit_, WM_SETFONT, (WPARAM)theme::fontBody(), TRUE);
        ::SendMessageW(searchEdit_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                       MAKELPARAM(theme::M.px(6), theme::M.px(6)));
    }
}

void App::placeSearchEdit() {
    ensureSearchEdit();
    if (!searchEdit_) return;
    // Inset so the box sits inside the rounded frame drawn around it.
    Frame f = currentFrame();
    int inset = theme::M.px(30);   // room for the search glyph on the left
    Rect box{f.search.x + inset, f.search.y + theme::M.px(4),
             f.search.w - inset - theme::M.px(8), f.search.h - theme::M.px(8)};
    ::SetWindowPos(searchEdit_, nullptr, box.x, box.y, box.w, box.h,
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

// ------------------------------------------------------------------ refresh --
void App::refresh() {
    store_.load();
    if (!selectedId_.empty() && !store_.find(selectedId_)) {
        selectedId_.clear();
        view_ = View::Welcome;
    }
    if (selectedId_.empty() && view_ != View::Welcome) view_ = View::Welcome;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::setLanguage(Lang lang) {
    i18n::set(lang);
    store_.settings().language = (lang == Lang::En) ? L"en" : L"zh";
    store_.saveSettings();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::clearSelection() {
    selectedId_.clear();
    view_ = View::Welcome;
    contentScroll_ = 0;
}

const store::Config* App::selected() const {
    if (selectedId_.empty()) return nullptr;
    return store_.find(selectedId_);
}

void App::setView(View v) {
    view_ = v;
    contentScroll_ = 0;
    // Hides the embedded browser when leaving the chat view.
    syncWebView();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

bool App::anyServerRunning() const { return processAlive(); }

bool App::processAlive() const { return server_.isRunning() || externalPid_ != 0; }

DWORD App::activePid() const {
    if (server_.isRunning()) return server_.pid();
    return externalPid_;
}

std::vector<const store::Config*> App::filteredConfigs() const {
    std::vector<const store::Config*> out;
    std::wstring needle = util::lower(util::trim(searchText_));
    for (const store::Config& c : store_.configs()) {
        if (needle.empty()) {
            out.push_back(&c);
            continue;
        }
        std::wstring hay = util::lower(c.name + L" " + c.id + L" " + c.notes + L" " + c.modelFile());
        if (hay.find(needle) != std::wstring::npos) out.push_back(&c);
    }
    return out;
}

int App::configItemAt(const shell::Rect& listArea, POINT clientPt) const {
    // Same row arithmetic paintSidebar uses, so the menu always targets the
    // entry under the pointer.
    auto list = filteredConfigs();
    int rowH = theme::M.px(58);
    int gap = theme::M.px(3);
    int rel = clientPt.y - listArea.y + sidebarScroll_;
    if (rel < 0) return -1;
    int idx = rel / (rowH + gap);
    int within = rel - idx * (rowH + gap);
    if (idx >= (int)list.size() || within > rowH) return -1;
    return idx;
}

// ------------------------------------------------------------- model facts --
// Everything below exists so the memory card can show a *computed* KV split
// instead of the old "dedicated VRAM minus the model file" guess, which went
// negative as soon as the model did not fit the card.
namespace {

// Context size that actually reaches llama-server. Only an enabled -c row ends
// up on the command line, so a defaulted but switched-off value must not be
// used - it would double the reported cache.
uint64_t activeContext(const store::Config& cfg) {
    for (const store::Param& p : cfg.params) {
        if (p.enabled && (p.flag == L"-c" || p.flag == L"--ctx-size")) {
            uint64_t v = ::_wcstoui64(p.value.c_str(), nullptr, 10);
            if (v) return v;
        }
    }
    return 4096;  // llama.cpp's own default when -c is not passed at all
}

// Same rule for the KV quantisation: switched-off rows are not on the command.
std::wstring activeValue(const store::Config& cfg, const std::wstring& flag,
                         const std::wstring& longFlag) {
    for (const store::Param& p : cfg.params) {
        if (p.enabled && (p.flag == flag || p.flag == longFlag)) return p.value;
    }
    return std::wstring();
}

}  // namespace

const store::Config* App::runningConfig() const {
    if (runningConfigId_.empty()) return nullptr;
    return store_.find(runningConfigId_);
}

void App::loadModelMeta(const store::Config& cfg) {
    resetModelMeta();
    // Header only: one 8 MB read, never the tensor data.
    modelMeta_ = gguf::readMeta(cfg.modelFile());
}

void App::resetModelMeta() {
    modelMeta_ = gguf::Meta{};
    offloadedGpu_ = 0;
    offloadedTotal_ = 0;
    offloadKnown_ = false;
    ctxSlots_ = 0;
    ctxPerSlot_ = 0;
    ctxKnown_ = false;
}

void App::scanLogLine(const std::wstring& line) {
    // Cache capacity: "initializing, n_slots = 4, n_ctx_slot = 32768".
    if (!ctxKnown_) {
        size_t at = line.find(L"n_slots =");
        if (at != std::wstring::npos) {
            unsigned slots = 0;
            unsigned perSlot = 0;
            if (::swscanf_s(line.c_str() + at, L"n_slots = %u, n_ctx_slot = %u", &slots,
                            &perSlot) == 2 &&
                slots && perSlot) {
                ctxSlots_ = slots;
                ctxPerSlot_ = perSlot;
                ctxKnown_ = true;
            }
        }
    }
    if (offloadKnown_) return;
    // "offloaded 41/48 layers to GPU" - printed by older builds only.
    size_t at = line.find(L"offloaded");
    if (at == std::wstring::npos) return;
    unsigned gpu = 0;
    unsigned total = 0;
    if (::swscanf_s(line.c_str() + at, L"offloaded %u/%u", &gpu, &total) != 2 || total == 0) return;
    offloadedGpu_ = gpu;
    offloadedTotal_ = total;
    offloadKnown_ = true;
}

app::MemorySlices App::memorySlices() const {
    MemorySlices s;
    const store::Config* cfg = runningConfig();
    const uint64_t dedicated = monitor_.gpuDedicatedBytes();
    const uint64_t workingSet = monitor_.processWorkingSet();
    const uint64_t weights = modelWeightBytes();

    // ---- how much of the model is in VRAM ----
    // Two ways to know: the offload line some builds still print, or the plain
    // observation that a GPU holding fewer bytes than the file cannot be
    // holding the whole file. Recent builds print neither a buffer size nor an
    // offload line, so the second test is what usually runs - and when even it
    // fails (partially offloaded model) the weight slice is simply not drawn.
    if (weights) {
        if (offloadKnown_ && offloadedTotal_) {
            s.weightsGpu = weights * offloadedGpu_ / offloadedTotal_;
            s.weightsKnown = true;
        } else if (dedicated >= weights) {
            s.weightsGpu = weights;
            s.weightsKnown = true;
        }
    }

    // ---- the cache ----
    if (cfg && modelMeta_.valid) {
        // Prefer the capacity the server reported over the -c we handed it.
        uint64_t context = ctxKnown_ ? (uint64_t)ctxSlots_ * ctxPerSlot_ : activeContext(*cfg);
        uint64_t total = gguf::cacheBytes(modelMeta_, context,
                                          activeValue(*cfg, L"-ctk", L"--cache-type-k"),
                                          activeValue(*cfg, L"-ctv", L"--cache-type-v"));
        if (total) {
            s.kvKnown = true;
            s.kvTotal = total;
            s.context = context;
            s.cacheType = activeValue(*cfg, L"-ctk", L"--cache-type-k");
            if (s.cacheType.empty()) s.cacheType = L"f16";
            // llama.cpp fills the device before it spills, so the cache takes
            // whatever VRAM the weights left - capped by the cache's own size.
            // Whatever is left over after that is buffers, and whatever did not
            // fit has to be living in RAM.
            uint64_t room = dedicated > s.weightsGpu ? dedicated - s.weightsGpu : 0;
            s.kvGpu = std::min(total, room);
            s.kvRam = total - s.kvGpu;
        }
    }

    // ---- remainders ----
    // These are measured, not allocated by rule: they are whatever the monitor
    // reports that the named slices do not account for.
    uint64_t taken = s.weightsGpu + s.kvGpu;
    s.otherGpu = dedicated > taken ? dedicated - taken : 0;
    s.otherRam = workingSet > s.kvRam ? workingSet - s.kvRam : 0;
    return s;
}

uint64_t App::modelWeightBytes() const {
    const store::Config* cfg = runningConfig();
    if (!cfg) return 0;
    return util::modelFileBytes(cfg->modelFile());
}

// ------------------------------------------------------------------- actions --
bool App::startConfig(const std::wstring& id) {
    const store::Config* cfg = store_.find(id);
    if (!cfg) return false;

    // llama-server serves one model per process: a second "启动" while
    // something is already running would silently kill the first model.
    // The detail pages of the other configurations show 启动 too, so this
    // needs a visible explanation rather than a silent switch.
    if (processAlive() && runningConfigId_ != id) {
        toast_ = T(Str::ServerBusy);
        toastUntil_ = util::nowSeconds() + 4;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }

    if (server_.isRunning()) server_.stop();
    externalPid_ = 0;

    std::wstring exe = store_.serverExe();
    if (exe.empty()) {
        views::message(hwnd_, T(Str::SettingsLlama), T(Str::StatusLlamaMissing));
        return false;
    }

    std::wstring args = store::buildArgs(*cfg);
    std::wstring logPath =
        util::joinPath(paths::logDir(), util::format(L"llama-server-%s.log", cfg->id.c_str()));

    std::wstring error;
    if (!server_.start(exe, args, store_.effectiveLlamaDir(), logPath, error)) {
        views::message(hwnd_, T(Str::ServerFailed), error);
        // Keep externalPid_ here: a llama-server that was already running
        // before we tried (and failed) must stay visible to the timer and to
        // 停止服务 - clearing the pid orphaned it from the UI.
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return false;
    }
    // Our own server is tracked by Server; only now is the adopted external
    // pid irrelevant.
    externalPid_ = 0;

    logTail_.clear();

    runStarted_ = util::nowSeconds();
    runningConfigId_ = id;
    loadModelMeta(*cfg);
    selectedId_ = id;
    view_ = View::Running;
    contentScroll_ = 0;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void App::stopServer() {
    server_.stop();
    // A llama-server the app did not start itself is not tracked by Server:
    // without an explicit terminate, "停止服务" only cleared the UI while the
    // process kept the port. Terminate and wait briefly - a GPU driver
    // unmapping a big VRAM allocation can take seconds, and blocking the UI
    // for that long made stopping feel frozen.
    if (externalPid_ != 0) {
        if (HANDLE p = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, externalPid_)) {
            ::TerminateProcess(p, 0);
            ::WaitForSingleObject(p, 500);
            ::CloseHandle(p);
        }
        externalPid_ = 0;
    }
    logTail_.clear();

    runningConfigId_.clear();
    resetModelMeta();
    // The run is over, so the resource view has nothing left to show - fall
    // back to the selected configuration (or the welcome screen).
    if (view_ == View::Running) {
        setView(selected() ? View::Detail : View::Welcome);
        contentScroll_ = 0;
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::openChatPage() {
    // The chat page lives inside the app now. The browser pane is created on
    // first use; if the WebView2 runtime is missing the pane says so and the
    // user can still pop the page out into their normal browser.
    //
    // Without a running llama-server the pane can only ever show a connection
    // error, and tearing the pane down while its controller is still spinning
    // up has proven crash-prone - so the page is simply gated on the server.
    if (!processAlive()) {
        toast_ = T(Str::ChatNeedsServer);
        toastUntil_ = util::nowSeconds() + 4;
        setView(selected() ? View::Detail : View::Welcome);
        ::InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    view_ = View::Chat;
    contentScroll_ = 0;
    syncWebView();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

int App::chatPort() const {
    if (const store::Config* cfg = selected()) return store::configPort(*cfg);
    if (!store_.configs().empty()) return store::configPort(store_.configs().front());
    return 8080;
}

// Keeps the embedded pane in step with the view and the current layout: it is
// moved over the chat page area when the chat view is up, and hidden otherwise.
void App::syncWebView() {
    if (view_ != View::Chat) {
        webView_.setVisible(false);
        if (HWND h = webView_.host()) ::ShowWindow(h, SW_HIDE);
        return;
    }

    Frame f = currentFrame();
    if (!webView_.host()) {
        // openChatPage() gates on the server, but the server can die between
        // that check and the first WM_SIZE here - never spin up a browser for
        // a port nothing is listening on.
        if (!processAlive()) {
            if (HWND h = webView_.host()) ::ShowWindow(h, SW_HIDE);
            view_ = selected() ? View::Detail : View::Welcome;
            return;
        }
        webView_.setNotify(
            [](void* ctx) {
                ::InvalidateRect(((App*)ctx)->hwnd_, nullptr, FALSE);
            },
            this);

        std::wstring url = util::format(L"http://127.0.0.1:%d", chatPort());
        if (!webView_.create(hwnd_, util::joinPath(paths::dataRoot(), L"webview"), url)) return;
        webviewNavPort_ = chatPort();
    } else if (webviewNavPort_ != chatPort()) {
        // Re-navigate only when the port actually changed. Every WM_SIZE used
        // to land here (resizing, snapping, minimise/restore), reloading the
        // whole page and throwing away whatever was typed into the chat.
        std::wstring url = util::format(L"http://127.0.0.1:%d", chatPort());
        webView_.navigate(url);
        webviewNavPort_ = chatPort();
    }

    if (HWND h = webView_.host()) {
        RECT r{f.chatPage.x, f.chatPage.y, f.chatPage.right(), f.chatPage.bottom()};
        webView_.setBounds(r);
        ::ShowWindow(h, SW_SHOW);
        // The browser will not present its content until its controller is told
        // it is visible - hiding the pane does not do that by itself.
        webView_.setVisible(true);
    }
}

// Hands the chat URL to the system browser. Used as the escape hatch when the
// embedded pane cannot start (no WebView2 runtime) or the user simply prefers
// their own browser.
void App::openChatInBrowser() {
    std::wstring url = util::format(L"http://127.0.0.1:%d", chatPort());
    if (!proc::openInBrowser(url)) {
        toast_ = url;
        toastUntil_ = util::nowSeconds() + 4;
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::openLogWindow() {
    views::LogViewer viewer(logTail_);
    // Live: the dialog pulls logTail_ while it is open, so a log opened early
    // fills in instead of showing a frozen (often empty) snapshot.
    viewer.setProvider([this] { return logTail_; });
    if (logTail_.empty() && externalPid_)
        viewer.setEmptyHint(T(Str::LogExternal));
    viewer.show(hwnd_, T(Str::RunLog));
}

void App::copyCommandToClipboard() {
    const store::Config* cfg = selected();
    if (!cfg) return;
    std::wstring cmd = store::buildDisplayCommand(*cfg, store_.serverExe());
    bool copied = false;
    if (::OpenClipboard(hwnd_)) {
        ::EmptyClipboard();
        size_t bytes = (cmd.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            if (void* dst = ::GlobalLock(mem)) {
                memcpy(dst, cmd.c_str(), bytes);
                ::GlobalUnlock(mem);
                // Ownership passes to the clipboard on success; on failure the
                // allocation is ours to release.
                copied = ::SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
                if (!copied) ::GlobalFree(mem);
            } else {
                ::GlobalFree(mem);
            }
        }
        ::CloseClipboard();
    }
    // Only claim success when the data really landed on the clipboard.
    if (copied) {
        toast_ = T(Str::Copied);
        toastUntil_ = util::nowSeconds() + 2;
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::openDataDir() {
    std::wstring dir = paths::dataRoot();
    util::ensureDir(dir);
    ::ShellExecuteW(hwnd_, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// --------------------------------------------------------------------- timer --
void App::onTimer() {
    bool running = server_.isRunning();
    if (!running) {
        if (externalPid_) {
            DWORD found = util::findProcessByName(L"llama-server.exe");
            if (!found) externalPid_ = 0;
        }
    }

    // Drain the log every tick even when the running view is hidden, so the log
    // window always has the complete output.
    std::vector<std::wstring> fresh = server_.takeOutput();
    if (!fresh.empty()) {
        for (auto& line : fresh) {
            // The startup log is the only place the server says how much cache
            // it allocated and how many layers reached the GPU.
            scanLogLine(line);
            logTail_.push_back(std::move(line));
        }
        while (logTail_.size() > 800) logTail_.erase(logTail_.begin());
        logDirty_ = true;
    }

    bool wasRunning = lastRunning_;
    lastRunning_ = running;
    if (wasRunning && !running) {
        // The process ended on its own (crash or exit): the resource view has
        // nothing to monitor any more, so leave it like stopServer does.
        runningConfigId_.clear();
        resetModelMeta();
        if (view_ == View::Running) {
            setView(selected() ? View::Detail : View::Welcome);
            contentScroll_ = 0;
        }
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (view_ == View::Running && ::IsWindowVisible(hwnd_)) {
        monitor_.sample(activePid());
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }

    if (!toast_.empty() && util::nowSeconds() > toastUntil_) {
        toast_.clear();
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::onSize() {
    RECT rc{};
    ::GetClientRect(hwnd_, &rc);
    placeSearchEdit();
    syncWebView();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::onMouseWheel(int delta, int x, int y) {
    Frame f = currentFrame();
    int step = theme::M.px(56) * delta / WHEEL_DELTA;

    if (f.list.contains(x, y) || f.sidebar.contains(x, y)) {
        sidebarScroll_ -= step;
        if (sidebarScroll_ < 0) sidebarScroll_ = 0;
    } else if (f.content.contains(x, y)) {
        contentScroll_ -= step;
        if (contentScroll_ < 0) contentScroll_ = 0;
        if (contentScroll_ > contentScrollMax_) contentScroll_ = contentScrollMax_;
    }
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

// -------------------------------------------------------------------- layout --
App::Frame App::layout(const Rect& client) const {
    Frame f;
    f.client = client;
    f.topBar = Rect{0, 0, client.w, theme::M.topBarHeight};
    f.bottomBar = Rect{0, client.h - theme::M.bottomBarHeight, client.w, theme::M.bottomBarHeight};
    int bodyTop = f.topBar.bottom();
    int bodyH = f.bottomBar.top() - bodyTop;

    f.sidebar = Rect{0, bodyTop, theme::M.sidebarWidth, bodyH};
    f.sidebarHeader = Rect{f.sidebar.x + theme::M.padding, f.sidebar.y + theme::M.padding,
                           f.sidebar.w - theme::M.padding * 2, theme::M.px(32)};
    f.search = Rect{f.sidebar.x + theme::M.padding, f.sidebarHeader.bottom() + theme::M.gapSmall,
                    f.sidebar.w - theme::M.padding * 2, theme::M.px(32)};
    f.list = Rect{f.sidebar.x, f.search.bottom() + theme::M.gapSmall, f.sidebar.w,
                  f.sidebar.bottom() - f.search.bottom() - theme::M.gapSmall};
    f.content = Rect{f.sidebar.right(), bodyTop, client.w - f.sidebar.right(), bodyH};

    // When the chat view is up, the content area splits into a slim strip of
    // chrome and the embedded browser underneath it.
    f.chatBar = Rect{f.content.x, f.content.y, f.content.w, theme::M.px(48)};
    f.chatPage = Rect{f.content.x, f.chatBar.bottom(), f.content.w,
                      f.content.h - f.chatBar.h};

    // Top bar: title on the left, tool buttons on the right. The button strip is
    // sized to match paintTopBar's own arithmetic (4 buttons, 84px + 4px gap).
    int toolsWidth = theme::M.px(84) * 4 + theme::M.px(4) * 3;
    f.topTitle = Rect{theme::M.padding + theme::M.px(4), 0, f.topBar.w - toolsWidth - theme::M.px(80),
                      f.topBar.h};
    f.topTools = Rect{f.topBar.right() - toolsWidth - theme::M.padding, 0, toolsWidth,
                      f.topBar.h};
    return f;
}

App::Frame App::currentFrame() const {
    RECT rc{};
    ::GetClientRect(hwnd_, &rc);
    return layout(Rect{0, 0, rc.right, rc.bottom});
}

}  // namespace app