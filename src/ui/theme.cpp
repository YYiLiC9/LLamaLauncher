#include "ui/theme.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

#include "core/util.h"

namespace theme {

Metrics M;

// ----------------------------------------------------------------- palette --
Color WindowBg, LayerBg, CardBg, SidebarBg, TopBarBg, BottomBarBg;
Color Accent, AccentLight, AccentSoft, HoverBg, PressedBg, SelectedBg, FocusRing;
Color TextPrimary, TextSecondary, TextTertiary, TextOnAccent, TextDisabled;
Color Border, BorderStrong, Divider;
Color Success, Warning, Danger, Info;
Color ChartCpu, ChartGpu, ChartMem, ChartTrack, ChartGrid, ChartOther;

namespace {

bool g_dark = false;
ThemeMode g_mode = ThemeMode::System;

BYTE clamp8(int v) { return (BYTE)std::clamp(v, 0, 255); }

// Lifts or lowers a colour towards white/black by `amount` (0..255).
Color shift(Color c, int amount) {
    return Color{clamp8(c.r + amount), clamp8(c.g + amount), clamp8(c.b + amount)};
}

// A soft, low-saturation version of the accent, used for tinted tiles.
Color soften(Color accent, Color bg, double mix) {
    return Color{clamp8((int)(accent.r * mix + bg.r * (1 - mix))),
                 clamp8((int)(accent.g * mix + bg.g * (1 - mix))),
                 clamp8((int)(accent.b * mix + bg.b * (1 - mix)))};
}

double luma(Color c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

void applyPalette() {
    if (g_dark) {
        // Windows 11 dark: near-black canvas, cards one step lighter, and text
        // that stays legible against both.
        WindowBg = Color{32, 32, 32};
        LayerBg = Color{44, 44, 44};
        CardBg = Color{42, 42, 42};
        SidebarBg = Color{32, 32, 32};
        TopBarBg = Color{42, 42, 42};
        BottomBarBg = Color{32, 32, 32};

        TextPrimary = Color{255, 255, 255};
        TextSecondary = Color{220, 220, 220};
        TextTertiary = Color{158, 158, 158};
        TextOnAccent = Color{255, 255, 255};
        TextDisabled = Color{110, 110, 110};

        Border = Color{62, 62, 62};
        BorderStrong = Color{84, 84, 84};
        Divider = Color{55, 55, 55};

        Success = Color{108, 203, 95};
        Warning = Color{247, 214, 0};
        Danger = Color{255, 153, 164};

        SelectedBg = soften(Accent, WindowBg, 0.30);
        AccentSoft = soften(Accent, WindowBg, 0.22);
        ChartTrack = Color{62, 62, 62};
        ChartGrid = Color{55, 55, 55};
        // Slate grey: clearly lighter than the track, clearly not the purple
        // or the green family.
        ChartOther = Color{124, 124, 138};
    } else {
        WindowBg = Color{243, 243, 243};
        LayerBg = Color{252, 252, 252};
        CardBg = Color{255, 255, 255};
        SidebarBg = Color{243, 243, 243};
        TopBarBg = Color{252, 252, 252};
        BottomBarBg = Color{248, 248, 248};

        TextPrimary = Color{26, 26, 26};
        TextSecondary = Color{95, 99, 104};
        TextTertiary = Color{133, 138, 143};
        TextOnAccent = Color{255, 255, 255};
        TextDisabled = Color{160, 165, 170};

        Border = Color{229, 229, 229};
        BorderStrong = Color{209, 209, 209};
        Divider = Color{235, 235, 235};

        Success = Color{16, 124, 16};
        Warning = Color{157, 93, 0};
        Danger = Color{196, 43, 28};

        SelectedBg = soften(Accent, CardBg, 0.16);
        AccentSoft = soften(Accent, CardBg, 0.14);
        ChartTrack = Color{232, 232, 232};
        ChartGrid = Color{240, 240, 240};
        // The mirror of the dark value: noticeably darker than the track.
        ChartOther = Color{150, 150, 164};
    }

    HoverBg = Color{0, 0, 0};
    PressedBg = Color{0, 0, 0};
    FocusRing = Accent;
    // The accent is often too light to carry white text on the light theme; on
    // the dark theme it is usually too dark for black, so pick by luminance.
    TextOnAccent = luma(Accent) > 140 ? Color{0, 0, 0} : Color{255, 255, 255};

    ChartCpu = Accent;
    ChartGpu = Color{113, 76, 168};
    ChartMem = Color{16, 124, 16};
    Info = Accent;

}

}  // namespace

bool isDarkMode() { return g_dark; }

void setDarkMode(bool dark) {
    if (g_dark == dark) return;
    g_dark = dark;
    applyPalette();
}

ThemeMode themeMode() { return g_mode; }

void setThemeMode(ThemeMode mode) {
    g_mode = mode;
    // Re-resolve against the new preference (a pinned theme ignores Windows,
    // System re-reads it) and rebuild the palette.
    refreshSystemTheme();
}

// Maps the stored setting string back onto the enum.
ThemeMode modeFromSetting(const std::wstring& value) {
    if (util::iequals(value, L"light")) return ThemeMode::Light;
    if (util::iequals(value, L"dark")) return ThemeMode::Dark;
    return ThemeMode::System;
}

COLORREF systemAccent() {
    // The true accent lives in DWM's settings; the value is 0xAARRGGBB.
    DWORD raw = 0;
    DWORD size = sizeof(raw);
    if (::RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
                       L"AccentColor", RRF_RT_REG_DWORD, nullptr, &raw, &size) == ERROR_SUCCESS) {
        // Registry stores it as 0xAABBGGRR.
        return RGB((raw >> 0) & 0xFF, (raw >> 8) & 0xFF, (raw >> 16) & 0xFF);
    }
    DWORD color = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(::DwmGetColorizationColor(&color, &opaque))) {
        return RGB((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
    }
    return RGB(0, 95, 184);   // Windows 11 default
}

void refreshSystemTheme() {
    // What Windows asks for is only consulted when the user has not pinned a
    // theme of their own.
    bool systemDark = false;
    DWORD light = 1;
    DWORD size = sizeof(light);
    if (::RegGetValueW(HKEY_CURRENT_USER,
                       L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light,
                       &size) == ERROR_SUCCESS) {
        systemDark = (light == 0);
    }

    switch (g_mode) {
        case ThemeMode::Light:
            g_dark = false;
            break;
        case ThemeMode::Dark:
            g_dark = true;
            break;
        case ThemeMode::System:
        default:
            g_dark = systemDark;
            break;
    }

    COLORREF accent = systemAccent();
    Accent = Color{GetRValue(accent), GetGValue(accent), GetBValue(accent)};
    AccentLight = shift(Accent, g_dark ? 24 : -12);
    applyPalette();
}

int lineHeight(HFONT font) {
    if (!font) return 0;
    HDC dc = ::GetDC(nullptr);
    HGDIOBJ old = ::SelectObject(dc, font);
    TEXTMETRICW tm{};
    ::GetTextMetricsW(dc, &tm);
    ::SelectObject(dc, old);
    ::ReleaseDC(nullptr, dc);
    return (int)tm.tmHeight;
}

COLORREF fieldBack(bool enabled) {
    if (enabled) return CardBg;
    // A switched-off field has to read as switched off at a glance. Pointing it
    // at the layer colour made a disabled box all but identical to an editable
    // one (44 vs 42 in the dark theme), which is exactly the confusion the row
    // switch exists to remove.
    return g_dark ? blend(CardBg, RGB(0, 0, 0), 26) : blend(CardBg, RGB(0, 0, 0), 9);
}

HBRUSH fieldBrush(bool enabled) {
    // Rebuilt whenever the palette flips, so a field always matches the surface
    // it sits on. (A stock white brush is what made the dark theme show white
    // input boxes.) Two brushes, because a disabled field sits on the muted
    // surface - which is the grey frame the dialog paints behind it.
    static HBRUSH on = nullptr;
    static HBRUSH off = nullptr;
    static bool builtForDark = false;
    static bool everBuilt = false;
    if (!everBuilt || builtForDark != g_dark) {
        if (on) ::DeleteObject(on);
        if (off) ::DeleteObject(off);
        on = ::CreateSolidBrush(fieldBack(true));
        off = ::CreateSolidBrush(fieldBack(false));
        builtForDark = g_dark;
        everBuilt = true;
    }
    return enabled ? on : off;
}

void repaintTree(HWND hwnd) {
    if (!hwnd) return;
    // RDW_ALLCHILDREN is the important flag: without it the EDIT controls keep
    // the colours they were last painted with, which is what made the theme
    // change look half-applied.
    ::RedrawWindow(hwnd, nullptr, nullptr,
                   RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void applyCaptionTheme(HWND hwnd) {
    if (!hwnd) return;
    // Only the caption flag: it has to agree with the palette, otherwise the
    // title bar stays light while the client goes dark.
    //
    // No Mica/Acrylic is requested. Two attempts were made and both failed:
    //   * DWMWA_SYSTEMBACKDROP_TYPE alone leaves alpha-0 pixels showing whatever
    //     window is behind, because this app composites its client with
    //     per-pixel alpha rather than leaving it empty for the DWM.
    //   * DwmExtendFrameIntoClientArea(-1) turns the client into a *snapshot* of
    //     the desktop - blank on first paint, never refreshing while the window
    //     moves, and only rebuilt on a minimise/restore.
    // The app paints its own surface instead (see App::paintContent).
    BOOL dark = g_dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

    // The immersive flag alone is not enough for every window: a dialog that was
    // created before the palette flipped, and shown straight afterwards, can
    // keep a light caption. Setting the colours explicitly makes the caption
    // deterministic for both the main window and every dialog.
    COLORREF caption = RGB(LayerBg.r, LayerBg.g, LayerBg.b);
    ::DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));
    COLORREF text = RGB(TextPrimary.r, TextPrimary.g, TextPrimary.b);
    ::DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof(text));
}

static HFONT g_body = nullptr;
static HFONT g_bodyBold = nullptr;
static HFONT g_small = nullptr;
static HFONT g_caption = nullptr;
static HFONT g_subtitle = nullptr;
static HFONT g_subtitleBold = nullptr;
static HFONT g_title = nullptr;
static HFONT g_heading = nullptr;
static HFONT g_mono = nullptr;

static std::unordered_map<int, HFONT> g_cache;

namespace {
std::wstring uiFace() {
    // Segoe UI Variable is the Windows 11 system face; fall back gracefully on
    // older builds. The UI font keeps Chinese glyphs sharp automatically.
    return L"Segoe UI Variable Text";
}
std::wstring monoFace() { return L"Cascadia Mono"; }
}  // namespace

COLORREF blend(COLORREF dst, COLORREF src, BYTE alpha) {
    // Both scaled by 255 so alpha is applied at 8-bit precision.
    BYTE dr = GetRValue(dst), dg = GetGValue(dst), db = GetBValue(dst);
    BYTE sr = GetRValue(src), sg = GetGValue(src), sb = GetBValue(src);
    BYTE r = (BYTE)((dr * (255 - alpha) + sr * alpha) / 255);
    BYTE g = (BYTE)((dg * (255 - alpha) + sg * alpha) / 255);
    BYTE b = (BYTE)((db * (255 - alpha) + sb * alpha) / 255);
    return RGB(r, g, b);
}

static HFONT makeFont(int pt, bool bold, const std::wstring& face) {
    LOGFONTW lf{};
    // `pt` is a design-unit size at 96 dpi; scale it to the real DPI exactly.
    lf.lfHeight = -::MulDiv(pt, M.dpi, 96);
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DONTCARE;
    wcsncpy_s(lf.lfFaceName, face.c_str(), _TRUNCATE);
    return ::CreateFontIndirectW(&lf);
}

// All the layout constants are expressed in 96-dpi design units and expanded
// once, here, so the rest of the UI never has to think about DPI.
static void applyMetrics() {
    // Two tiers only, matching Windows 11:
    //   4 - interactive controls (buttons, inputs, checkboxes, list items)
    //   8 - surfaces (cards, panels, dialogs)
    // radiusMedium is deliberately equal to radiusSmall so that a button and a
    // text box next to each other have the same curve.
    M.radiusSmall = M.px(4);
    M.radiusMedium = M.px(4);
    M.radiusLarge = M.px(8);
    M.strokeWidth = M.px(1) > 0 ? M.px(1) : 1;
    M.topBarHeight = M.px(48);
    M.bottomBarHeight = M.px(30);
    M.sidebarWidth = M.px(264);
    M.rowHeight = M.px(34);
    M.itemHeight = M.px(32);
    M.padding = M.px(12);
    M.gapSmall = M.px(6);
    M.gap = M.px(10);
    M.gapLarge = M.px(16);
}

void init(HWND hwnd) {
    UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : ::GetDpiForSystem();
    if (dpi < 72) dpi = 96;
    M.dpi = (int)dpi;
    applyMetrics();

    // Pull the palette in from Windows: theme (light/dark) plus accent colour.
    refreshSystemTheme();

    shutdown();

    g_body = makeFont(M.fontBody, false, uiFace());
    g_bodyBold = makeFont(M.fontBody, true, uiFace());
    g_small = makeFont(M.fontSmall, false, uiFace());
    g_caption = makeFont(M.fontCaption, false, uiFace());
    g_subtitle = makeFont(M.fontSubtitle, false, uiFace());
    g_subtitleBold = makeFont(M.fontSubtitle, true, uiFace());
    g_title = makeFont(M.fontTitle, true, uiFace());
    g_heading = makeFont(M.fontHeading, true, uiFace());
    g_mono = makeFont(M.fontMono, false, monoFace());
}

void onDpiChanged(UINT dpi) {
    if (dpi < 72) dpi = 96;
    // Keep the DPI the caller measured: init(nullptr) would silently override
    // it with GetDpiForSystem, which put a secondary monitor back on the primary
    // monitor's scale.
    M.dpi = (int)dpi;
    applyMetrics();

    for (auto& [_, f] : g_cache) ::DeleteObject(f);
    g_cache.clear();
    // Deliberately NOT deleting the nine shared fonts: child EDIT controls
    // (search box, parameter editors) still hold the old handles, and using a
    // deleted HFONT is undefined behaviour. The orphaned fonts are a few
    // hundred bytes each and DPI changes are rare; they die with the process.
    g_body = makeFont(M.fontBody, false, uiFace());
    g_bodyBold = makeFont(M.fontBody, true, uiFace());
    g_small = makeFont(M.fontSmall, false, uiFace());
    g_caption = makeFont(M.fontCaption, false, uiFace());
    g_subtitle = makeFont(M.fontSubtitle, false, uiFace());
    g_subtitleBold = makeFont(M.fontSubtitle, true, uiFace());
    g_title = makeFont(M.fontTitle, true, uiFace());
    g_heading = makeFont(M.fontHeading, true, uiFace());
    g_mono = makeFont(M.fontMono, false, monoFace());
    refreshSystemTheme();
}

HFONT fontBody() { return g_body; }
HFONT fontBodyBold() { return g_bodyBold; }
HFONT fontSmall() { return g_small; }
HFONT fontCaption() { return g_caption; }
HFONT fontSubtitle() { return g_subtitle; }
HFONT fontSubtitleBold() { return g_subtitleBold; }
HFONT fontTitle() { return g_title; }
HFONT fontHeading() { return g_heading; }
HFONT fontMono() { return g_mono; }

HFONT fontAt(int pt, bool bold) {
    int key = pt * 2 + (bold ? 1 : 0);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    HFONT f = makeFont(pt, bold, uiFace());
    g_cache[key] = f;
    return f;
}

void shutdown() {
    for (HFONT f : {g_body, g_bodyBold, g_small, g_caption, g_subtitle, g_subtitleBold, g_title,
                    g_heading, g_mono})
        if (f) ::DeleteObject(f);
    g_body = g_bodyBold = g_small = g_caption = g_subtitle = g_subtitleBold = g_title = g_heading =
        g_mono = nullptr;
}

}  // namespace theme