// theme.h - design tokens for the Windows 11 look.
//
// Everything is drawn by hand with GDI, so the whole visual language lives in
// this one header. Colours are taken from the Windows 11 light palette
// (Mica-like surfaces, subtle 1px borders, 4px/8px radii).
#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>

namespace theme {

struct Color {
    BYTE r = 0, g = 0, b = 0;
    constexpr COLORREF ref() const { return RGB(r, g, b); }
    // Lets the palette constants be passed straight to any GDI call that wants
    // a COLORREF, which keeps the drawing code free of `.ref()` noise. It is
    // constexpr so palette entries can initialise constexpr variables.
    constexpr operator COLORREF() const { return RGB(r, g, b); }
    constexpr Color alphaOver(Color bg, double a) const {
        return Color{(BYTE)(r * a + bg.r * (1 - a)), (BYTE)(g * a + bg.g * (1 - a)),
                     (BYTE)(b * a + bg.b * (1 - a))};
    }
};

// ---------------------------------------------------------------- surfaces --
// Live palette. These follow the Windows theme (light or dark) and the user's
// accent colour, and are swapped by setDarkMode()/refreshSystemTheme().
extern Color WindowBg;        // app canvas (unpainted when Mica is active)
extern Color LayerBg;         // cards / content surface
extern Color CardBg;
extern Color SidebarBg;
extern Color TopBarBg;
extern Color BottomBarBg;

// -------------------------------------------------------------- interaction --
extern Color Accent;          // follows the user's Windows accent
extern Color AccentLight;
extern Color AccentSoft;
extern Color HoverBg;
extern Color PressedBg;
extern Color SelectedBg;
extern Color FocusRing;

// -------------------------------------------------------------------- text --
extern Color TextPrimary;
extern Color TextSecondary;
extern Color TextTertiary;
extern Color TextOnAccent;
extern Color TextDisabled;

// ------------------------------------------------------------------ strokes --
extern Color Border;
extern Color BorderStrong;
extern Color Divider;

// ------------------------------------------------------------------ status --
extern Color Success;
extern Color Warning;
extern Color Danger;
extern Color Info;

// Chart series (legible on both the light and the dark card).
extern Color ChartCpu;
extern Color ChartGpu;
extern Color ChartMem;
extern Color ChartTrack;
extern Color ChartGrid;

// ------------------------------------------------------------------ theme ----
// Follow Windows by default; the user can pin light or dark in the settings.
enum class ThemeMode { System, Light, Dark };

// True when the app is painting with the dark palette.
bool isDarkMode();
// Swaps the palette. Callers are responsible for repainting.
void setDarkMode(bool dark);
ThemeMode themeMode();
// Sets the user's preference and re-applies the palette straight away.
void setThemeMode(ThemeMode mode);
// Maps the stored setting ("system" / "light" / "dark") onto the enum.
ThemeMode modeFromSetting(const std::wstring& value);

// Background of a text field. Rebuilt when the palette changes - a plain EDIT
// control otherwise keeps its white background in the dark theme.
//
// `enabled` matters: a disabled field is painted on the muted surface, and a
// brush that ignores that leaves a bright box sitting inside a grey frame.
COLORREF fieldBack(bool enabled);
HBRUSH fieldBrush(bool enabled = true);

// Repaints a window and every child control. Needed after a theme change: the
// brushes change underneath, but existing controls keep their cached colours
// until they are told to repaint.
void repaintTree(HWND hwnd);

// Height of one line of text in the given font. Used to centre a single-line
// edit vertically: an EDIT pins its text to the top of its box, so the box has
// to be shrunk to exactly one line and moved down.
int lineHeight(HFONT font);
// Re-reads the Windows theme and accent colour. Cheap, so it is fine to call on
// WM_SETTINGCHANGE or when the settings dialog closes.
void refreshSystemTheme();
// The user's accent as Windows reports it, or the Win11 default if unreadable.
COLORREF systemAccent();
// Makes the native caption bar agree with the palette. There is deliberately no
// Mica/Acrylic handling here: this app paints its own surface, and the DWM
// backdrop APIs do not work with a hand-drawn GDI client (see theme.cpp and
// App::paintContent).
void applyCaptionTheme(HWND hwnd);

// A translucent black overlay is how Windows 11 draws button hover states.
inline COLORREF hoverOverlay(COLORREF base) { return base; }
inline constexpr BYTE HoverAlpha = 14;     // ~5.5% black
inline constexpr BYTE PressAlpha = 26;     // ~10% black

COLORREF blend(COLORREF dst, COLORREF src, BYTE alpha);

// ----------------------------------------------------------------- metrics --
struct Metrics {
    // Device pixels per 96 design units, kept as the raw DPI so fractional
    // scalings stay exact: 125% = 120, 150% = 144, 175% = 168.
    //
    // Storing an integer multiplier instead (round(dpi / 96)) is the classic
    // "everything is slightly too big" bug: 175% would round to 2 and every
    // metric - and the window itself - would come out 14% oversized.
    int dpi = 96;

    int radiusSmall = 4;
    int radiusMedium = 6;
    int radiusLarge = 8;
    int strokeWidth = 1;

    int topBarHeight = 48;
    int bottomBarHeight = 30;
    int sidebarWidth = 260;

    int rowHeight = 34;
    int itemHeight = 32;
    int padding = 12;
    int gapSmall = 6;
    int gap = 10;
    int gapLarge = 16;

    int fontBody = 14;
    int fontSmall = 12;
    int fontCaption = 12;
    int fontSubtitle = 16;
    int fontTitle = 20;
    int fontHeading = 24;
    int fontMono = 13;

    // Design units -> device pixels at the current DPI.
    int px(int value) const { return ::MulDiv(value, dpi, 96); }
    // Same, as a floating point factor (used by the embedded browser).
    double scale() const { return dpi / 96.0; }
};

extern Metrics M;

void init(HWND hwnd);                                   // reads DPI, builds fonts
void onDpiChanged(UINT dpi);

HFONT fontBody();
HFONT fontBodyBold();
HFONT fontSmall();
HFONT fontCaption();
HFONT fontSubtitle();
HFONT fontSubtitleBold();
HFONT fontTitle();
HFONT fontHeading();
HFONT fontMono();

// Fonts for a one-off size (cached, do not delete).
HFONT fontAt(int pt, bool bold = false);

void shutdown();

}  // namespace theme