#include "ui/shell.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/util.h"

namespace shell {

// Segoe Fluent Icons is the Windows 11 icon face; Segoe MDL2 Assets is the
// Windows 10 equivalent and shares the same codepoints. Whichever exists is
// cached per point size, because icon fonts are created and destroyed for every
// glyph otherwise, which is a measurable cost while scrolling a long list.
namespace icon {

std::wstring face() {
    static std::wstring cached;
    if (!cached.empty()) return cached;
    HDC screen = ::GetDC(nullptr);
    const wchar_t* candidates[] = {L"Segoe Fluent Icons", L"Segoe MDL2 Assets"};
    for (const wchar_t* name : candidates) {
        HFONT probe = ::CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                    OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, name);
        if (!probe) continue;
        HGDIOBJ old = ::SelectObject(screen, probe);
        wchar_t actual[LF_FACESIZE]{};
        ::GetTextFaceW(screen, LF_FACESIZE, actual);
        ::SelectObject(screen, old);
        // CreateFont silently substitutes a default face, so the request only
        // succeeded when the font it picked matches what we asked for.
        bool ok = util::iequals(actual, name);
        ::DeleteObject(probe);
        if (ok) {
            cached = name;
            break;
        }
    }
    ::ReleaseDC(nullptr, screen);
    if (cached.empty()) cached = L"Segoe MDL2 Assets";   // last resort
    return cached;
}

std::map<int, HFONT>& cache() {
    static std::map<int, HFONT> c;
    return c;
}

}  // namespace icon

HFONT shellGlyphFont(int sizePt) {
    auto& c = icon::cache();
    auto it = c.find(sizePt);
    if (it != c.end()) return it->second;
    LOGFONTW lf{};
    lf.lfHeight = -theme::M.px(sizePt);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lf.lfPitchAndFamily = VARIABLE_PITCH | FF_DONTCARE;
    std::wstring faceName = icon::face();
    wcsncpy_s(lf.lfFaceName, faceName.c_str(), _TRUNCATE);
    HFONT f = ::CreateFontIndirectW(&lf);
    c[sizePt] = f;
    return f;
}

// -------------------------------------------------------------- back buffer --
// ------------------------------------------------------------------ Canvas ---
void Canvas::fill(const Rect& r, COLORREF color) {
    RECT rc = r.toWin();
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(dc_, &rc, brush);
    ::DeleteObject(brush);
}

void Canvas::fillRound(const Rect& r, int radius, COLORREF color) {
    if (!r.valid()) return;
    HBRUSH brush = ::CreateSolidBrush(color);
    HPEN pen = ::CreatePen(PS_SOLID, 1, color);
    HGDIOBJ oldBrush = ::SelectObject(dc_, brush);
    HGDIOBJ oldPen = ::SelectObject(dc_, pen);
    ::RoundRect(dc_, r.left(), r.top(), r.right(), r.bottom(), radius * 2, radius * 2);
    ::SelectObject(dc_, oldBrush);
    ::SelectObject(dc_, oldPen);
    ::DeleteObject(brush);
    ::DeleteObject(pen);
}

void Canvas::stroke(const Rect& r, COLORREF color, int width) {
    HPEN pen = ::CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = ::SelectObject(dc_, pen);
    HGDIOBJ oldBrush = ::SelectObject(dc_, ::GetStockObject(NULL_BRUSH));
    ::Rectangle(dc_, r.left(), r.top(), r.right(), r.bottom());
    ::SelectObject(dc_, oldPen);
    ::SelectObject(dc_, oldBrush);
    ::DeleteObject(pen);
}

void Canvas::strokeRound(const Rect& r, int radius, COLORREF color, int width) {
    if (!r.valid()) return;
    HPEN pen = ::CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = ::SelectObject(dc_, pen);
    HGDIOBJ oldBrush = ::SelectObject(dc_, ::GetStockObject(NULL_BRUSH));
    ::RoundRect(dc_, r.left(), r.top(), r.right(), r.bottom(), radius * 2, radius * 2);
    ::SelectObject(dc_, oldPen);
    ::SelectObject(dc_, oldBrush);
    ::DeleteObject(pen);
}

void Canvas::line(int x1, int y1, int x2, int y2, COLORREF color, int width) {
    HPEN pen = ::CreatePen(PS_SOLID, width, color);
    HGDIOBJ oldPen = ::SelectObject(dc_, pen);
    ::MoveToEx(dc_, x1, y1, nullptr);
    ::LineTo(dc_, x2, y2);
    ::SelectObject(dc_, oldPen);
    ::DeleteObject(pen);
}

void Canvas::gradient(const Rect& r, COLORREF top, COLORREF bottom, bool vertical) {
    TRIVERTEX vert[2];
    vert[0].x = r.left();
    vert[0].y = r.top();
    vert[0].Red = (COLOR16)(GetRValue(top) << 8);
    vert[0].Green = (COLOR16)(GetGValue(top) << 8);
    vert[0].Blue = (COLOR16)(GetBValue(top) << 8);
    vert[0].Alpha = 0xFF00;
    vert[1].x = r.right();
    vert[1].y = r.bottom();
    vert[1].Red = (COLOR16)(GetRValue(bottom) << 8);
    vert[1].Green = (COLOR16)(GetGValue(bottom) << 8);
    vert[1].Blue = (COLOR16)(GetBValue(bottom) << 8);
    vert[1].Alpha = 0xFF00;

    GRADIENT_RECT g{0, 1};
    ::GradientFill(dc_, vert, 2, &g, 1, vertical ? GRADIENT_FILL_RECT_V : GRADIENT_FILL_RECT_H);
}

void Canvas::text(const Rect& r, const std::wstring& s, COLORREF color, HFONT font, UINT format) {
    if (s.empty() || !r.valid()) return;
    HGDIOBJ old = ::SelectObject(dc_, font);
    int oldMode = ::SetBkMode(dc_, TRANSPARENT);
    COLORREF oldColor = ::SetTextColor(dc_, color);
    RECT rc = r.toWin();
    ::DrawTextW(dc_, s.c_str(), (int)s.size(), &rc, format);
    ::SetTextColor(dc_, oldColor);
    ::SetBkMode(dc_, oldMode);
    ::SelectObject(dc_, old);
}

int Canvas::textWidth(const std::wstring& s, HFONT font) const {
    HGDIOBJ old = ::SelectObject(dc_, font);
    SIZE sz{};
    ::GetTextExtentPoint32W(dc_, s.c_str(), (int)s.size(), &sz);
    ::SelectObject(dc_, old);
    return sz.cx;
}

void Canvas::textBlock(const Rect& r, const std::wstring& s, COLORREF color, HFONT font,
                       UINT format) {
    text(r, s, color, font, format);
}

void Canvas::circle(int cx, int cy, int radius, COLORREF color) {
    HBRUSH brush = ::CreateSolidBrush(color);
    HPEN pen = ::CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = ::SelectObject(dc_, brush);
    HGDIOBJ op = ::SelectObject(dc_, pen);
    ::Ellipse(dc_, cx - radius, cy - radius, cx + radius, cy + radius);
    ::SelectObject(dc_, ob);
    ::SelectObject(dc_, op);
    ::DeleteObject(brush);
    ::DeleteObject(pen);
}

void Canvas::circleOutline(int cx, int cy, int radius, COLORREF color, int width) {
    HPEN pen = ::CreatePen(PS_SOLID, width, color);
    HGDIOBJ op = ::SelectObject(dc_, pen);
    HGDIOBJ ob = ::SelectObject(dc_, ::GetStockObject(NULL_BRUSH));
    ::Ellipse(dc_, cx - radius, cy - radius, cx + radius, cy + radius);
    ::SelectObject(dc_, op);
    ::SelectObject(dc_, ob);
    ::DeleteObject(pen);
}

void Canvas::supersample(const Rect& region, const std::function<void(Canvas&)>& draw) {
    const int w = region.w, h = region.h;
    if (w <= 0 || h <= 0 || !draw) return;

    // Adaptive scale. 4x makes small shapes (rings, the corner ball) butter
    // smooth and costs nothing at their size; the big chart plots at 4x meant
    // an ~11 MB bitmap plus two filtered StretchBlt passes per chart on every
    // repaint, which is what made view switches and scrolling feel sluggish.
    // Large regions drop to 2x - still far smoother than no AA, at a quarter
    // of the work.
    int S = (w * h > 240 * 160) ? 2 : 4;

    HDC hi = ::CreateCompatibleDC(dc_);
    HBITMAP bmp = ::CreateCompatibleBitmap(dc_, w * S, h * S);
    HGDIOBJ oldBmp = ::SelectObject(hi, bmp);

    // Start from the content already on the canvas, so the shapes blend into
    // their real background instead of an arbitrary fill colour. This is an
    // exact integer-multiple enlargement, so COLORONCOLOR (plain pixel copy)
    // is lossless here and skips the expensive HALFTONE filtering pass.
    ::SetStretchBltMode(hi, COLORONCOLOR);
    ::StretchBlt(hi, 0, 0, w * S, h * S, dc_, region.x, region.y, w, h, SRCCOPY);

    // The world transform (and only that) must live inside this save block:
    // window/viewport org and extents persist in the DC after switching back
    // to GM_COMPATIBLE, and a leftover mapping makes the final StretchBlt read
    // its source from outside the bitmap - the whole draw silently vanishes.
    int saved = ::SaveDC(hi);

    // Viewport scaling, classic MM_ANISOTROPIC style: the lambda keeps drawing
    // in absolute coordinates, and window/viewport org+ext map them onto the
    // S-times larger buffer. (GM_ADVANCED world transforms proved unreliable
    // here - the shapes came out at a quarter size or not at all.)
    ::SetMapMode(hi, MM_ANISOTROPIC);
    ::SetWindowOrgEx(hi, region.x, region.y, nullptr);
    ::SetWindowExtEx(hi, w, h, nullptr);
    ::SetViewportOrgEx(hi, 0, 0, nullptr);
    ::SetViewportExtEx(hi, w * S, h * S, nullptr);

    Canvas sub(hi);
    draw(sub);

    ::RestoreDC(hi, saved);
    ::SetStretchBltMode(dc_, HALFTONE);
    ::SetBrushOrgEx(dc_, 0, 0, nullptr);
    ::StretchBlt(dc_, region.x, region.y, w, h, hi, 0, 0, w * S, h * S, SRCCOPY);

    ::SelectObject(hi, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(hi);
}

void Canvas::ring(int cx, int cy, int radius, int thickness, double fraction,
                  COLORREF color, COLORREF track) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    // Draw the ring as a thick arc. GDI angles run counter-clockwise from 3
    // o'clock, so we start at the top (90 degrees) and sweep clockwise.
    // Supersampled: a plain AngleArc at 1x is visibly jagged on the monitor
    // rings, which are the most looked-at curves in the app.
    int pad = thickness + 2;
    supersample(Rect{cx - radius - pad, cy - radius - pad, 2 * (radius + pad),
                     2 * (radius + pad)},
                [&](Canvas& c) {
                    auto arc = [&](double startFrac, double endFrac, COLORREF col) {
                        if (endFrac <= startFrac) return;
                        const double startDeg = 90.0 - startFrac * 360.0;
                        const double sweepDeg = -(endFrac - startFrac) * 360.0;
                        // AngleArc draws a line from the current position to
                        // the arc start. On a fresh DC that position is the
                        // origin, which dragged a stray line across the ring -
                        // always move to the arc start first.
                        const double rad = startDeg * 3.14159265358979323846 / 180.0;
                        ::MoveToEx(c.dc(),
                                   (int)std::lround(cx + radius * std::cos(rad)),
                                   (int)std::lround(cy - radius * std::sin(rad)), nullptr);
                        HPEN pen = ::CreatePen(PS_SOLID, thickness, col);
                        HGDIOBJ op = ::SelectObject(c.dc(), pen);
                        HGDIOBJ ob = ::SelectObject(c.dc(), ::GetStockObject(NULL_BRUSH));
                        ::AngleArc(c.dc(), cx, cy, radius, (float)startDeg, (float)sweepDeg);
                        ::SelectObject(c.dc(), op);
                        ::SelectObject(c.dc(), ob);
                        ::DeleteObject(pen);
                    };
                    arc(0.0, 1.0, track);
                    if (fraction > 0.0005) arc(0.0, fraction, color);
                });
}

void Canvas::meter(const Rect& r, double fraction, COLORREF fillColor, COLORREF track) {
    fraction = std::clamp(fraction, 0.0, 1.0);
    int radius = r.h / 2;
    fillRound(r, radius, track);
    int w = (int)std::lround(r.w * fraction);
    if (w <= 0) return;
    if (w < r.h) w = r.h;                      // keep the rounded cap visible
    fillRound(Rect{r.x, r.y, w, r.h}, radius, fillColor);
}

void Canvas::sparkline(const Rect& r, const std::vector<float>& samples, COLORREF lineColor,
                       COLORREF fillColor, double maxValue) {
    if (r.w < 4 || r.h < 4) return;
    if (samples.size() < 2) {
        fill(r, fillColor);
        return;
    }
    const size_t n = samples.size();
    std::vector<POINT> pts;
    pts.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        double v = std::clamp((double)samples[i] / maxValue, 0.0, 1.0);
        int x = r.x + (int)std::lround((double)i * (r.w - 1) / (double)(n - 1));
        int y = r.bottom() - 1 - (int)std::lround(v * (r.h - 2));
        pts.push_back(POINT{x, y});
    }

    // Filled area under the curve, then the line - both supersampled, since a
    // 1x Polyline reads as a jagged mountain range on the resource charts.
    supersample(Rect{r.x - 2, r.y - 2, r.w + 4, r.h + 4}, [&](Canvas& c) {
        std::vector<POINT> poly;
        poly.reserve(pts.size() + 2);
        poly.push_back(POINT{r.x, r.bottom() - 1});
        for (const POINT& p : pts) poly.push_back(p);
        poly.push_back(POINT{r.right() - 1, r.bottom() - 1});

        HBRUSH brush = ::CreateSolidBrush(fillColor);
        HPEN pen = ::CreatePen(PS_SOLID, 1, fillColor);
        HGDIOBJ ob = ::SelectObject(c.dc(), brush);
        HGDIOBJ op = ::SelectObject(c.dc(), pen);
        ::Polygon(c.dc(), poly.data(), (int)poly.size());
        ::SelectObject(c.dc(), ob);
        ::SelectObject(c.dc(), op);
        ::DeleteObject(brush);
        ::DeleteObject(pen);

        HPEN linePen = ::CreatePen(PS_SOLID, std::max(1, theme::M.px(2)), lineColor);
        HGDIOBJ olp = ::SelectObject(c.dc(), linePen);
        ::Polyline(c.dc(), pts.data(), (int)pts.size());
        ::SelectObject(c.dc(), olp);
        ::DeleteObject(linePen);
    });
}

void Canvas::glyph(const Rect& r, wchar_t code, COLORREF color, int sizePt) {
    if (code == 0) return;
    HFONT font = shellGlyphFont(sizePt);
    if (!font) return;
    std::wstring s(1, code);
    text(r, s, color, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// ------------------------------------------------------------------ buttons --
void button(Canvas& c, const Rect& r, const std::wstring& label, ButtonStyle style, bool hovered,
            bool pressed, bool focused, wchar_t leadingGlyph, wchar_t trailingGlyph, bool enabled) {
    COLORREF base = theme::CardBg;
    COLORREF textColor = theme::TextPrimary;
    COLORREF border = theme::BorderStrong;

    switch (style) {
        case ButtonStyle::Primary:
            base = theme::Accent;
            textColor = theme::TextOnAccent;
            border = theme::Accent;
            break;
        case ButtonStyle::Danger:
            base = theme::CardBg;
            textColor = theme::Danger;
            border = theme::BorderStrong;
            break;
        case ButtonStyle::Subtle:
            base = theme::LayerBg;
            border = theme::LayerBg;
            textColor = theme::TextPrimary;
            break;
        case ButtonStyle::Secondary:
        default:
            break;
    }

    // Applied after the style, so a disabled button of any style lands on the
    // same muted surface a disabled field uses - the whole row then reads as
    // one inactive unit instead of a grey label on a live-looking button.
    if (!enabled) {
        base = theme::fieldBack(false);
        textColor = theme::TextDisabled;
        border = theme::fieldBack(false);
        hovered = pressed = false;
    }

    // Windows 11 tints the accent button lighter on hover and darker on press;
    // neutral buttons go slightly darker for both.
    if (pressed) {
        base = theme::blend(base, style == ButtonStyle::Primary ? RGB(255, 255, 255) : RGB(0, 0, 0),
                            theme::PressAlpha);
        if (style == ButtonStyle::Danger) textColor = theme::blend(theme::Danger, RGB(0, 0, 0), 40);
    } else if (hovered) {
        base = theme::blend(base, style == ButtonStyle::Primary ? RGB(255, 255, 255) : RGB(0, 0, 0),
                            theme::HoverAlpha);
    }

    c.fillRound(r, theme::M.radiusMedium, base);
    if (style != ButtonStyle::Subtle && style != ButtonStyle::Primary)
        c.strokeRound(r, theme::M.radiusMedium, border);

    if (focused) {
        c.strokeRound(r.inset(-2), theme::M.radiusMedium, theme::FocusRing, 2);
    }

    int pad = theme::M.px(10);
    int iconSize = theme::M.px(16);
    int textLeft = r.x + pad;
    int textRight = r.right() - pad;

    if (leadingGlyph) {
        Rect gr{textLeft, r.y, iconSize, r.h};
        c.glyph(gr, leadingGlyph, textColor, 15);
        textLeft += iconSize + theme::M.px(6);
    }
    if (trailingGlyph) {
        Rect gr{textRight - iconSize, r.y, iconSize, r.h};
        c.glyph(gr, trailingGlyph, textColor, 15);
        textRight -= iconSize + theme::M.px(6);
    }
    Rect tr{textLeft, r.y, std::max(0, textRight - textLeft), r.h};
    HFONT font = style == ButtonStyle::Primary || style == ButtonStyle::Danger
                     ? theme::fontBodyBold()
                     : theme::fontBody();
    // Measure with the font the label will actually be drawn in. ellipsize()
    // measures against whatever is currently selected in the DC, which is some
    // earlier font - that produced a short measurement and a label cut down to
    // "选择文..." even when it fitted comfortably.
    HGDIOBJ oldFont = ::SelectObject(c.dc(), font);
    std::wstring shown = util::ellipsize(c.dc(), label, tr.w);
    ::SelectObject(c.dc(), oldFont);
    c.text(tr, shown, textColor, font, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void iconButton(Canvas& c, const Rect& r, wchar_t glyph, bool hovered, bool pressed, bool selected) {
    COLORREF base = theme::TopBarBg;
    if (selected) {
        base = theme::SelectedBg;
    } else if (pressed) {
        base = theme::blend(base, RGB(0, 0, 0), theme::PressAlpha);
    } else if (hovered) {
        base = theme::blend(base, RGB(0, 0, 0), theme::HoverAlpha);
    }
    c.fillRound(r, theme::M.radiusMedium, base);
    c.glyph(r, glyph, selected ? theme::Accent : theme::TextPrimary, 16);
}

void toolbarButton(Canvas& c, const Rect& r, wchar_t glyph, const std::wstring& caption,
                   bool hovered, bool pressed, bool selected) {
    COLORREF base = theme::TopBarBg;
    if (selected)
        base = theme::SelectedBg;
    else if (pressed)
        base = theme::blend(base, RGB(0, 0, 0), theme::PressAlpha);
    else if (hovered)
        base = theme::blend(base, RGB(0, 0, 0), theme::HoverAlpha);

    c.fillRound(r, theme::M.radiusMedium, base);

    // Lay the icon and caption out as one block and centre that block in the
    // button. Positioning the icon from the top instead leaves the caption
    // floating in the lower half, which reads as top-heavy - especially once
    // the button grows at higher DPI.
    int iconSize = theme::M.px(18);
    int capH = theme::M.px(16);
    int gap = theme::M.px(2);
    int block = iconSize + gap + capH;
    int top = r.y + std::max(0, (r.h - block) / 2);

    Rect gr{r.x, top, r.w, iconSize};
    c.glyph(gr, glyph, selected ? theme::Accent : theme::TextPrimary, 17);
    Rect cap{r.x, gr.bottom() + gap, r.w, capH};
    c.text(cap, util::ellipsize(c.dc(), caption, cap.w, theme::fontCaption()), theme::TextSecondary, theme::fontCaption(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// ------------------------------------------------------------------- cards --
void card(Canvas& c, const Rect& r, COLORREF bg, COLORREF border, bool elevated) {
    if (elevated) {
        for (int step = 0; step < 2; ++step) {
            int offset = theme::M.px(2 - step);
            int alpha = step == 0 ? 20 : 11;
            Rect s{r.x, r.y + offset, r.w, r.h};
            c.fillRound(s, theme::M.radiusLarge, theme::blend(theme::WindowBg, RGB(0, 0, 0), alpha));
        }
    }
    c.fillRound(r, theme::M.radiusLarge, bg);
    if (border != CLR_INVALID) c.strokeRound(r, theme::M.radiusLarge, border);
}

void sectionTitle(Canvas& c, const Rect& r, const std::wstring& text) {
    c.text(r, text, theme::TextSecondary, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void divider(Canvas& c, int x1, int y, int x2, COLORREF color) {
    c.line(x1, y, x2, y, color, 1);
}

void chip(Canvas& c, const Rect& r, const std::wstring& text, COLORREF bg, COLORREF fg) {
    c.fillRound(r, r.h / 2, bg);
    Rect tr = r.inset(theme::M.px(8), 0);
    c.text(tr, util::ellipsize(c.dc(), text, tr.w, theme::fontCaption()), fg, theme::fontCaption(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void badge(Canvas& c, const Rect& r, const std::wstring& text, COLORREF bg, COLORREF fg) {
    c.fillRound(r, theme::M.radiusSmall, bg);
    Rect tr = r.inset(theme::M.px(6), 0);
    c.text(tr, util::ellipsize(c.dc(), text, tr.w, theme::fontCaption()), fg, theme::fontCaption(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// ------------------------------------------------------------------ metrics --
void metricTile(Canvas& c, const Rect& r, const std::wstring& label, const std::wstring& primary,
                const std::wstring& secondary, double fraction, COLORREF accent, wchar_t glyph) {
    card(c, r);

    int pad = theme::M.px(14);
    int ringSize = theme::M.px(58);
    Rect ringArea{r.x + pad, r.y + (r.h - ringSize) / 2, ringSize, ringSize};
    int radius = ringSize / 2 - theme::M.px(5);
    int thickness = theme::M.px(5);

    c.ring(ringArea.cx(), ringArea.cy(), radius, thickness, fraction, accent, theme::ChartTrack);

    // The percentage sits inside the ring; the caption lives under it.
    Rect pctRect{ringArea.x, ringArea.cy() - theme::M.px(9), ringSize, theme::M.px(18)};
    c.text(pctRect, primary, theme::TextPrimary, theme::fontAt(13, true),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int textLeft = ringArea.right() + pad;
    int textWidth = r.right() - pad - textLeft;
    if (textWidth > 20) {
        Rect lr{textLeft, r.y + theme::M.px(16), textWidth, theme::M.px(18)};
        if (glyph) {
            Rect gr{lr.x, lr.y, theme::M.px(16), lr.h};
            c.glyph(gr, glyph, theme::TextSecondary, 14);
            lr.x += theme::M.px(20);
            lr.w -= theme::M.px(20);
        }
        c.text(lr, label, theme::TextSecondary, theme::fontBody(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Rect sr{textLeft, lr.bottom() + theme::M.px(2), textWidth, theme::M.px(18)};
        c.text(sr, util::ellipsize(c.dc(), secondary, sr.w, theme::fontCaption()), theme::TextTertiary,
               theme::fontCaption(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

void meterRow(Canvas& c, const Rect& r, const std::wstring& label, const std::wstring& value,
              double fraction, COLORREF accent, wchar_t glyph) {
    int iconSize = theme::M.px(16);
    int labelW = theme::M.px(110);
    int valueW = theme::M.px(120);

    Rect ir{r.x, r.y, iconSize, r.h};
    if (glyph) c.glyph(ir, glyph, accent, 14);

    Rect lr{r.x + iconSize + theme::M.px(8), r.y, labelW, r.h};
    c.text(lr, label, theme::TextSecondary, theme::fontSmall(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int barX = lr.right() + theme::M.px(8);
    int barW = r.right() - valueW - theme::M.px(10) - barX;
    if (barW > 20) {
        Rect bar{barX, r.cy() - theme::M.px(4), barW, theme::M.px(8)};
        c.meter(bar, fraction, accent, theme::ChartTrack);
    }

    Rect vr{r.right() - valueW, r.y, valueW, r.h};
    c.text(vr, util::ellipsize(c.dc(), value, vr.w, theme::fontBody()), theme::TextPrimary, theme::fontBody(),
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

}  // namespace shell