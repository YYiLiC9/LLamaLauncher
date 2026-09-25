// shell.h - the hand-drawn widget kit.
//
// The whole interface is drawn into a single double-buffered window using GDI.
// That keeps the binary tiny, keeps idle CPU at zero, and makes the Windows 11
// look (rounded corners, subtle borders, translucent hover states) achievable
// without shipping a UI toolkit.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "ui/theme.h"

namespace shell {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    int left() const { return x; }
    int top() const { return y; }
    int right() const { return x + w; }
    int bottom() const { return y + h; }
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool valid() const { return w > 0 && h > 0; }
    RECT toWin() const { return RECT{x, y, x + w, y + h}; }
    Rect inset(int d) const { return Rect{x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect inset(int dx, int dy) const { return Rect{x + dx, y + dy, w - 2 * dx, h - 2 * dy}; }
    Rect offset(int dx, int dy) const { return Rect{x + dx, y + dy, w, h}; }
    Rect sub(int dy, int hh) const { return Rect{x, y + dy, w, hh}; }
    // A horizontal slice starting at dx with the given width.
    Rect slice(int dx, int ww) const { return Rect{x + dx, y, ww, h}; }
    Rect takeTop(int hh) const { return Rect{x, y, w, hh}; }
    Rect below(int hh) const { return Rect{x, y + hh, w, h - hh}; }
};

// Layout helper: a vertical flow cursor.
struct Layout {
    Rect area;
    int cursor = 0;
    int gap = 0;
    explicit Layout(Rect r, int gapPx = 0) : area(r), cursor(r.y), gap(gapPx) {}
    Rect row(int height) {
        Rect r{area.x, cursor, area.w, height};
        cursor += height + gap;
        return r;
    }
    Rect rowEx(int height, int indent, int width) {
        Rect r{area.x + indent, cursor, width > 0 ? width : area.w - indent, height};
        cursor += height + gap;
        return r;
    }
    void skip(int px) { cursor += px; }
    Rect remainder() const { return Rect{area.x, cursor, area.w, area.bottom() - cursor}; }
    int usedHeight() const { return cursor - area.y; }
};

// ------------------------------------------------------------------ painter --
class Canvas {
public:
    explicit Canvas(HDC dc) : dc_(dc) {}

    HDC dc() const { return dc_; }

    void fill(const Rect& r, COLORREF color);
    void fillRound(const Rect& r, int radius, COLORREF color);
    void stroke(const Rect& r, COLORREF color, int width = 1);
    void strokeRound(const Rect& r, int radius, COLORREF color, int width = 1);
    void line(int x1, int y1, int x2, int y2, COLORREF color, int width = 1);

    // Vertical or horizontal two-stop gradient, used for the accent button.
    void gradient(const Rect& r, COLORREF top, COLORREF bottom, bool vertical = true);

    // ---- text ----
    void text(const Rect& r, const std::wstring& s, COLORREF color, HFONT font,
              UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    int  textWidth(const std::wstring& s, HFONT font) const;
    void textBlock(const Rect& r, const std::wstring& s, COLORREF color, HFONT font,
                   UINT format = DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    // ---- shapes ----
    void circle(int cx, int cy, int radius, COLORREF color);
    void circleOutline(int cx, int cy, int radius, COLORREF color, int width = 1);
    // Donut / progress ring. `sweep` is a fraction of a full turn (0..1).
    void ring(int cx, int cy, int radius, int thickness, double fraction, COLORREF color,
              COLORREF track);
    // Rounded bar used by the resource meters.
    void meter(const Rect& r, double fraction, COLORREF fillColor, COLORREF track);
    // Sparkline area chart from a series of samples in 0..100.
    void sparkline(const Rect& r, const std::vector<float>& samples, COLORREF lineColor,
                   COLORREF fillColor, double maxValue = 100.0);

    // A monochrome glyph from the Segoe Fluent Icons / Segoe MDL2 font, which
    // is how the toolbar icons are drawn without any image assets.
    void glyph(const Rect& r, wchar_t code, COLORREF color, int sizePt);

    // Applies a translucent black overlay, the way Windows 11 tints hover.
    void overlay(const Rect& r, int radius, BYTE alpha);

private:
    HDC dc_;
};

// ------------------------------------------------------------------- icons --
// Codepoints from the Segoe Fluent Icons font that ships with Windows 11.
namespace glyphs {
constexpr wchar_t kSettings = L'\xE713';
constexpr wchar_t kImport   = L'\xE8B5';   // Download-ish arrow into tray
constexpr wchar_t kExport   = L'\xEDE1';   // Upload-ish arrow
// Two arrows swapping places: the merged import/export toolbar entry.
constexpr wchar_t kTransfer = L'\xE8AB';   // Switch / exchange
constexpr wchar_t kHelp     = L'\xE897';
constexpr wchar_t kChat     = L'\xE8BD';   // Chat bubbles
constexpr wchar_t kAdd      = L'\xE710';
constexpr wchar_t kPlay     = L'\xE768';
constexpr wchar_t kEdit     = L'\xE70F';
constexpr wchar_t kDelete   = L'\xE74D';
constexpr wchar_t kSearch   = L'\xE721';
constexpr wchar_t kFolder   = L'\xE8B7';
constexpr wchar_t kFile     = L'\xE7C3';
constexpr wchar_t kStop     = L'\xE71A';
constexpr wchar_t kRefresh  = L'\xE72C';
constexpr wchar_t kBack     = L'\xE72B';
constexpr wchar_t kGlobe    = L'\xE774';
constexpr wchar_t kInfo     = L'\xE946';
constexpr wchar_t kCheck    = L'\xE73E';
constexpr wchar_t kCopy     = L'\xE8C8';
constexpr wchar_t kLog      = L'\xE9D9';
constexpr wchar_t kChip     = L'\xE950';
constexpr wchar_t kMemoryStick = L'\xEEA0';
constexpr wchar_t kGauge    = L'\xE9D2';
}  // namespace glyphs

// Cached icon font for the Segoe Fluent Icons / MDL2 Assets codepoints above.
HFONT shellGlyphFont(int sizePt);

// ------------------------------------------------------------------ buttons --
enum class ButtonStyle { Primary, Secondary, Subtle, Danger };

// Draws a button and reports whether it is hovered/pressed so the caller can
// decide what to do on click. `focused` draws the keyboard focus ring.
// `enabled` false renders the muted, non-interactive form. A button that looks
// live while the row it belongs to is switched off is exactly the kind of thing
// that makes a form feel broken, so there is an explicit state for it.
void button(Canvas& c, const Rect& r, const std::wstring& label, ButtonStyle style, bool hovered,
            bool pressed, bool focused = false, wchar_t leadingGlyph = 0,
            wchar_t trailingGlyph = 0, bool enabled = true);
// Compact icon-only button used in the top bar.
void iconButton(Canvas& c, const Rect& r, wchar_t glyph, bool hovered, bool pressed,
                bool selected = false);
// Toolbar button with an icon above a caption.
void toolbarButton(Canvas& c, const Rect& r, wchar_t glyph, const std::wstring& caption,
                   bool hovered, bool pressed, bool selected = false);

// ------------------------------------------------------------------- cards --
// `elevated` adds a two-step soft shadow under the card. GDI has no cheap blur,
// so the shadow is suggested with progressively darker rounds offset downward -
// enough to lift a card off the canvas without looking heavy.
void card(Canvas& c, const Rect& r, COLORREF bg = theme::CardBg, COLORREF border = theme::Border,
          bool elevated = false);
void sectionTitle(Canvas& c, const Rect& r, const std::wstring& text);
void divider(Canvas& c, int x1, int y, int x2, COLORREF color = theme::Divider);
void chip(Canvas& c, const Rect& r, const std::wstring& text, COLORREF bg, COLORREF fg);
void badge(Canvas& c, const Rect& r, const std::wstring& text, COLORREF bg, COLORREF fg);

// A labelled metric tile with a ring gauge, used by the running view.
void metricTile(Canvas& c, const Rect& r, const std::wstring& label, const std::wstring& primary,
                const std::wstring& secondary, double fraction, COLORREF accent, wchar_t glyph);

// A horizontal progress row: icon, label, value, bar.
void meterRow(Canvas& c, const Rect& r, const std::wstring& label, const std::wstring& value,
              double fraction, COLORREF accent, wchar_t glyph);

}  // namespace shell