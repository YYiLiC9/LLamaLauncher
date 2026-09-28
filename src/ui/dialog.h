// ui/dialog.h - a tiny framework for the custom-drawn modal windows.
//
// Every dialog in the app is a plain WS_POPUP window painted with the same
// widget kit as the main window, which is what keeps the visual language
// consistent and avoids the dated look of stock Win32 dialogs.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "ui/shell.h"
#include "ui/theme.h"

namespace ui {

using shell::Canvas;
using shell::Rect;

enum class DialogResult { None, Ok, Cancel, Custom1, Custom2 };

// Child-EDIT font registry. theme::onDpiChanged rebuilds its font handles, so
// every EDIT created through makeEdit() is tracked here and re-sent the
// current-scaled font when the dialog handling WM_DPICHANGED asks for it.
void trackEditFont(HWND edit, bool mono);
// Re-sends the right font to the tracked EDITs that live under `dialog`.
void refontTrackedEdits(HWND dialog);

class Dialog {
public:
    virtual ~Dialog();

    // Creates the window, centres it over `owner`, and runs the modal loop.
    bool run(HWND owner, const std::wstring& title, int width, int height,
             bool resizable = false);

    HWND hwnd() const { return hwnd_; }
    const Rect& clientRect() const { return client_; }

protected:
    // ---- to implement ----
    // Called with the frame's client rectangle; draw everything here.
    virtual void onPaint(Canvas& c, const Rect& client) = 0;
    // Mouse events, already translated to client coordinates. Return true when
    // the dialog should repaint, and call close() to dismiss.
    virtual void onMouseMove(int x, int y) { (void)x; (void)y; }
    virtual void onLButtonDown(int x, int y) { (void)x; (void)y; }
    virtual void onLButtonUp(int x, int y) { (void)x; (void)y; }
    virtual void onMouseWheel(int delta, int x, int y) {
        (void)delta; (void)x; (void)y;
    }
    // The pointer left the dialog (TrackMouseEvent delivers WM_MOUSELEAVE).
    virtual void onMouseLeave() {}
    virtual bool onKeyDown(WPARAM key) { (void)key; return false; }
    // Fired once, after a press-and-release on the same region. This is where
    // click behaviour belongs; the mouse handlers above are for tracking state.
    virtual void onClick(int id) { (void)id; }
    // Forwarded from child controls (an EDIT sends EN_CHANGE on every
    // keystroke), which is what drives the live command preview.
    virtual void onCommand(int id, int code, HWND ctl) { (void)id; (void)code; (void)ctl; }
    // Called after the window exists and again on DPI change, so child controls
    // can be created and positioned.
    virtual void onLayout() {}
    virtual void onDestroy() {}
    // Forwarded from WM_TIMER with the timer id. The base class repaints on
    // every tick; override to stop a timer that has done its job.
    virtual void onTimer(WPARAM id) { (void)id; }

    // ---- services ----
    void addHit(const Rect& r, int id, bool enabled = true);

    // Repaints this dialog, its child controls and its owner. Call after a
    // palette change: the theme brushes change underneath, but a control keeps
    // the colours it was last painted with until it is told to redraw.
    void repaintForTheme();
    // id of the topmost hit region under the point, or -1.
    int hitAt(int x, int y) const;
    void invalidate();
    void close(DialogResult result);

    // Mouse state, kept here so every dialog gets hover and press for free.
    int hoverId_ = -1;
    int pressId_ = -1;
    bool isHovered(int id) const { return id >= 0 && id == hoverId_; }
    bool isPressed(int id) const { return id >= 0 && id == pressId_; }

    // Determines which ids the mouse handler should report. Subclasses set this
    // so shared code knows what is interactive.
    std::vector<std::pair<Rect, int>> hitRegions_;

    DialogResult result_ = DialogResult::None;
    HWND owner_ = nullptr;
    bool closing_ = false;

private:
    struct HitEntry {
        Rect rect;
        int id;
        bool enabled;
    };
    std::vector<HitEntry> hits_;

    HWND hwnd_ = nullptr;
    Rect client_{};
    bool tracking_ = false;

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static const wchar_t* className();
};

}  // namespace ui