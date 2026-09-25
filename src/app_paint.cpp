// app_paint.cpp - everything the window draws.
//
// The whole UI is composed here from the widget kit in ui/shell.h. Painting is
// also where hit regions are registered: each interactive element calls addHit
// as it is drawn, so geometry is defined in exactly one place.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "core/i18n.h"
#include "core/paths.h"
#include "core/util.h"
#include "ui/theme.h"

using shell::Canvas;
using shell::Rect;
using shell::Layout;

namespace app {

namespace {

// Round up to the nearest pixel-aligned value so borders stay crisp.
int align(int v) { return v; }

COLORREF usageColor(double fraction) {
    if (fraction >= 0.9) return theme::Danger;
    if (fraction >= 0.75) return theme::Warning;
    return theme::Accent;
}

// The accent used for each metric, matching the chart series colours.
// Metric accents. The chart colours follow the system theme now, so they are
// re-read at the top of every paint rather than frozen at compile time.
COLORREF kCpuColor = RGB(0, 95, 184);
COLORREF kGpuColor = RGB(113, 76, 168);
COLORREF kMemColor = RGB(16, 124, 16);


}  // namespace

void App::addHit(const Rect& r, Action a, const std::wstring& payload, bool enabled) {
    Hit h;
    h.rect = r;
    h.action = a;
    h.payload = payload;
    h.enabled = enabled;
    hits_.push_back(h);
}

// ------------------------------------------------------------------ top bar --
void App::paintTopBar(Canvas& c, const Frame& f) {
    // Main-2. A hairline under the bar separates it from the body without the
    // heavy chrome of a traditional menu.
    c.fill(f.topBar, theme::TopBarBg);
    c.line(0, f.topBar.bottom() - 1, f.topBar.w, f.topBar.bottom() - 1, theme::Border);

    // Product mark plus the running-state dot.
    int iconSize = theme::M.px(22);
    Rect mark{theme::M.padding, f.topBar.cy() - iconSize / 2, iconSize, iconSize};
    c.fillRound(mark, theme::M.radiusSmall, theme::Accent);
    c.glyph(mark, shell::glyphs::kChip, theme::TextOnAccent, 13);

    bool llamaReady = !store_.serverExe().empty();
    bool running = processAlive();
    COLORREF dot = running ? theme::Success : (llamaReady ? theme::TextTertiary : theme::Warning);
    c.circle(mark.right() + theme::M.px(9), mark.cy(), theme::M.px(3), dot);

    Rect title{mark.right() + theme::M.px(17), 0, theme::M.px(190), f.topBar.h};
    c.text(title, T(Str::AppTitle), theme::TextPrimary, theme::fontSubtitleBold(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- toolbar (main-2), right aligned ----
    struct Tool {
        Action action;
        wchar_t glyph;
        Str caption;
    };
    const Tool tools[] = {
        {Action::OpenSettings, shell::glyphs::kSettings, Str::Settings},
        {Action::OpenImport, shell::glyphs::kTransfer, Str::ImportExportShort},
        {Action::OpenWeb, shell::glyphs::kChat, Str::WebChat},
        {Action::OpenHelp, shell::glyphs::kHelp, Str::Help},
    };

    int count = (int)(sizeof(tools) / sizeof(tools[0]));
    int bw = theme::M.px(84);
    int bh = f.topBar.h - theme::M.px(8);
    int gap = theme::M.px(4);
    int totalW = count * bw + (count - 1) * gap;
    int x = f.topBar.right() - theme::M.padding - totalW;

    for (int i = 0; i < count; ++i) {
        Rect r{x, (f.topBar.h - bh) / 2, bw, bh};
        int idx = (int)hits_.size();
        addHit(r, tools[i].action);
        bool hovered = idx == hoverIndex_;
        bool pressed = idx == pressIndex_;
        shell::toolbarButton(c, r, tools[i].glyph, T(tools[i].caption), hovered, pressed, false);
        x += bw + gap;
    }
}

// ------------------------------------------------------------------ sidebar --
void App::paintSidebar(Canvas& c, const Frame& f) {
    // Main-1: the saved configuration list.
    c.fill(f.sidebar, theme::SidebarBg);
    c.line(f.sidebar.right() - 1, f.sidebar.top(), f.sidebar.right() - 1, f.sidebar.bottom(),
           theme::Border);

    // ---- header: title + "new configuration" ----
    Rect head = f.sidebarHeader;
    Rect label{head.x, head.y, head.w - theme::M.px(78), head.h};
    const std::vector<store::Config>& all = store_.configs();
    c.text(label, T(Str::Configurations), theme::TextSecondary, theme::fontBodyBold(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    std::wstring countText = util::format(L"%d %s", (int)all.size(), T(Str::ConfigsCount));
    Rect countRect{head.x, head.y, head.w, head.h};
    c.text(countRect, countText, theme::TextTertiary, theme::fontCaption(),
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    Rect newBtn{head.right() - theme::M.px(76), head.y, theme::M.px(76), head.h};
    {
        int idx = (int)hits_.size();
        addHit(newBtn, Action::NewConfig);
        // A compact "+ 新建 / New" pill. The icon and label are drawn by hand so
        // the glyph sits tight against the text in the narrow header slot.
        bool hov = idx == hoverIndex_;
        bool pre = idx == pressIndex_;
        Canvas& bc = c;
        bc.fillRound(newBtn, theme::M.radiusMedium, theme::Accent);
        if (hov || pre)
            bc.overlay(newBtn, theme::M.radiusMedium, pre ? theme::PressAlpha : theme::HoverAlpha);

        std::wstring shortLabel = (i18n::current() == Lang::Zh) ? L"新建" : L"New";
        int labelW = bc.textWidth(shortLabel, theme::fontBodyBold());
        int glyphW = theme::M.px(15);
        int innerW = glyphW + theme::M.px(6) + labelW;
        int innerX = newBtn.x + (newBtn.w - innerW) / 2;

        bc.glyph(Rect{innerX, newBtn.y, glyphW, newBtn.h}, shell::glyphs::kAdd,
                 theme::TextOnAccent, 12);
        bc.text(Rect{innerX + glyphW + theme::M.px(6), newBtn.y, labelW + theme::M.px(4),
                     newBtn.h},
                shortLabel, theme::TextOnAccent, theme::fontBodyBold(),
                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // ---- search box ----
    Rect box = f.search;
    bool focused = (::GetFocus() == searchEdit_);
    c.fillRound(box, theme::M.radiusMedium, theme::CardBg);
    c.strokeRound(box, theme::M.radiusMedium,
                  focused ? theme::Accent : theme::BorderStrong, focused ? 2 : 1);
    c.glyph(Rect{box.x + theme::M.px(8), box.y, theme::M.px(16), box.h},
            shell::glyphs::kSearch, theme::TextTertiary, 13);
    if (searchText_.empty() && ::GetWindowTextLengthW(searchEdit_) == 0) {
        c.text(Rect{box.x + theme::M.px(30), box.y, box.w - theme::M.px(36), box.h},
               T(Str::SearchPlaceholder), theme::TextTertiary, theme::fontBody(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // ---- configuration rows ----
    std::vector<const store::Config*> list = filteredConfigs();
    Rect listArea = f.list;

    if (list.empty()) {
        // Empty state: centred in the list area with a soft glyph tile above the
        // text. Pinning the message to the top instead leaves the rest of the
        // panel looking like it failed to load.
        int tile = theme::M.px(48);
        int blockH = tile + theme::M.px(14) + theme::M.px(22) + theme::M.px(4) + theme::M.px(18);
        int top = listArea.y + std::max(theme::M.px(24), (listArea.h - blockH) / 2);

        Rect glyphTile{listArea.cx() - tile / 2, top, tile, tile};
        c.fillRound(glyphTile, theme::M.radiusLarge, theme::AccentSoft);
        c.glyph(glyphTile, shell::glyphs::kChip, theme::Accent, 22);

        Rect t{listArea.x + theme::M.padding, glyphTile.bottom() + theme::M.px(14),
               listArea.w - theme::M.padding * 2, theme::M.px(22)};
        c.text(t, T(Str::NoConfigs), theme::TextSecondary, theme::fontBody(),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        c.text(Rect{t.x, t.bottom() + theme::M.px(4), t.w, theme::M.px(18)},
               searchText_.empty() ? T(Str::NoConfigsHint) : T(Str::SearchPlaceholder),
               theme::TextTertiary, theme::fontCaption(),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        return;
    }

    int rowH = theme::M.px(58);
    int rowGap = theme::M.px(3);
    int contentH = (int)list.size() * (rowH + rowGap);
    int maxScroll = std::max(0, contentH - listArea.h + theme::M.px(8));
    sidebarScroll_ = std::clamp(sidebarScroll_, 0, maxScroll);

    HRGN clip = ::CreateRectRgn(listArea.left(), listArea.top(), listArea.right(),
                                listArea.bottom());
    ::SelectClipRgn(c.dc(), clip);

    int y = listArea.y - sidebarScroll_;
    for (const store::Config* cfg : list) {
        Rect row{listArea.x + theme::M.px(8), y, listArea.w - theme::M.px(16), rowH};
        y += rowH + rowGap;
        if (row.bottom() < listArea.top() || row.top() > listArea.bottom()) continue;

        int idx = (int)hits_.size();
        bool selected = (cfg->id == selectedId_);

        // Windows 11 selection pill: a filled rounded rect plus a 3px accent
        // bar on the leading edge.
        if (selected) {
            c.fillRound(row, theme::M.radiusMedium, theme::SelectedBg);
            c.fillRound(Rect{row.x, row.y + theme::M.px(12), theme::M.px(3),
                             row.h - theme::M.px(24)},
                        theme::M.px(2), theme::Accent);
        } else if (idx == hoverIndex_) {
            c.fillRound(row, theme::M.radiusMedium, theme::blend(theme::SidebarBg, RGB(0, 0, 0), 10));
        }
        addHit(row, Action::SelectConfig, cfg->id);

        // Leading glyph tile.
        Rect tile{row.x + theme::M.px(10), row.cy() - theme::M.px(14), theme::M.px(28),
                  theme::M.px(28)};
        c.fillRound(tile, theme::M.radiusSmall, selected ? theme::AccentSoft : theme::CardBg);
        c.glyph(tile, shell::glyphs::kChip, selected ? theme::Accent : theme::TextSecondary, 14);

        int textX = tile.right() + theme::M.px(10);
        int textW = row.right() - theme::M.px(10) - textX;
        Rect name{textX, row.y + theme::M.px(9), textW, theme::M.px(20)};
        c.text(name, util::ellipsize(c.dc(), cfg->name, name.w),
               selected ? theme::TextPrimary : theme::TextPrimary, theme::fontBodyBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect sub{textX, name.bottom(), textW, theme::M.px(18)};
        std::wstring model = util::fileName(cfg->modelFile());
        if (model.empty()) model = cfg->id;
        c.text(sub, util::ellipsize(c.dc(), model, sub.w), theme::TextTertiary,
               theme::fontCaption(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    ::SelectClipRgn(c.dc(), nullptr);
    ::DeleteObject(clip);

    // A slim scroll indicator, only when there is something to scroll.
    if (maxScroll > 0) {
        int trackTop = listArea.top() + theme::M.px(4);
        int trackH = listArea.h - theme::M.px(8);
        double visible = (double)listArea.h / (double)(contentH > 0 ? contentH : 1);
        visible = std::clamp(visible, 0.15, 1.0);
        int thumbH = (int)(trackH * visible);
        int travel = trackH - thumbH;
        double pos = maxScroll > 0 ? (double)sidebarScroll_ / (double)maxScroll : 0.0;
        Rect thumb{listArea.right() - theme::M.px(6), trackTop + (int)(travel * pos),
                   theme::M.px(3), thumbH};
        c.fillRound(thumb, theme::M.px(2), theme::BorderStrong);
    }
}

// ------------------------------------------------------------------ content --
void App::paintContent(Canvas& c, const Frame& f) {
    // The canvas is painted here rather than handed to the DWM. A hand-drawn
    // GDI window cannot get a real Mica material (the DWM either ignores its
    // transparent pixels or hands back a snapshot that never refreshes), so the
    // surface is a barely-there vertical gradient over a colourisation tint -
    // which is what makes it read as a Win11 surface instead of a flat fill.
    COLORREF lift = theme::isDarkMode() ? RGB(255, 255, 255) : RGB(255, 255, 255);
    COLORREF drop = theme::isDarkMode() ? RGB(0, 0, 0) : RGB(0, 0, 0);
    COLORREF top = theme::blend(theme::WindowBg, lift, theme::isDarkMode() ? 18 : 40);
    COLORREF bottom = theme::blend(theme::WindowBg, drop, 13);
    c.gradient(f.content, top, bottom, true);

    // Clip so scrolling views cannot bleed into the top or bottom bars.
    HRGN clip = ::CreateRectRgn(f.content.left(), f.content.top(), f.content.right(),
                                f.content.bottom());
    ::SelectClipRgn(c.dc(), clip);

    switch (view_) {
        case View::Detail: {
            if (const store::Config* cfg = selected()) {
                paintDetail(c, f.content, *cfg);
            } else {
                view_ = View::Welcome;
                paintWelcome(c, f.content, f);
            }
            break;
        }
        case View::Running: {
            if (const store::Config* cfg = selected()) {
                paintRunning(c, f.content, *cfg);
            } else {
                view_ = View::Welcome;
                paintWelcome(c, f.content, f);
            }
            break;
        }
        case View::Chat:
            // The browser pane covers the page area, so only the strip of
            // chrome above it is drawn here.
            paintChat(c, f);
            break;

        case View::Welcome:
        default:
            paintWelcome(c, f.content, f);
            break;
    }

    ::SelectClipRgn(c.dc(), nullptr);
    ::DeleteObject(clip);
}

void App::paintWelcome(Canvas& c, const Rect& area, const Frame&) {
    // Main-3 default state: the primary call to action plus a short explainer.
    // The card and the storage hint underneath are centred as one group, so the
    // composition does not look like it is drifting upward.
    int cardW = std::min(theme::M.px(560), area.w - theme::M.px(80));
    int cardH = theme::M.px(268);
    int hintGap = theme::M.px(14);
    int hintH = theme::M.px(20);
    int groupH = cardH + hintGap + hintH;
    int groupTop = area.y + std::max(theme::M.px(32), (area.h - groupH) / 2);
    Rect cardRect{area.cx() - cardW / 2, groupTop, cardW, cardH};

    shell::card(c, cardRect, theme::CardBg, theme::Border, /*elevated=*/true);

    // Hero glyph tile.
    Rect tile{cardRect.cx() - theme::M.px(29), cardRect.y + theme::M.px(34), theme::M.px(58),
              theme::M.px(58)};
    c.fillRound(tile, theme::M.radiusLarge, theme::AccentSoft);
    c.glyph(tile, shell::glyphs::kChip, theme::Accent, 26);

    Rect title{cardRect.x + theme::M.px(28), tile.bottom() + theme::M.px(20),
               cardRect.w - theme::M.px(56), theme::M.px(30)};
    c.text(title, T(Str::WelcomeTitle), theme::TextPrimary, theme::fontHeading(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    Rect body{cardRect.x + theme::M.px(34), title.bottom() + theme::M.px(10),
              cardRect.w - theme::M.px(68), theme::M.px(70)};
    c.textBlock(body, T(Str::WelcomeBody), theme::TextSecondary, theme::fontBody(),
                DT_CENTER | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

    Rect btn{cardRect.cx() - theme::M.px(105), cardRect.bottom() - theme::M.px(66),
             theme::M.px(210), theme::M.px(40)};
    {
        int idx = (int)hits_.size();
        addHit(btn, Action::NewConfig);
        shell::button(c, btn, T(Str::CreateConfiguration), shell::ButtonStyle::Primary,
                      idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kAdd);
    }

    // A quiet hint about where things are stored, which answers the usual
    // "where did my configs go?" question before it is asked. It sits on its own
    // pill rather than straight on the Mica canvas: the canvas stays at alpha 0,
    // so text drawn there directly would have nothing to composite against.
    int hintW = c.textWidth(paths::dataRoot(), theme::fontCaption()) + theme::M.px(28);
    hintW = std::min(hintW, area.w - theme::M.px(32));
    Rect hint{area.cx() - hintW / 2, cardRect.bottom() + hintGap, hintW, hintH};
    c.fillRound(hint, theme::M.radiusSmall, theme::LayerBg);
    c.strokeRound(hint, theme::M.radiusSmall, theme::Border);
    c.text(hint, paths::dataRoot(), theme::TextTertiary, theme::fontCaption(),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

void App::paintDetail(Canvas& c, const Rect& area, const store::Config& cfg) {
    // Main 3-1: every parameter of the selected configuration, grouped, plus
    // the start / modify / delete actions.
    int pad = theme::M.px(24);
    int x = area.x + pad;
    int w = area.w - pad * 2;
    int y = area.y + pad - contentScroll_;

    // ---- header card ----
    Rect header{x, y, w, theme::M.px(112)};
    shell::card(c, header);

    Rect tile{header.x + theme::M.px(18), header.y + theme::M.px(20), theme::M.px(44),
              theme::M.px(44)};
    c.fillRound(tile, theme::M.radiusMedium, theme::AccentSoft);
    c.glyph(tile, shell::glyphs::kChip, theme::Accent, 20);

    Rect name{tile.right() + theme::M.px(14), header.y + theme::M.px(18),
              header.w - tile.w - theme::M.px(180), theme::M.px(26)};
    c.text(name, util::ellipsize(c.dc(), cfg.name, name.w), theme::TextPrimary,
           theme::fontTitle(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    Rect meta{name.x, name.bottom() + theme::M.px(2), name.w, theme::M.px(20)};
    std::wstring model = util::fileName(cfg.modelFile());
    std::wstring metaText = model.empty() ? T(Str::DetailNotes) : model;
    metaText += util::format(L"   ·   %s %d   ·   %s %d", T(Str::MetricThreads),
                             cfg.intValue(L"-t", 0), T(Str::MetricGpu),
                             cfg.intValue(L"-ngl", 0));
    c.text(meta, util::ellipsize(c.dc(), metaText, meta.w), theme::TextTertiary,
           theme::fontCaption(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Status chip: running or idle.
    bool running = processAlive();
    std::wstring chipText = running ? T(Str::ServerReady) : T(Str::StatusReady);
    COLORREF chipBg = running ? theme::Success : theme::Border;
    COLORREF chipFg = running ? theme::TextOnAccent : theme::TextSecondary;
    int chipW = c.textWidth(chipText, theme::fontCaption()) + theme::M.px(18);
    Rect chipRect{name.x, meta.bottom() + theme::M.px(4), chipW, theme::M.px(20)};
    shell::chip(c, chipRect, chipText, chipBg, chipFg);

    // ---- action buttons, right aligned in the header ----
    int bw = theme::M.px(96);
    int bh = theme::M.px(36);
    int gap = theme::M.px(8);
    int bx = header.right() - theme::M.px(18) - bw;

    Rect deleteBtn{bx, header.cy() - bh / 2 - theme::M.px(14), bw, bh};
    bx -= bw + gap;
    Rect editBtn{bx, header.cy() - bh / 2 - theme::M.px(14), bw, bh};

    // Start / stop share a slot because only one of them is ever relevant.
    Rect startBtn{bx - gap - theme::M.px(104), header.cy() - bh / 2 - theme::M.px(14),
                  theme::M.px(104), bh};

    {
        int idx = (int)hits_.size();
        addHit(startBtn, running ? Action::Stop : Action::Start, cfg.id);
        shell::button(c, startBtn, running ? T(Str::Stop) : T(Str::Start),
                      running ? shell::ButtonStyle::Danger : shell::ButtonStyle::Primary,
                      idx == hoverIndex_, idx == pressIndex_, false,
                      running ? shell::glyphs::kStop : shell::glyphs::kPlay);
    }
    {
        int idx = (int)hits_.size();
        addHit(editBtn, Action::Modify, cfg.id);
        shell::button(c, editBtn, T(Str::Modify), shell::ButtonStyle::Secondary,
                      idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kEdit);
    }
    {
        int idx = (int)hits_.size();
        addHit(deleteBtn, Action::Delete, cfg.id);
        shell::button(c, deleteBtn, T(Str::Delete), shell::ButtonStyle::Danger,
                      idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kDelete);
    }

    y = header.bottom() + theme::M.gapLarge;

    // ---- command line preview ----
    Rect cmdCard{x, y, w, theme::M.px(96)};
    shell::card(c, cmdCard);
    shell::sectionTitle(c, Rect{cmdCard.x + theme::M.px(16), cmdCard.y + theme::M.px(8),
                                cmdCard.w - theme::M.px(32), theme::M.px(18)},
                     T(Str::DetailCommand));

    Rect cmdText{cmdCard.x + theme::M.px(16), cmdCard.y + theme::M.px(28),
                 cmdCard.w - theme::M.px(32) - theme::M.px(96), theme::M.px(56)};
    c.fillRound(cmdText, theme::M.radiusSmall, RGB(246, 246, 246));
    c.textBlock(cmdText.inset(theme::M.px(8)), store::buildDisplayCommand(cfg, store_.serverExe()),
                theme::TextSecondary, theme::fontMono(),
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);

    Rect copyBtn{cmdCard.right() - theme::M.px(16) - theme::M.px(88),
                 cmdCard.bottom() - theme::M.px(16) - theme::M.px(30), theme::M.px(88),
                 theme::M.px(30)};
    {
        int idx = (int)hits_.size();
        addHit(copyBtn, Action::CopyCommand, cfg.id);
        shell::button(c, copyBtn, T(Str::CopyCommand), shell::ButtonStyle::Subtle,
                      idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kCopy);
    }

    y = cmdCard.bottom() + theme::M.gapLarge;

    // ---- grouped parameters ----
    // Group order matches the editor so the two views feel like the same thing.
    struct Section {
        catalog::Group group;
        std::wstring title;
    };
    std::vector<Section> sections;
    for (int g = 0; g < (int)catalog::Group::Count; ++g) {
        auto grp = (catalog::Group)g;
        bool hasAny = false;
        for (const store::Param& p : cfg.params) {
            const catalog::Spec* spec = catalog::find(p.flag);
            if (grp == catalog::Group::Custom) {
                if (p.custom) hasAny = true;
            } else if (spec && spec->group == grp) {
                hasAny = true;
            }
        }
        if (hasAny) sections.push_back(Section{grp, catalog::groupName(grp)});
    }

    for (const Section& s : sections) {
        // Gather the rows for this section.
        std::vector<const store::Param*> rows;
        for (const store::Param& p : cfg.params) {
            const catalog::Spec* spec = catalog::find(p.flag);
            if (s.group == catalog::Group::Custom) {
                if (p.custom) rows.push_back(&p);
            } else if (spec && spec->group == s.group) {
                rows.push_back(&p);
            }
        }

        int rowH = theme::M.px(34);
        int cardH = theme::M.px(40) + (int)rows.size() * rowH + theme::M.px(6);
        Rect card{x, y, w, cardH};
        if (card.bottom() < area.top() - theme::M.px(60) || card.top() > area.bottom()) {
            y += cardH + theme::M.gap;
            contentScrollMax_ = std::max(contentScrollMax_, y - contentScroll_ - area.bottom());
            continue;
        }
        shell::card(c, card);

        shell::sectionTitle(c, Rect{card.x + theme::M.px(16), card.y + theme::M.px(10),
                                    card.w - theme::M.px(32), theme::M.px(20)},
                           s.title);

        int ry = card.y + theme::M.px(36);
        for (const store::Param* p : rows) {
            Rect row{card.x, ry, card.w, rowH};
            ry += rowH;

            const catalog::Spec* spec = catalog::find(p->flag);
            bool toggle = spec && spec->isToggle;
            // The stored switch decides whether the flag reaches the command
            // line; the value only matters for value-style parameters.
            // Deriving the state from the value instead made a switched-on
            // toggle (empty value) display as off while the editor showed on.
            bool on = p->enabled;
            bool passed = on && (toggle || !util::trim(p->value).empty());

            // Left column: the flag, rendered in the mono face so it reads as
            // literal command-line text.
            Rect flagRect{row.x + theme::M.px(18), row.y, theme::M.px(196), row.h};
            c.text(flagRect, util::ellipsize(c.dc(), store::flagLabel(p->flag), flagRect.w),
                   theme::TextPrimary, theme::fontMono(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            // Middle column: the value, or a toggle state chip.
            Rect valueRect{flagRect.right() + theme::M.px(10), row.y,
                           row.w - theme::M.px(196) - theme::M.px(18) - theme::M.px(24) -
                               theme::M.px(10),
                           row.h};
            if (toggle) {
                std::wstring state = on ? L"on" : L"off";
                int cw = c.textWidth(state, theme::fontCaption()) + theme::M.px(16);
                Rect chipR{valueRect.x, row.cy() - theme::M.px(10), cw, theme::M.px(20)};
                shell::chip(c, chipR, state, on ? theme::AccentSoft : theme::Border,
                            on ? theme::Accent : theme::TextTertiary);
            } else if (!util::trim(p->value).empty()) {
                bool isPath = spec && (spec->valueIsFile || spec->valueIsDir);
                c.text(valueRect,
                       util::ellipsize(c.dc(), p->value, valueRect.w),
                       isPath ? theme::TextSecondary : theme::TextPrimary, theme::fontMono(),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            } else {
                c.text(valueRect, p->desc.empty() ? L"—" : p->desc, theme::TextTertiary,
                       theme::fontCaption(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            // Trailing dot signals that the parameter will be passed.
            c.circle(row.right() - theme::M.px(22), row.cy(), theme::M.px(3),
                     passed ? theme::Success : theme::BorderStrong);

            if (ry < card.bottom() - rowH) {
                c.line(row.x + theme::M.px(18), row.bottom() - 1, row.right() - theme::M.px(18),
                       row.bottom() - 1, theme::Divider);
            }
        }
        y += cardH + theme::M.gap;
    }

    // ---- notes ----
    if (!util::trim(cfg.notes).empty()) {
        int notesH = theme::M.px(96);
        Rect notes{x, y, w, notesH};
        shell::card(c, notes);
        shell::sectionTitle(c, Rect{notes.x + theme::M.px(16), notes.y + theme::M.px(10),
                                    notes.w - theme::M.px(32), theme::M.px(20)},
                           T(Str::DetailNotes));
        c.textBlock(Rect{notes.x + theme::M.px(16), notes.y + theme::M.px(34),
                         notes.w - theme::M.px(32), notesH - theme::M.px(46)},
                    cfg.notes, theme::TextSecondary, theme::fontBody(),
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
        y += notesH + theme::M.gap;
    }

    contentScrollMax_ = std::max(0, y + contentScroll_ - area.bottom() + theme::M.px(16));
}

void App::paintRunning(Canvas& c, const Rect& area, const store::Config& cfg) {
    // Main 3-2: live CPU / GPU / memory, with graphs.
    int pad = theme::M.px(24);
    int x = area.x + pad;
    int w = area.w - pad * 2;
    int y = area.y + pad - contentScroll_;

    bool running = processAlive();
    bool ready = running && server_.ready();
    int uptime = runStarted_ ? (int)(util::nowSeconds() - runStarted_) : 0;

    // ---- header ----
    Rect header{x, y, w, theme::M.px(88)};
    shell::card(c, header);

    Rect tile{header.x + theme::M.px(18), header.y + theme::M.px(20), theme::M.px(44),
              theme::M.px(44)};
    c.fillRound(tile, theme::M.radiusMedium, running ? theme::AccentSoft : theme::Border);
    c.glyph(tile, shell::glyphs::kGauge, running ? theme::Accent : theme::TextSecondary, 20);

    Rect title{tile.right() + theme::M.px(14), header.y + theme::M.px(18),
               header.w - tile.w - theme::M.px(330), theme::M.px(24)};
    c.text(title, util::ellipsize(c.dc(), cfg.name, title.w), theme::TextPrimary,
           theme::fontSubtitleBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    Rect state{title.x, title.bottom() + theme::M.px(2), title.w, theme::M.px(20)};
    std::wstring stateText;
    COLORREF stateColor = theme::TextTertiary;
    if (!running) {
        stateText = T(Str::ServerStopped);
        if (lastExitCode_ > 0)
            stateText += util::format(L" (%s %d)", T(Str::ServerFailed), lastExitCode_);
    } else if (ready) {
        stateText = T(Str::ServerReady);
        stateColor = theme::Success;
    } else {
        stateText = T(Str::ServerStarting);
        stateColor = theme::Warning;
    }
    c.circle(state.x + theme::M.px(4), state.y + theme::M.px(10), theme::M.px(3),
             running ? (ready ? theme::Success : theme::Warning) : theme::TextTertiary);
    c.text(Rect{state.x + theme::M.px(12), state.y, state.w, state.h},
           util::ellipsize(c.dc(), stateText, state.w), stateColor, theme::fontCaption(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // port + uptime chips
    int port = store::configPort(cfg);
    std::wstring info = util::format(L"%s %d    %s %s", T(Str::Port), port, T(Str::Uptime),
                                     util::formatDuration(uptime).c_str());
    Rect infoRect{header.right() - theme::M.px(300), header.y + theme::M.px(16),
                  theme::M.px(180), theme::M.px(20)};
    c.text(infoRect, info, theme::TextTertiary, theme::fontCaption(),
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // ---- actions ----
    int bh = theme::M.px(34);
    Rect chatBtn{header.right() - theme::M.px(18) - theme::M.px(132),
                 header.bottom() - theme::M.px(18) - bh, theme::M.px(132), bh};
    Rect stopBtn{chatBtn.x - theme::M.px(10) - theme::M.px(96), chatBtn.y, theme::M.px(96), bh};
    Rect logBtn{stopBtn.x - theme::M.px(10) - theme::M.px(96), chatBtn.y, theme::M.px(96), bh};

    {
        int idx = (int)hits_.size();
        addHit(logBtn, Action::OpenLog);
        shell::button(c, logBtn, T(Str::RunLog), shell::ButtonStyle::Secondary,
                      idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kLog);
    }
    if (running) {
        int idx = (int)hits_.size();
        addHit(stopBtn, Action::Stop, cfg.id);
        shell::button(c, stopBtn, T(Str::StopServer), shell::ButtonStyle::Danger,
                      idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kStop);
    }
    {
        int idx = (int)hits_.size();
        addHit(chatBtn, Action::OpenChatPage, cfg.id, ready);
        shell::button(c, chatBtn, T(Str::OpenInBrowser), shell::ButtonStyle::Primary,
                      ready && idx == hoverIndex_, ready && idx == pressIndex_, false,
                      shell::glyphs::kGlobe);
    }

    y = header.bottom() + theme::M.gapLarge;

    // ---- metric tiles ----
    const monitor::MemoryInfo& mem = monitor_.memory();
    const monitor::GpuInfo& gpu = monitor_.gpu();
    int tiles = 3;
    int gap = theme::M.gap;
    int tileW = (w - gap * (tiles - 1)) / tiles;
    int tileH = theme::M.px(92);

    {
        Rect r{x, y, tileW, tileH};
        double f = monitor_.cpuPercent() / 100.0;
        std::wstring primary = util::format(L"%d%%", monitor_.cpuPercent());
        std::wstring secondary =
            monitor_.processCpuPercent() ? util::format(L"%d%% · %s", monitor_.processCpuPercent(),
                                                        T(Str::MetricThreads))
                                         : std::wstring(T(Str::NotAvailable));
        shell::metricTile(c, r, T(Str::MetricCpu), primary, secondary, f, kCpuColor,
                          shell::glyphs::kChip);
    }
    {
        Rect r{x + tileW + gap, y, tileW, tileH};
        double f = gpu.hasEngineCounter ? monitor_.gpuPercent() / 100.0
                                        : (gpu.vramValid ? gpu.vramPercent / 100.0 : 0.0);
        std::wstring primary = gpu.hasEngineCounter ? util::format(L"%d%%", monitor_.gpuPercent())
                                                   : std::wstring(T(Str::NotAvailable));
        std::wstring secondary;
        if (gpu.vramValid)
            secondary = util::format(L"%s / %s", util::humanBytes(gpu.vramUsed).c_str(),
                                     util::humanBytes(gpu.vramTotal).c_str());
        else if (!gpu.name.empty())
            secondary = gpu.name;
        else
            secondary = T(Str::NotAvailable);
        shell::metricTile(c, r, T(Str::MetricGpu), primary, secondary, f, kGpuColor,
                          shell::glyphs::kGauge);
    }
    {
        Rect r{x + (tileW + gap) * 2, y, tileW, tileH};
        double f = mem.percent / 100.0;
        std::wstring primary = util::format(L"%u%%", mem.percent);
        std::wstring secondary = util::format(L"%s / %s", util::humanBytes(mem.used).c_str(),
                                              util::humanBytes(mem.total).c_str());
        shell::metricTile(c, r, T(Str::MetricMemory), primary, secondary, f, kMemColor,
                          shell::glyphs::kMemoryStick);
    }
    y += tileH + theme::M.gapLarge;

    // ---- sparkline charts ----
    Rect chartCard{x, y, w, theme::M.px(190)};
    shell::card(c, chartCard);

    int cx = chartCard.x + theme::M.px(18);
    int cw = chartCard.w - theme::M.px(36);
    int chartH = theme::M.px(62);
    int cy = chartCard.y + theme::M.px(38);

    auto chartRow = [&](const std::wstring& label, const std::deque<float>& history, COLORREF accent,
                        const std::wstring& value, wchar_t glyph) {
        Rect head{cx, cy - theme::M.px(20), cw, theme::M.px(18)};
        if (glyph)
            c.glyph(Rect{head.x, head.y, theme::M.px(15), head.h}, glyph, accent, 12);
        c.text(Rect{head.x + (glyph ? theme::M.px(19) : 0), head.y, head.w, head.h}, label,
               theme::TextSecondary, theme::fontSmall(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        c.text(head, value, theme::TextPrimary, theme::fontBodyBold(),
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect plot{cx, cy, cw, chartH};
        c.fillRound(plot, theme::M.radiusSmall, RGB(250, 250, 250));
        // Baseline grid at 50% so the reader can judge magnitude quickly.
        c.line(plot.x, plot.cy(), plot.right(), plot.cy(), theme::ChartGrid);
        std::vector<float> samples(history.begin(), history.end());
        COLORREF fill = theme::blend(accent, RGB(255, 255, 255), 210);
        c.sparkline(plot.inset(1), samples, accent, fill);
        cy += chartH + theme::M.px(34);
    };

    chartRow(T(Str::MetricCpu), monitor_.cpuHistory(), kCpuColor,
             util::format(L"%d%%", monitor_.cpuPercent()), shell::glyphs::kChip);
    chartRow(T(Str::MetricGpu), monitor_.gpuHistory(), kGpuColor,
             gpu.hasEngineCounter ? util::format(L"%d%%", monitor_.gpuPercent())
                                  : std::wstring(T(Str::NotAvailable)),
             shell::glyphs::kGauge);

    y = chartCard.bottom() + theme::M.gap;

    // ---- process footprint rows ----
    Rect procCard{x, y, w, theme::M.px(122)};
    shell::card(c, procCard);
    shell::sectionTitle(c, Rect{procCard.x + theme::M.px(16), procCard.y + theme::M.px(10),
                                procCard.w - theme::M.px(32), theme::M.px(18)},
                       T(Str::MetricMemory));

    int ry = procCard.y + theme::M.px(36);
    int rowH = theme::M.px(26);
    double memFrac = mem.total ? (double)mem.used / (double)mem.total : 0.0;
    shell::meterRow(c, Rect{procCard.x + theme::M.px(16), ry, procCard.w - theme::M.px(32), rowH},
                    T(Str::MetricMemory),
                    util::format(L"%s / %s", util::humanBytes(mem.used).c_str(),
                                 util::humanBytes(mem.total).c_str()),
                    memFrac, kMemColor, shell::glyphs::kMemoryStick);
    ry += rowH + theme::M.px(6);

    double vramFrac = gpu.vramTotal ? (double)gpu.vramUsed / (double)gpu.vramTotal : 0.0;
    shell::meterRow(c, Rect{procCard.x + theme::M.px(16), ry, procCard.w - theme::M.px(32), rowH},
                    T(Str::MetricVram),
                    gpu.vramValid
                        ? util::format(L"%s / %s", util::humanBytes(gpu.vramUsed).c_str(),
                                       util::humanBytes(gpu.vramTotal).c_str())
                        : std::wstring(T(Str::NotAvailable)),
                    vramFrac, kGpuColor, shell::glyphs::kGauge);
    ry += rowH + theme::M.px(6);

    double wsFrac = mem.total ? (double)monitor_.processWorkingSet() / (double)mem.total : 0.0;
    shell::meterRow(c, Rect{procCard.x + theme::M.px(16), ry, procCard.w - theme::M.px(32), rowH},
                    T(Str::MetricModel),
                    util::humanBytes(monitor_.processWorkingSet()), wsFrac, kCpuColor,
                    shell::glyphs::kChip);

    y = procCard.bottom() + theme::M.gap;

    // ---- live log tail ----
    int logLines = 8;
    int logH = theme::M.px(34) + logLines * theme::M.px(18) + theme::M.px(12);
    Rect logCard{x, y, w, logH};
    shell::card(c, logCard);
    shell::sectionTitle(c, Rect{logCard.x + theme::M.px(16), logCard.y + theme::M.px(10),
                                logCard.w - theme::M.px(32), theme::M.px(18)},
                       T(Str::RunLog));

    Rect expandBtn{logCard.right() - theme::M.px(16) - theme::M.px(80), logCard.y + theme::M.px(8),
                   theme::M.px(80), theme::M.px(24)};
    {
        int idx = (int)hits_.size();
        addHit(expandBtn, Action::OpenLog);
        shell::button(c, expandBtn, T(Str::RunLog), shell::ButtonStyle::Subtle,
                      idx == hoverIndex_, idx == pressIndex_, false);
    }

    Rect logArea{logCard.x + theme::M.px(16), logCard.y + theme::M.px(34),
                 logCard.w - theme::M.px(32), logH - theme::M.px(46)};
    c.fillRound(logArea, theme::M.radiusSmall, RGB(249, 249, 249));
    Rect inner = logArea.inset(theme::M.px(8));
    int shown = 0;
    size_t total = logTail_.size();
    size_t start = total > (size_t)logLines ? total - (size_t)logLines : 0;
    for (size_t i = start; i < total && shown < logLines; ++i, ++shown) {
        Rect lr{inner.x, inner.y + shown * theme::M.px(18), inner.w, theme::M.px(18)};
        std::wstring line = logTail_[i];
        // Strip the leading timestamp the reader thread may have added so the
        // tail stays readable in the narrow column.
        c.text(lr, util::ellipsize(c.dc(), line, lr.w), theme::TextSecondary, theme::fontMono(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    if (total == 0) {
        c.text(inner, T(Str::NotAvailable), theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    y = logCard.bottom() + theme::M.gap;

    contentScrollMax_ = std::max(0, y + contentScroll_ - area.bottom() + theme::M.px(16));
}

// ------------------------------------------------------------------- chat --
void App::paintChat(Canvas& c, const Frame& f) {
    // Main 3-3. Only the strip above the browser is ours to draw: the page area
    // is covered by the WebView2 child window. Anything still visible there
    // means the browser has not painted yet, or could not start at all.
    Rect bar = f.chatBar;
    c.fill(bar, theme::TopBarBg);
    c.line(bar.x, bar.bottom() - 1, bar.right(), bar.bottom() - 1, theme::Border);

    int pad = theme::M.px(12);
    int bh = bar.h - pad * 2;
    int x = bar.x + pad;

    Rect backBtn{x, bar.y + pad, theme::M.px(34), bh};
    int idx = (int)hits_.size();
    addHit(backBtn, Action::CloseChat);
    shell::iconButton(c, backBtn, shell::glyphs::kBack, idx == hoverIndex_, idx == pressIndex_,
                      false);
    x = backBtn.right() + theme::M.px(8);

    Rect reloadBtn{x, bar.y + pad, theme::M.px(34), bh};
    idx = (int)hits_.size();
    addHit(reloadBtn, Action::ReloadChat);
    shell::iconButton(c, reloadBtn, shell::glyphs::kRefresh, idx == hoverIndex_,
                      idx == pressIndex_, false);
    x = reloadBtn.right() + theme::M.px(6);

    // Address pill: the embedded page is always the local server, so showing the
    // origin is enough and doubles as a hint that the server must be running.
    std::wstring url = util::format(L"127.0.0.1:%d", chatPort());
    Rect urlBox{x, bar.y + pad + theme::M.px(3), theme::M.px(150), bh - theme::M.px(6)};
    c.fillRound(urlBox, theme::M.radiusSmall, theme::CardBg);
    c.strokeRound(urlBox, theme::M.radiusSmall, theme::Border);
    c.glyph(Rect{urlBox.x + theme::M.px(7), urlBox.y, theme::M.px(14), urlBox.h},
            shell::glyphs::kGlobe, theme::TextTertiary, 11);
    c.text(Rect{urlBox.x + theme::M.px(25), urlBox.y, urlBox.w - theme::M.px(31), urlBox.h}, url,
           theme::TextSecondary, theme::fontCaption(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    x = urlBox.right() + theme::M.px(10);

    Rect openBtn{bar.right() - pad - theme::M.px(150), bar.y + pad, theme::M.px(150), bh};
    idx = (int)hits_.size();
    addHit(openBtn, Action::OpenInBrowser);
    shell::button(c, openBtn, T(Str::OpenInBrowser), shell::ButtonStyle::Secondary,
                  idx == hoverIndex_, idx == pressIndex_, false, shell::glyphs::kGlobe);

    // Status line between the pill and the button.
    std::wstring status;
    COLORREF statusColor = theme::TextTertiary;
    if (webView_.failed()) {
        status = webView_.lastError() != S_OK
                     ? util::format(L"%s (0x%08X)", T(Str::WebViewFailed),
                                    (unsigned)webView_.lastError())
                     : std::wstring(T(Str::WebViewFailed));
        statusColor = theme::Warning;
    } else if (webView_.pending()) {
        status = T(Str::WebViewLoading);
    } else if (!processAlive()) {
        status = T(Str::StatusLlamaMissing);
        statusColor = theme::TextTertiary;
    } else {
        status = T(Str::ServerReady);
        statusColor = theme::Success;
    }
    Rect statusRect{x, bar.y, std::max(theme::M.px(60), openBtn.x - x - theme::M.px(10)), bar.h};
    c.text(statusRect, status, statusColor, theme::fontCaption(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Backdrop for the page area while the browser is still starting.
    if (!webView_.ready()) {
        c.fill(f.chatPage, theme::LayerBg);
        if (webView_.failed()) {
            Rect msg{f.chatPage.x + theme::M.px(24), f.chatPage.y + theme::M.px(24),
                     f.chatPage.w - theme::M.px(48), theme::M.px(60)};
            c.textBlock(msg, T(Str::WebViewFailed), theme::Warning, theme::fontBody());
        }
    }
}

// --------------------------------------------------------------- bottom bar --
void App::paintBottomBar(Canvas& c, const Frame& f) {
    // Main-4: a status strip with the essentials.
    c.fill(f.bottomBar, theme::BottomBarBg);
    c.line(0, f.bottomBar.top(), f.bottomBar.w, f.bottomBar.top(), theme::Border);

    bool running = processAlive();
    bool llamaReady = !store_.serverExe().empty();

    std::wstring left;
    COLORREF leftColor = theme::TextTertiary;
    if (running) {
        int port = 8080;
        if (const store::Config* cfg = selected()) port = store::configPort(*cfg);
        left = util::format(L"%s %d", T(Str::StatusRunning), port);
        leftColor = theme::Success;
    } else if (!llamaReady) {
        left = T(Str::StatusLlamaMissing);
        leftColor = theme::Warning;
    } else {
        left = util::format(L"%s  ·  %s", T(Str::StatusLlamaReady),
                            util::ellipsize(c.dc(), store_.effectiveLlamaDir(), theme::M.px(320))
                                .c_str());
    }

    Rect statusRect{theme::M.padding, f.bottomBar.y, f.bottomBar.w / 2, f.bottomBar.h};
    c.circle(statusRect.x + theme::M.px(4), statusRect.cy(), theme::M.px(3), leftColor);
    c.text(Rect{statusRect.x + theme::M.px(12), statusRect.y, statusRect.w, statusRect.h}, left,
           leftColor, theme::fontCaption(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

    // Backup indicator on the right: reassures the user their configs are safe.
    if (store_.settings().backupEnabled) {
        Rect backupRect{f.bottomBar.right() - theme::M.px(260), f.bottomBar.y, theme::M.px(210),
                        f.bottomBar.h};
        c.glyph(Rect{backupRect.x, backupRect.y, theme::M.px(14), backupRect.h},
                shell::glyphs::kCheck, theme::Success, 11);
        c.text(Rect{backupRect.x + theme::M.px(18), backupRect.y, backupRect.w, backupRect.h},
               T(Str::AutoBackupOn), theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // Version mark, right aligned.
    Rect versionRect{f.bottomBar.right() - theme::M.px(56), f.bottomBar.y, theme::M.px(44),
                     f.bottomBar.h};
    c.text(versionRect, L"v1.0", theme::TextTertiary, theme::fontCaption(),
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Transient toast (for example "copied to clipboard").
    if (!toast_.empty() && util::nowSeconds() <= toastUntil_) {
        int tw = c.textWidth(toast_, theme::fontBody()) + theme::M.px(28);
        Rect toastRect{f.bottomBar.cx() - tw / 2, f.bottomBar.y - theme::M.px(44), tw,
                       theme::M.px(32)};
        c.fillRound(toastRect, theme::M.radiusMedium, theme::TextPrimary);
        c.text(toastRect, toast_, RGB(255, 255, 255), theme::fontBody(),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// -------------------------------------------------------------------- paint --
void App::paint(HDC target, const Rect& client) {
    // Everything is drawn into an off-screen bitmap first. That removes all
    // flicker from the live-updating running view.
    HDC mem = ::CreateCompatibleDC(target);
    HBITMAP bmp = ::CreateCompatibleBitmap(target, std::max(1, client.w), std::max(1, client.h));
    HGDIOBJ oldBmp = ::SelectObject(mem, bmp);

    Canvas c(mem);
    kCpuColor = theme::ChartCpu;
    kGpuColor = theme::ChartGpu;
    kMemColor = theme::ChartMem;

    hits_.clear();
    hits_.reserve(64);

    Frame f = layout(client);
    paintContent(c, f);      // painted first so the bars sit on top at the edges
    paintSidebar(c, f);
    paintTopBar(c, f);
    paintBottomBar(c, f);

    // The search box is a real child window; it is clipped out via
    // WS_CLIPCHILDREN, so nothing here needs to avoid it.

    ::BitBlt(target, 0, 0, client.w, client.h, mem, 0, 0, SRCCOPY);

    ::SelectObject(mem, oldBmp);
    ::DeleteObject(bmp);
    ::DeleteDC(mem);
}

}  // namespace app