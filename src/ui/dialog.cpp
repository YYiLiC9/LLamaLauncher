#include "ui/dialog.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>

#include "core/util.h"

namespace ui {

namespace {

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

// Dialogs are modal by running their own message pump and disabling the owner.
// That keeps start/dialog/end code linear at the call site.
struct ModalContext {
    Dialog* dialog = nullptr;
    HWND owner = nullptr;
    bool* done = nullptr;
};

}  // namespace

const wchar_t* Dialog::className() { return L"LlamaLauncherDialog"; }

Dialog::~Dialog() {
    if (hwnd_) ::DestroyWindow(hwnd_);
}

bool Dialog::run(HWND owner, const std::wstring& title, int width, int height, bool resizable) {
    owner_ = owner;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc = &Dialog::WndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = className();
        if (!::RegisterClassExW(&wc)) return false;
        registered = true;
    }

    // WS_CLIPCHILDREN is essential: the whole client area is redrawn by hand, and
// without it every repaint would paint straight over the child EDIT controls
// (the dialog would look like the text boxes were empty until they were
// focused).
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    if (resizable) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;

    // Account for the border and caption so the client area matches the design.
    RECT want{0, 0, width, height};
    ::AdjustWindowRectEx(&want, style, FALSE, 0);
    int winW = want.right - want.left;
    int winH = want.bottom - want.top;

    // Centre over the owner (or the screen when there is none).
    RECT anchor{};
    if (owner && ::IsWindow(owner)) {
        ::GetWindowRect(owner, &anchor);
    } else {
        anchor = RECT{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    }
    int x = anchor.left + ((anchor.right - anchor.left) - winW) / 2;
    int y = anchor.top + ((anchor.bottom - anchor.top) - winH) / 2;

    hwnd_ = ::CreateWindowExW(WS_EX_DLGMODALFRAME, className(), title.c_str(), style, x, y, winW,
                              winH, owner, nullptr, ::GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;

    // `client_` must be known before onLayout() runs: subclasses place their
    // child edit controls from clientRect(), and this is the only chance to get
    // it right - WM_PAINT has not happened yet, and until now client_ was still
    // a zero rect, which put every control into a degenerate position.
    RECT rc{};
    ::GetClientRect(hwnd_, &rc);
    client_ = Rect{0, 0, rc.right, rc.bottom};

    int corner = DWMWCP_ROUND;
    ::DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    if (owner) ::EnableWindow(owner, FALSE);
    onLayout();
    ::ShowWindow(hwnd_, SW_SHOW);
    ::UpdateWindow(hwnd_);

    // Applied *after* the window is visible, for the same reason the main window
    // does it there: DWM silently ignores frame attributes sent to a hidden
    // window, which is what left every dialog with a light caption in the dark
    // theme even though the flag had been set.
    theme::applyCaptionTheme(hwnd_);

    // Modal loop: pump until the dialog closes. The DPI change and paint
    // messages for the owner window still need to be processed while the
    // dialog is up.
    // GetMessageW returns -1 on error (which is truthy): a plain `while`
    // would never notice and spin forever, so the loop tests > 0.
    MSG msg{};
    while (::IsWindow(hwnd_)) {
        BOOL got = ::GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;
        if (msg.hwnd == hwnd_ || ::IsChild(hwnd_, msg.hwnd)) {
            if (!::IsDialogMessageW(hwnd_, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            continue;
        }
        // Keystrokes aimed at a child edit control of the dialog.
        if (msg.hwnd && ::GetParent(msg.hwnd) == hwnd_) {
            if (!::IsDialogMessageW(hwnd_, &msg)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            continue;
        }
        // Everything else belongs to the main window; forward only the ones the
        // main window must not act on while a modal is open.
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
            continue;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    if (owner) {
        ::EnableWindow(owner, TRUE);
        ::SetActiveWindow(owner);
    }
    if (hwnd_) {
        HWND h = hwnd_;
        hwnd_ = nullptr;
        ::DestroyWindow(h);
    }
    return result_ == DialogResult::Ok || result_ == DialogResult::Custom1 ||
           result_ == DialogResult::Custom2;
}

LRESULT CALLBACK Dialog::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Dialog* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = (CREATESTRUCTW*)lp;
        self = (Dialog*)cs->lpCreateParams;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
        if (self) self->hwnd_ = hwnd;
    } else {
        self = (Dialog*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (!self) return ::DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = ::BeginPaint(hwnd, &ps);
            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            self->client_ = Rect{0, 0, rc.right, rc.bottom};

            HDC mem = ::CreateCompatibleDC(dc);
            HBITMAP bmp = ::CreateCompatibleBitmap(dc, (int)std::max<LONG>(1, rc.right),
                                                   (int)std::max<LONG>(1, rc.bottom));
            HGDIOBJ old = ::SelectObject(mem, bmp);
            HBRUSH bg = ::CreateSolidBrush(theme::LayerBg);
            ::FillRect(mem, &rc, bg);
            ::DeleteObject(bg);

            Canvas c(mem);
            self->hits_.clear();
            self->onPaint(c, self->client_);

            ::BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            ::SelectObject(mem, old);
            ::DeleteObject(bmp);
            ::DeleteDC(mem);
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;

        case WM_COMMAND:
            self->onCommand((int)LOWORD(wp), (int)HIWORD(wp), (HWND)lp);
            return 0;

        // Child EDIT controls paint their own background, and the stock colour
        // is white whatever the palette says. Handing back a theme brush and
        // colours is the only way to stop them glaring in the dark theme.
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HWND ctl = (HWND)lp;
            HDC dc = (HDC)wp;
            bool enabled = !ctl || ::IsWindowEnabled(ctl);
            ::SetBkMode(dc, OPAQUE);
            ::SetTextColor(dc, enabled ? theme::TextPrimary : theme::TextDisabled);
            ::SetBkColor(dc, theme::fieldBack(enabled));
            return (LRESULT)theme::fieldBrush(enabled);
        }

        case WM_SIZE: {
            // Keep client_ current and let subclasses reflow their children, so
            // a resizable dialog keeps its controls inside the frame.
            int cw = GET_X_LPARAM(lp), ch = GET_Y_LPARAM(lp);
            if (cw <= 0 || ch <= 0) return 0;   // minimised
            self->client_ = Rect{0, 0, cw, ch};
            self->onLayout();
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (!self->tracking_) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                ::TrackMouseEvent(&tme);
                self->tracking_ = true;
            }
            self->onMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
            return 0;

        case WM_MOUSELEAVE:
            self->tracking_ = false;
            self->hoverId_ = -1;
            self->onMouseLeave();
            self->invalidate();
            return 0;

        case WM_LBUTTONDOWN:
            self->pressId_ = self->hitAt((short)LOWORD(lp), (short)HIWORD(lp));
            self->onLButtonDown((short)LOWORD(lp), (short)HIWORD(lp));
            self->invalidate();
            return 0;

        case WM_LBUTTONUP: {
            int up = self->hitAt((short)LOWORD(lp), (short)HIWORD(lp));
            int down = self->pressId_;
            self->pressId_ = -1;
            self->onLButtonUp((short)LOWORD(lp), (short)HIWORD(lp));
            // A click is delivered as a press and release pair, so the dialog
            // only needs to compare ids and act on a match.
            if (up >= 0 && up == down) self->onClick(up);
            self->invalidate();
            return 0;
        }

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
                if (self->hitAt(pt.x, pt.y) >= 0) {
                    ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
            }
            break;

        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                self->close(DialogResult::Cancel);
                return 0;
            }
            if (self->onKeyDown(wp)) return 0;
            break;

        case WM_DPICHANGED:
            // Reposition the child editors and hand them the rebuilt fonts.
            // Without this a dialog dragged to another monitor kept its old
            // layout and the stale HFONT handles its EDITs were created with.
            self->onLayout();
            refontTrackedEdits(hwnd);
            ::InvalidateRect(hwnd, nullptr, TRUE);
            return 0;

        case WM_CLOSE:
            self->close(DialogResult::Cancel);
            return 0;

        case WM_DESTROY:
            self->onDestroy();
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// ---- child EDIT font registry (see dialog.h) ----
namespace {

struct FontBind {
    HWND hwnd;
    bool mono;
};
std::vector<FontBind>& fontBinds() {
    static std::vector<FontBind> binds;
    return binds;
}

}  // namespace

void trackEditFont(HWND edit, bool mono) {
    fontBinds().push_back({edit, mono});
    // Prune entries whose dialog is long gone; the vector is tiny either way.
    auto& binds = fontBinds();
    binds.erase(std::remove_if(binds.begin(), binds.end(),
                               [](const FontBind& b) { return !::IsWindow(b.hwnd); }),
                binds.end());
}

void refontTrackedEdits(HWND dialog) {
    for (const FontBind& b : fontBinds()) {
        if (!::IsWindow(b.hwnd) || !::IsChild(dialog, b.hwnd)) continue;
        HFONT f = b.mono ? theme::fontMono() : theme::fontBody();
        ::SendMessageW(b.hwnd, WM_SETFONT, (WPARAM)f, TRUE);
    }
}

void Dialog::repaintForTheme() {    // The caption is DWM-drawn, so a plain redraw leaves it light while the
    // client goes dark. Re-send the immersive-dark flag for this window as well
    // as for the one behind it.
    theme::applyCaptionTheme(hwnd_);
    theme::repaintTree(hwnd_);
    // The window behind is disabled while a modal is up, but it still has to
    // follow the theme - otherwise it only catches up when the dialog closes.
    if (owner_) {
        theme::applyCaptionTheme(owner_);
        theme::repaintTree(owner_);
    }
}

void Dialog::addHit(const Rect& r, int id, bool enabled) {
    if (!r.valid()) return;
    hits_.push_back(HitEntry{r, id, enabled});
}

int Dialog::hitAt(int x, int y) const {
    for (int i = (int)hits_.size() - 1; i >= 0; --i) {
        const HitEntry& h = hits_[(size_t)i];
        if (h.enabled && h.rect.contains(x, y)) return h.id;
    }
    return -1;
}

void Dialog::invalidate() {
    if (hwnd_) ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void Dialog::close(DialogResult r) {
    if (closing_) return;
    closing_ = true;
    result_ = r;
    if (hwnd_) {
        HWND h = hwnd_;
        hwnd_ = nullptr;          // detach first so re-entrant calls are no-ops
        ::DestroyWindow(h);
    }
}

}  // namespace ui