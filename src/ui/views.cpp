// views.cpp - settings, confirmations and the file pickers.
//
// Dialogs are custom drawn (see ui/dialog.h) so they match the main window. Text
// entry uses real Win32 edit controls placed over the canvas, which keeps IME
// support - essential for entering Chinese - working for free.
//
// Layout uses shell::Layout, a simple vertical cursor, so the geometry is
// declared once and both painting and control placement read from it.
#include "ui/views.h"

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cmath>

#include "core/i18n.h"
#include "core/paths.h"
#include "core/process.h"
#include "core/util.h"
#include "ui/theme.h"

using shell::Canvas;
using shell::Layout;
using shell::Rect;
using ui::Dialog;
using ui::DialogResult;

namespace views {

// ============================================================================
//  Shared helpers
// ============================================================================
namespace {

HWND makeEdit(HWND parent, HFONT font, bool multiline = false, bool centered = false) {
    DWORD style = multiline
                      ? (WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN |
                         WS_VSCROLL)
                      : (WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL);
    // Parameter values read better centred: they are short tokens in a wide box.
    if (centered) style |= ES_CENTER;
    HWND h = ::CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 10, 10, parent, nullptr,
                               ::GetModuleHandleW(nullptr), nullptr);
    ::SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    ::SendMessageW(h, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                   MAKELPARAM(theme::M.px(7), theme::M.px(7)));
    if (multiline) ::SendMessageW(h, EM_SETLIMITTEXT, 8000, 0);
    // Register for DPI refonting: theme rebuilds its font handles when the
    // monitor changes, and a stale HFONT in an EDIT is undefined behaviour.
    ui::trackEditFont(h, font == theme::fontMono());
    return h;
}

std::wstring editText(HWND h) {
    if (!h) return {};
    int len = ::GetWindowTextLengthW(h);
    std::wstring s((size_t)len + 1, L'\0');
    ::GetWindowTextW(h, s.data(), len + 1);
    s.resize((size_t)len);
    return s;
}

// A Win11 style toggle switch.
void toggleSwitch(Canvas& c, const Rect& r, bool on, bool hovered, bool pressed) {
    int h = r.h;
    Rect track{r.x, r.cy() - h / 2, theme::M.px(40), h};
    COLORREF bg = on ? theme::Accent : theme::BorderStrong;
    if (pressed)
        bg = theme::blend(bg, on ? RGB(255, 255, 255) : RGB(0, 0, 0), theme::PressAlpha);
    else if (hovered)
        bg = theme::blend(bg, RGB(255, 255, 255), theme::HoverAlpha);
    c.fillRound(track, h / 2, bg);

    int knob = h - theme::M.px(6);
    int kx = on ? track.right() - theme::M.px(3) - knob : track.x + theme::M.px(3);
    c.circle(kx + knob / 2, track.cy(), knob / 2, RGB(255, 255, 255));
}

// Segmented control, used for the language choice.
void segmented(Canvas& c, const Rect& r, const std::vector<std::wstring>& options, int selected,
               int hoveredIndex, int pressedIndex) {
    // The track follows the palette; a fixed light grey left the segmented
    // controls (language, appearance) glaring in the dark theme.
    COLORREF track = theme::isDarkMode() ? theme::blend(theme::CardBg, RGB(255, 255, 255), 18)
                                         : RGB(237, 237, 237);
    c.fillRound(r, theme::M.radiusMedium, track);
    int segW = r.w / (int)options.size();
    for (size_t i = 0; i < options.size(); ++i) {
        Rect seg{r.x + (int)i * segW, r.y + theme::M.px(2), segW, r.h - theme::M.px(4)};
        if (i + 1 == options.size()) seg.w = r.right() - theme::M.px(2) - seg.x;
        int idx = (int)i;
        if (idx == selected) {
            c.fillRound(seg, theme::M.radiusSmall, theme::CardBg);
            c.strokeRound(seg, theme::M.radiusSmall, theme::Border);
        } else if (idx == pressedIndex) {
            c.fillRound(seg, theme::M.radiusSmall,
                        theme::blend(track, RGB(0, 0, 0), theme::PressAlpha));
        } else if (idx == hoveredIndex) {
            c.fillRound(seg, theme::M.radiusSmall, theme::blend(track, RGB(255, 255, 255), 160));
        }
        c.text(seg, options[i], idx == selected ? theme::TextPrimary : theme::TextSecondary,
               idx == selected ? theme::fontBodyBold() : theme::fontBody(),
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// Draws the shared chrome: the title band and the footer buttons. Returns the
// rect the body may use.
Rect headerAndFooter(Canvas& c, Dialog& dlg, const Rect& client, const std::wstring& title,
                     const std::wstring& okLabel, bool* okHitOut, bool* cancelHitOut) {
    (void)dlg;
    Rect header{0, 0, client.w, theme::M.px(56)};
    c.fill(header, theme::LayerBg);
    c.line(0, header.bottom() - 1, client.w, header.bottom() - 1, theme::Border);
    c.text(Rect{theme::M.px(22), 0, client.w - theme::M.px(60), header.h}, title,
           theme::TextPrimary, theme::fontSubtitleBold(),
           DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    int bh = theme::M.px(34);
    int bw = theme::M.px(100);
    Rect ok{client.w - theme::M.px(22) - bw, client.h - theme::M.px(20) - bh, bw, bh};
    Rect cancel{ok.x - theme::M.px(10) - bw, ok.y, bw, bh};
    if (okHitOut) *okHitOut = true;
    if (cancelHitOut) *cancelHitOut = true;
    return Rect{0, header.bottom(), client.w, ok.y - header.bottom()};
}

}  // namespace

// ============================================================================
//  Confirmation / message
// ============================================================================
namespace {

class MessageDialog : public Dialog {
public:
    MessageDialog(bool ask, const std::wstring& title, const std::wstring& okLabel, bool danger,
                  const std::wstring& body)
        : title_(title), body_(body), ask_(ask), okLabel_(okLabel), danger_(danger) {}

protected:
    void onPaint(Canvas& c, const Rect& client) override {
        bool okHit = false, cancelHit = false;
        Rect body = headerAndFooter(c, *this, client, title_, okLabel_, &okHit, &cancelHit);
        (void)body;

        int pad = theme::M.px(22);
        Rect text{pad, theme::M.px(56) + theme::M.px(20), client.w - pad * 2,
                  client.h - theme::M.px(56) - theme::M.px(96)};
        c.textBlock(text, body_, theme::TextSecondary, theme::fontBody(),
                    DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

        int bh = theme::M.px(34);
        int bw = theme::M.px(100);
        Rect ok{client.w - pad - bw, client.h - theme::M.px(20) - bh, bw, bh};
        addHit(ok, ID_OK);
        shell::button(c, ok, okLabel_,
                      danger_ ? shell::ButtonStyle::Danger : shell::ButtonStyle::Primary,
                      isHovered(ID_OK), isPressed(ID_OK), false);
        if (ask_) {
            Rect cancel{ok.x - theme::M.px(10) - bw, ok.y, bw, bh};
            addHit(cancel, ID_CANCEL);
            shell::button(c, cancel, T(Str::Cancel), shell::ButtonStyle::Secondary,
                          isHovered(ID_CANCEL), isPressed(ID_CANCEL), false);
        }
    }

    void setTitle(const std::wstring& t) { title_ = t; }

    void onClick(int id) override {
        if (id == ID_OK) close(DialogResult::Ok);
        else if (id == ID_CANCEL) close(DialogResult::Cancel);
    }

    bool onKeyDown(WPARAM key) override {
        if (key == VK_RETURN) {
            close(DialogResult::Ok);
            return true;
        }
        return false;
    }

private:
    std::wstring title_;
    std::wstring body_;
    bool ask_ = false;
    std::wstring okLabel_;
    bool danger_ = false;
};

}  // namespace

bool confirm(HWND owner, const std::wstring& title, const std::wstring& body,
             const std::wstring& okLabel, bool danger) {
    MessageDialog dlg(true, title, okLabel, danger, body);
    return dlg.run(owner, title, theme::M.px(470), theme::M.px(250));
}

void message(HWND owner, const std::wstring& title, const std::wstring& body) {
    MessageDialog dlg(false, title, T(Str::Ok), false, body);
    dlg.run(owner, title, theme::M.px(470), theme::M.px(230));
}

// ============================================================================
//  File / folder pickers
// ============================================================================
bool pickFile(HWND owner, const std::wstring& title, const std::wstring& filterText,
              const std::wstring& filterPattern, const std::wstring& defaultExt,
              std::wstring& out) {
    wchar_t buffer[MAX_PATH * 4]{};
    if (!out.empty()) wcsncpy_s(buffer, out.c_str(), _TRUNCATE);

    // The filter is a double-null terminated list of "label\0pattern\0" pairs.
    std::wstring filter;
    filter += filterText.empty() ? std::wstring(L"All files") : filterText;
    filter.push_back(L'\0');
    filter += filterPattern.empty() ? std::wstring(L"*.*") : filterPattern;
    filter.push_back(L'\0');
    if (!filterPattern.empty() && filterPattern != L"*.*") {
        filter += L"All files";
        filter.push_back(L'\0');
        filter += L"*.*";
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');

    // Open in the folder the current value already points at, so re-picking a
    // model does not start from some arbitrary working directory.
    std::wstring initialDir = out.empty() ? std::wstring() : util::parentDir(out);

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = (DWORD)std::size(buffer);
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrDefExt = defaultExt.empty() ? nullptr : defaultExt.c_str();
    ofn.lpstrInitialDir = initialDir.empty() ? nullptr : initialDir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!::GetOpenFileNameW(&ofn)) return false;
    out = buffer;
    return true;
}

std::vector<std::wstring> pickFiles(HWND owner, const std::wstring& title,
                                    const std::wstring& filterText,
                                    const std::wstring& filterPattern) {
    std::vector<std::wstring> out;
    // The multi-select form needs a generously sized buffer.
    std::vector<wchar_t> buffer(64 * 1024, L'\0');

    std::wstring filter;
    filter += filterText.empty() ? std::wstring(L"YAML") : filterText;
    filter.push_back(L'\0');
    filter += filterPattern.empty() ? std::wstring(L"*.yaml") : filterPattern;
    filter.push_back(L'\0');
    filter += L"All files";
    filter.push_back(L'\0');
    filter += L"*.*";
    filter.push_back(L'\0');
    filter.push_back(L'\0');

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = (DWORD)buffer.size();
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                OFN_ALLOWMULTISELECT;

    if (!::GetOpenFileNameW(&ofn)) return out;

    // Multi-select returns "folder\0file1\0file2\0"; a single pick returns one
    // full path, which is distinguished by an immediate terminator.
    const wchar_t* p = buffer.data();
    std::wstring first = p;
    p += first.size() + 1;
    if (*p == L'\0') {
        out.push_back(first);
        return out;
    }
    for (; *p; p += wcslen(p) + 1) out.push_back(util::joinPath(first, p));
    return out;
}

bool pickFolder(HWND owner, const std::wstring& title, std::wstring& out) {
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                     IID_PPV_ARGS(&dialog))) &&
        dialog) {
        DWORD opts = 0;
        dialog->GetOptions(&opts);
        dialog->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        dialog->SetTitle(title.c_str());
        bool ok = false;
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item)) && item) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                    out = path;
                    ::CoTaskMemFree(path);
                    ok = true;
                }
                item->Release();
            }
        }
        dialog->Release();
        return ok;
    }

    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.lpszTitle = title.c_str();
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    LPITEMIDLIST idl = ::SHBrowseForFolderW(&bi);
    if (!idl) return false;
    wchar_t path[MAX_PATH]{};
    bool ok = ::SHGetPathFromIDListW(idl, path) != FALSE;
    ::CoTaskMemFree(idl);
    if (ok) out = path;
    return ok;
}

// ============================================================================
//  Settings (sub-view 1)
// ============================================================================
namespace {

class SettingsDialog : public Dialog {
public:
    explicit SettingsDialog(store::Store& store) : store_(store) {
        language_ = util::iequals(store.settings().language, L"en") ? Lang::En : Lang::Zh;
        themeMode_ = theme::modeFromSetting(store.settings().theme);
        llamaDir_ = store.settings().llamaDir;
        backupEnabled_ = store.settings().backupEnabled;
        backupDir_ = store.settings().backupDir.empty() ? paths::backupsDir()
                                                        : store.settings().backupDir;
    }

protected:
    // ---- geometry, shared by painting and control placement ----
    int bodyTop() const { return theme::M.px(56) + theme::M.px(16); }
    int left() const { return theme::M.px(24); }
    int width() const { return clientRect().w - theme::M.px(48); }

    // The general section is two stacked rows - language, then appearance - each
    // with its label above its control. Cramming both onto one line pushed the
    // appearance control past the right edge and hid the language label behind
    // its own segmented control.
    Rect languageSeg() const {
        return Rect{left(), bodyTop() + theme::M.px(52), theme::M.px(240), theme::M.px(32)};
    }
    Rect themeSeg() const {
        return Rect{left(), bodyTop() + theme::M.px(118), theme::M.px(240), theme::M.px(32)};
    }
    Rect llamaField() const {
        int w = width() - theme::M.px(118);
        return Rect{left(), bodyTop() + theme::M.px(232), w, theme::M.px(30)};
    }
    Rect backupField() const {
        // Sits just under the enable switch. It used to be pinned far below it,
        // which left a dead band in the middle of the backup section.
        int w = width() - theme::M.px(118);
        return Rect{left(), bodyTop() + theme::M.px(400), w, theme::M.px(30)};
    }

    void onLayout() override {
        // Controls are created once; later calls (resize, DPI change) only
        // reposition the ones that already exist.
        if (!llamaEdit_) {
            llamaEdit_ = makeEdit(hwnd(), theme::fontBody());
            ::SetWindowTextW(llamaEdit_, llamaDir_.c_str());
        }
        if (!backupEdit_) {
            backupEdit_ = makeEdit(hwnd(), theme::fontBody());
            ::SetWindowTextW(backupEdit_, backupDir_.c_str());
        }
        placeControls();
    }

    void onDestroy() override { llamaEdit_ = backupEdit_ = nullptr; }

    void placeControls() {
        Rect l = llamaField().inset(theme::M.px(1));
        if (llamaEdit_)
            ::SetWindowPos(llamaEdit_, nullptr, l.x, l.y, l.w, l.h, SWP_NOZORDER | SWP_NOACTIVATE);
        Rect b = backupField().inset(theme::M.px(1));
        if (backupEdit_)
            ::SetWindowPos(backupEdit_, nullptr, b.x, b.y, b.w, b.h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void onPaint(Canvas& c, const Rect& client) override {
        int x = left();
        int w = width();

        // ---- chrome ----
        Rect header{0, 0, client.w, theme::M.px(56)};
        c.fill(header, theme::LayerBg);
        c.line(0, header.bottom() - 1, client.w, header.bottom() - 1, theme::Border);
        c.text(Rect{theme::M.px(22), 0, client.w - theme::M.px(60), header.h}, T(Str::Settings),
               theme::TextPrimary, theme::fontSubtitleBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        int y = bodyTop();

        // ---- general ----
        c.text(Rect{x, y, w, theme::M.px(22)}, T(Str::SettingsGeneral), theme::TextPrimary,
               theme::fontBodyBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        // Row 1: language.
        c.text(Rect{x, y + theme::M.px(28), w, theme::M.px(18)}, T(Str::Language),
               theme::TextSecondary, theme::fontSmall(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect seg = languageSeg();
        addHit(Rect{seg.x, seg.y, seg.w / 2, seg.h}, ID_LANG_ZH);
        addHit(Rect{seg.x + seg.w / 2, seg.y, seg.w - seg.w / 2, seg.h}, ID_LANG_EN);
        segmented(c, seg, {T(Str::LangZh), T(Str::LangEn)}, language_ == Lang::Zh ? 0 : 1,
                  isHovered(ID_LANG_ZH) ? 0 : (isHovered(ID_LANG_EN) ? 1 : -1),
                  isPressed(ID_LANG_ZH) ? 0 : (isPressed(ID_LANG_EN) ? 1 : -1));

        // Row 2: appearance, on its own line so all three options fit.
        c.text(Rect{x, y + theme::M.px(94), w, theme::M.px(18)}, T(Str::ThemeMode),
               theme::TextSecondary, theme::fontSmall(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect tseg = themeSeg();
        int third = tseg.w / 3;
        addHit(Rect{tseg.x, tseg.y, third, tseg.h}, ID_THEME_SYSTEM);
        addHit(Rect{tseg.x + third, tseg.y, third, tseg.h}, ID_THEME_LIGHT);
        addHit(Rect{tseg.x + third * 2, tseg.y, tseg.w - third * 2, tseg.h}, ID_THEME_DARK);
        segmented(c, tseg, {T(Str::ThemeSystem), T(Str::ThemeLight), T(Str::ThemeDark)},
                  (int)themeMode_, themeHover(), themePress());

        int divY = y + theme::M.px(162);
        c.line(x, divY, x + w, divY, theme::Divider);

        // ---- llama.cpp location ----
        y = divY + theme::M.px(16);
        c.text(Rect{x, y, w, theme::M.px(22)}, T(Str::SettingsLlama), theme::TextPrimary,
               theme::fontBodyBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        c.text(Rect{x, y + theme::M.px(24), w, theme::M.px(18)}, T(Str::LlamaPathHint),
               theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // The child edit sits inside this frame.
        Rect field = llamaField();
        c.fillRound(field, theme::M.radiusSmall, theme::CardBg);
        c.strokeRound(field, theme::M.radiusSmall, theme::BorderStrong);

        Rect detectBtn{field.right() + theme::M.px(8), field.y, theme::M.px(110), field.h};
        addHit(detectBtn, ID_AUTODETECT);
        shell::button(c, detectBtn, T(Str::AutoDetect), shell::ButtonStyle::Secondary,
                      isHovered(ID_AUTODETECT), isPressed(ID_AUTODETECT), false,
                      shell::glyphs::kSearch);

        Rect browseBtn{detectBtn.x, detectBtn.bottom() + theme::M.px(8), theme::M.px(110),
                       field.h};
        addHit(browseBtn, ID_BROWSE_FOLDER);
        shell::button(c, browseBtn, T(Str::Browse), shell::ButtonStyle::Secondary,
                      isHovered(ID_BROWSE_FOLDER), isPressed(ID_BROWSE_FOLDER), false,
                      shell::glyphs::kFolder);

        Rect status{field.x, field.bottom() + theme::M.px(10), field.w + theme::M.px(118),
                    theme::M.px(20)};
        if (!statusText_.empty()) {
            COLORREF col = statusOk_ ? theme::Success : theme::Warning;
            c.glyph(Rect{status.x, status.y, theme::M.px(14), status.h},
                    statusOk_ ? shell::glyphs::kCheck : shell::glyphs::kInfo, col, 11);
            c.text(Rect{status.x + theme::M.px(18), status.y, status.w, status.h}, statusText_, col,
                   theme::fontCaption(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        } else {
            // No explicit status yet: report whether the resolved path actually
            // contains llama-server.exe, with the same check/info glyph the
            // explicit status uses, so the two states read the same way.
            std::wstring exe = store_.serverExe();
            COLORREF col = exe.empty() ? theme::Warning : theme::Success;
            c.glyph(Rect{status.x, status.y, theme::M.px(14), status.h},
                    exe.empty() ? shell::glyphs::kInfo : shell::glyphs::kCheck, col, 11);
            c.text(Rect{status.x + theme::M.px(18), status.y, status.w - theme::M.px(18),
                        status.h},
                   exe.empty() ? std::wstring(T(Str::NotAvailable))
                               : std::wstring(T(Str::StatusLlamaReady)),
                   col, theme::fontCaption(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        divY = status.bottom() + theme::M.px(14);
        c.line(x, divY, x + w, divY, theme::Divider);

        // ---- backup ----
        y = divY + theme::M.px(16);
        c.text(Rect{x, y, w, theme::M.px(22)}, T(Str::SettingsBackup), theme::TextPrimary,
               theme::fontBodyBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect switchRow{x, y + theme::M.px(26), w, theme::M.px(22)};
        Rect switchRect{switchRow.x, switchRow.y, theme::M.px(40), switchRow.h};
        addHit(switchRect, ID_BACKUP_NOW);
        toggleSwitch(c, switchRect, backupEnabled_, isHovered(ID_BACKUP_NOW),
                     isPressed(ID_BACKUP_NOW));
        c.text(Rect{switchRect.right() + theme::M.px(10), switchRow.y, w - theme::M.px(60),
                    switchRow.h},
               T(Str::BackupEnabled), backupEnabled_ ? theme::TextPrimary : theme::TextSecondary,
               theme::fontBody(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect field2 = backupField();
        c.text(Rect{x, field2.y - theme::M.px(24), w, theme::M.px(18)}, T(Str::BackupDirHint),
               theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        COLORREF offField = theme::LayerBg;
        c.fillRound(field2, theme::M.radiusSmall, backupEnabled_ ? theme::CardBg : offField);
        c.strokeRound(field2, theme::M.radiusSmall, theme::BorderStrong);

        Rect bBrowse{field2.right() + theme::M.px(8), field2.y, theme::M.px(110), field2.h};
        addHit(bBrowse, ID_BROWSE_FILE, backupEnabled_);
        shell::button(c, bBrowse, T(Str::Browse), shell::ButtonStyle::Secondary,
                      backupEnabled_ && isHovered(ID_BROWSE_FILE),
                      backupEnabled_ && isPressed(ID_BROWSE_FILE), false, shell::glyphs::kFolder);

        Rect backupNow{field2.x, field2.bottom() + theme::M.px(12), theme::M.px(130),
                       theme::M.px(30)};
        addHit(backupNow, ID_BACKUP_ALL);
        shell::button(c, backupNow, T(Str::BackupNow), shell::ButtonStyle::Subtle,
                      isHovered(ID_BACKUP_ALL), isPressed(ID_BACKUP_ALL), false,
                      shell::glyphs::kExport);

        // Keep the folder visible but read-only when backups are off. Hiding it
        // outright left what looked like a broken, permanently empty field.
        if (backupEdit_) {
            ::ShowWindow(backupEdit_, SW_SHOW);
            ::EnableWindow(backupEdit_, backupEnabled_ ? TRUE : FALSE);
        }

        // ---- footer ----
        int bh = theme::M.px(34);
        int bw = theme::M.px(100);
        Rect ok{client.w - theme::M.px(24) - bw, client.h - theme::M.px(20) - bh, bw, bh};
        Rect cancel{ok.x - theme::M.px(10) - bw, ok.y, bw, bh};
        addHit(ok, ID_OK);
        addHit(cancel, ID_CANCEL);
        shell::button(c, ok, T(Str::Save), shell::ButtonStyle::Primary, isHovered(ID_OK),
                      isPressed(ID_OK), false);
        shell::button(c, cancel, T(Str::Cancel), shell::ButtonStyle::Secondary,
                      isHovered(ID_CANCEL), isPressed(ID_CANCEL), false);
    }

    void onClick(int id) override {
        switch (id) {
            case ID_LANG_ZH:
                language_ = Lang::Zh;
                i18n::set(language_);   // preview immediately
                break;
            case ID_LANG_EN:
                language_ = Lang::En;
                i18n::set(language_);
                break;

            // Theme: applied straight away so the dialog itself previews it.
            case ID_THEME_SYSTEM:
                themeMode_ = theme::ThemeMode::System;
                theme::setThemeMode(themeMode_);
                repaintForTheme();
                break;
            case ID_THEME_LIGHT:
                themeMode_ = theme::ThemeMode::Light;
                theme::setThemeMode(themeMode_);
                repaintForTheme();
                break;
            case ID_THEME_DARK:
                themeMode_ = theme::ThemeMode::Dark;
                theme::setThemeMode(themeMode_);
                repaintForTheme();
                break;

            case ID_AUTODETECT: {
                auto r = store_.autoDetectLlama();
                if (r.found) {
                    llamaDir_ = util::parentDir(r.exePath);
                    ::SetWindowTextW(llamaEdit_, llamaDir_.c_str());
                    statusOk_ = true;
                    statusText_ = util::format(L"%s: %s", T(Str::AutoDetected),
                                               util::fileName(r.exePath).c_str());
                } else {
                    statusOk_ = false;
                    statusText_ = T(Str::AutoDetectFailed);
                }
                break;
            }

            case ID_BROWSE_FOLDER: {
                std::wstring picked;
                if (pickFolder(hwnd(), T(Str::LlamaPath), picked)) {
                    llamaDir_ = picked;
                    ::SetWindowTextW(llamaEdit_, picked.c_str());
                    statusText_.clear();
                }
                break;
            }

            case ID_BROWSE_FILE: {
                std::wstring picked;
                if (pickFolder(hwnd(), T(Str::BackupDir), picked)) {
                    backupDir_ = picked;
                    ::SetWindowTextW(backupEdit_, picked.c_str());
                }
                break;
            }

            case ID_BACKUP_NOW:
                backupEnabled_ = !backupEnabled_;
                break;

            case ID_BACKUP_ALL: {
                readControls();
                store_.settings().backupEnabled = backupEnabled_;
                store_.settings().backupDir = backupDir_;
                int n = store_.backupAll();
                statusOk_ = n > 0;
                statusText_ = util::format(L"%s: %d", T(Str::BackupDone), n);
                break;
            }

            case ID_OK: {
                readControls();
                store_.settings().language = (language_ == Lang::En) ? L"en" : L"zh";
                store_.settings().theme = themeMode_ == theme::ThemeMode::Light ? L"light"
                                        : themeMode_ == theme::ThemeMode::Dark  ? L"dark"
                                                                                : L"system";
                store_.settings().backupEnabled = backupEnabled_;
                store_.settings().backupDir = backupDir_;
                store_.saveSettings();
                close(DialogResult::Ok);
                return;
            }

            case ID_CANCEL:
                // Roll back the live language preview.
                i18n::set(util::iequals(store_.settings().language, L"en") ? Lang::En : Lang::Zh);
                close(DialogResult::Cancel);
                return;

            default:
                break;
        }
        invalidate();
    }

    bool onKeyDown(WPARAM key) override {
        if (key == VK_RETURN) {
            readControls();
            onClick(ID_OK);
            return true;
        }
        return false;
    }

private:
    void readControls() {
        if (llamaEdit_) {
            llamaDir_ = util::trim(editText(llamaEdit_));
            store_.settings().llamaDir = llamaDir_;
            if (!llamaDir_.empty()) {
                std::wstring exe = util::joinPath(llamaDir_, L"llama-server.exe");
                store_.settings().llamaExe = util::fileExists(exe) ? exe : L"";
            } else {
                store_.settings().llamaExe.clear();
            }
        }
        if (backupEdit_) backupDir_ = util::trim(editText(backupEdit_));
    }

    store::Store& store_;
    Lang language_ = Lang::Zh;

    // Which of the three appearance options is hovered / pressed, or -1.
    int themeHover() const {
        if (isHovered(ID_THEME_SYSTEM)) return 0;
        if (isHovered(ID_THEME_LIGHT)) return 1;
        if (isHovered(ID_THEME_DARK)) return 2;
        return -1;
    }
    int themePress() const {
        if (isPressed(ID_THEME_SYSTEM)) return 0;
        if (isPressed(ID_THEME_LIGHT)) return 1;
        if (isPressed(ID_THEME_DARK)) return 2;
        return -1;
    }
    theme::ThemeMode themeMode_ = theme::ThemeMode::System;
    std::wstring llamaDir_;
    bool backupEnabled_ = true;
    std::wstring backupDir_;
    std::wstring statusText_;
    bool statusOk_ = false;
    HWND llamaEdit_ = nullptr;
    HWND backupEdit_ = nullptr;
};

}  // namespace

bool settingsDialog(HWND owner, store::Store& store, bool& languageChanged) {
    Lang before = i18n::current();
    SettingsDialog dlg(store);
    bool ok = dlg.run(owner, T(Str::Settings), theme::M.px(640), theme::M.px(580));
    languageChanged = i18n::current() != before;
    return ok;
}

// ============================================================================
//  Parameter editor (sub-views 2 and 3)
// ============================================================================
namespace {

// One row of the editor. Built-in rows show a locked flag (the flag column is
// drawn, never editable, which is what stops a typo from breaking the launch);
// custom rows get two text boxes so both the flag and the value can be typed.
// Tall enough that the value box inside is a comfortable single-line editor;
// the older 40 left it cramped and easy to mis-click.
constexpr int kRowH = 50;
constexpr int kGroupRowH = 38;
constexpr int kGroupW = 168;

// Placeholder left in the flag box of a row the user has just added. It reads
// as "type the flag here" and is never written to a configuration.
constexpr wchar_t kNewRowFlag[] = L"--";

// Appends one line to logs\save.log. A failed save is otherwise completely
// silent - the dialog simply stays open - so this is the only way to tell which
// step refused.

// The hover tip has to be a real window, not something the dialog paints.
// The value boxes are child EDITs, and GDI always draws child controls over
// whatever the parent painted, so a tip drawn in the dialog's paint pass ends up
// hidden behind them. A child window created after the editors sits above them.
const wchar_t* kTipClass = L"LlamaLauncherHoverTip";

void registerTipClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) -> LRESULT {
        auto* text = (const std::wstring*)::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (msg == WM_ERASEBKGND) return 1;
        if (msg == WM_PAINT) {
            PAINTSTRUCT ps{};
            HDC dc = ::BeginPaint(hwnd, &ps);
            RECT rc{};
            ::GetClientRect(hwnd, &rc);
            if (rc.right > 0 && rc.bottom > 0) {
                HDC mem = ::CreateCompatibleDC(dc);
                HBITMAP bmp = ::CreateCompatibleBitmap(dc, rc.right, rc.bottom);
                HGDIOBJ oldB = ::SelectObject(mem, bmp);
                shell::Canvas c(mem);
                int pad = theme::M.px(10);
                c.fillRound(Rect{0, 0, rc.right, rc.bottom}, theme::M.radiusLarge,
                            theme::CardBg);
                c.strokeRound(Rect{0, 0, rc.right, rc.bottom}, theme::M.radiusLarge,
                              theme::BorderStrong);
                if (text && !text->empty()) {
                    c.text(Rect{pad, pad, rc.right - pad * 2, rc.bottom - pad * 2}, *text,
                           theme::TextSecondary, theme::fontSmall(),
                           DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                }
                ::BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
                ::SelectObject(mem, oldB);
                ::DeleteObject(bmp);
                ::DeleteDC(mem);
            }
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    };
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kTipClass;
    ::RegisterClassExW(&wc);
    done = true;
}

class ParamEditorDialog : public Dialog {
public:
    // A single editable parameter row. Built-in rows carry a pointer to their
    // catalogue spec (which locks the flag text); custom rows leave it null and
    // keep whatever flag the user typed.
    struct Row {
        bool custom = false;
        const catalog::Spec* spec = nullptr;
        std::wstring flag;
        std::wstring value;
        std::wstring group;
        std::wstring desc;
        bool expanded = false;
        // Off means the row is left out of the launch command and its value box
        // is read-only, so a half-typed path can never reach llama-server.
        bool enabled = false;
    };

    // The child controls a row owns. Only the visible rows have them, which is
    // what keeps a long parameter list cheap.
    struct RowWidgets {
        HWND flag = nullptr;
        HWND value = nullptr;
        bool created = false;
    };

    ParamEditorDialog(store::Store& store, store::Config& config, bool isNew)
        : store_(store), config_(config), isNew_(isNew) {
        name_ = config.name;
        notes_ = config.notes;
        // Rebuild the row list from the config, preserving the catalogue order
        // and appending custom rows at the end.
        // The stored switch state, or - for a parameter the config never held -
        // simply "it has a value", which is the same rule a fresh config uses.
        auto enabledFor = [&](const std::wstring& flag, const std::wstring& value) {
            for (const store::Param& p : config.params)
                if (p.flag == flag) return p.enabled;
            return !util::trim(value).empty();
        };

        for (const auto& pv : catalog::defaultParams()) {
            Row r;
            r.custom = false;
            r.flag = pv.flag;
            r.spec = catalog::find(pv.flag);
            r.group = pv.group;
            r.value = config.value(pv.flag);
            if (r.value.empty() && r.spec && r.spec->defaultVal) r.value = r.spec->defaultVal;
            // The catalogue keeps its prose on the Spec, not on the value, so
            // the hover tooltip has to read it from there.
            if (r.spec) {
                const wchar_t* d =
                    i18n::current() == Lang::Zh ? r.spec->zhDesc : r.spec->enDesc;
                if (d && *d) r.desc = d;
            }
            r.enabled = enabledFor(pv.flag, r.value);
            // -m ships checked: every launch needs a model, and with the row
            // off, its browse button is disabled too, which made a fresh
            // config look broken. A config that explicitly stores the row
            // (unchecked) keeps the user's choice.
            if (pv.flag == L"-m") {
                bool stored = false;
                for (const store::Param& p : config.params)
                    if (p.flag == L"-m") { stored = true; break; }
                if (!stored) r.enabled = true;
            }
            rows_.push_back(std::move(r));
        }
        for (const store::Param& p : config.params) {
            if (!p.custom) continue;
            Row r;
            r.custom = true;
            r.flag = p.flag;
            r.value = p.value;
            r.group = p.group.empty() ? L"custom" : p.group;
            r.desc = p.desc;
            r.enabled = p.enabled;
            rows_.push_back(std::move(r));
        }
        // Group order for the rail: the catalogue groups plus any custom groups
        // the user invented, so grouping stays open ended.
        for (int g = 0; g < (int)catalog::Group::Count; ++g)
            groups_.push_back(catalog::groupKey((catalog::Group)g));

        // `visible_` mirrors `rows_` and is what the paint pass and the control
        // placement both read, so it must be sized before either runs. Leaving
        // it empty here used to make the first paint step off the end of the
        // vector.
        visible_.assign(rows_.size(), true);
        rowWidgets_.assign(rows_.size(), RowWidgets{});
    }

protected:
    // ------------------------------------------------------------------ geometry
    // The row/rail metrics are declared as 96-dpi design units (see kRowH
    // above); these wrappers expand them to the dialog's real DPI. Using the
    // raw constants directly left every row at 50 *physical* pixels, which on a
    // 175% display is two thirds of the height the rest of the UI assumes.
    int rowH() const { return theme::M.px(kRowH); }
    int groupRowH() const { return theme::M.px(kGroupRowH); }
    int groupW() const { return theme::M.px(kGroupW); }

    int contentTop() const { return theme::M.px(56) + theme::M.px(72); }
    int footerTop() const { return clientRect().h - theme::M.px(58); }

    // Inner text width of the preview box: the rows column minus the box
    // padding. Declared without rowsRect() because the row list's height
    // depends on the preview, not the other way round.
    int previewInnerW() const {
        int x = groupW() + theme::M.px(24);
        int w = (int)clientRect().w - x - theme::M.px(16);
        return std::max(0, w - theme::M.px(20));
    }

    // Lines the current command needs when wrapped into previewInnerW(). The
    // face is monospaced, so one width sample is enough. Cached because the
    // geometry below asks on every query; a negative cache means "measure".
    int previewLinesNeeded() const {
        if (previewLines_ < 0) {
            HDC screen = ::GetDC(nullptr);
            previewLines_ = countLines(screen);
            ::ReleaseDC(nullptr, screen);
        }
        return previewLines_;
    }
    int countLines(HDC dc) const {
        int maxW = previewInnerW();
        if (maxW <= 0 || previewText_.empty()) return 1;
        HGDIOBJ oldFont = ::SelectObject(dc, theme::fontMono());
        SIZE sz{};
        ::GetTextExtentPoint32W(dc, L"MM", 2, &sz);
        ::SelectObject(dc, oldFont);
        int charW = std::max(1, (int)(sz.cx / 2));
        int perLine = std::max(1, maxW / charW);
        return (int)((previewText_.size() + perLine - 1) / perLine);
    }

    // Height of caption + preview box that must fit between the row list and
    // the footer. The row list gives way when the command needs more lines,
    // so the preview never has to cut the command short.
    int previewBlockH() const {
        int lineH = theme::lineHeight(theme::fontMono());
        int n = std::max(1, previewLinesNeeded());
        // 24: caption band + gap under the rows; 16: box padding; 6: footer gap
        return theme::M.px(24) + n * lineH + theme::M.px(16) + theme::M.px(6);
    }
    int bodyBottom() const {
        int h = std::max(theme::M.px(96), previewBlockH());
        // Never squeeze the list below roughly three rows; an extreme command
        // then overflows the box and the wrap marks the cut with an ellipsis.
        int maxH = std::max(theme::M.px(96), footerTop() - contentTop() - rowH() * 3);
        return footerTop() - std::min(h, maxH);
    }

    Rect railRect() const {
        Rect body{0, contentTop(), clientRect().w, bodyBottom() - contentTop()};
        return Rect{body.x + theme::M.px(16), body.y, groupW(), body.h};
    }
    Rect rowsRect() const {
        Rect body{0, contentTop(), clientRect().w, bodyBottom() - contentTop()};
        int x = groupW() + theme::M.px(24);
        return Rect{x, body.y, body.w - x - theme::M.px(16), body.h};
    }

    // The list is drawn and clipped to whole rows. A row sliced in half by the
    // bottom edge leaves its value cell floating with no flag beside it, which
    // reads as a rendering bug rather than as a scroll position.
    Rect rowsClip() const {
        Rect r = rowsRect();
        int rh = std::max(1, rowH());
        int full = (r.h / rh) * rh;
        return Rect{r.x, r.y, r.w, full > 0 ? full : r.h};
    }

    // The command preview, spanning the rows column just above the footer.
    // Sized to exactly the lines the command needs (see previewBlockH), and
    // clamped so it can never poke into the footer on a short window.
    Rect previewBox() const {
        Rect rows = rowsRect();
        int lineH = theme::lineHeight(theme::fontMono());
        int n = std::max(1, previewLinesNeeded());
        int h = n * lineH + theme::M.px(16);
        int top = bodyBottom() + theme::M.px(24);
        int bottom = footerTop() - theme::M.px(6);
        if (top + h > bottom) h = std::max(theme::M.px(24), bottom - top);
        return Rect{rows.x, top, rows.w, h};
    }
    Rect previewCaption() const {
        Rect rows = rowsRect();
        int top = bodyBottom() + theme::M.px(2);
        return Rect{rows.x, top, rows.w, theme::M.px(18)};
    }

    // The command the launch would actually run: every switched-on row, in the
    // same order buildArgs uses, so the preview cannot drift from reality.
    std::wstring previewCommand() {
        std::vector<std::wstring> parts;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& r = rows_[i];
            if (!r.enabled || r.flag.empty()) continue;
            bool toggle = r.spec && r.spec->isToggle;
            std::wstring v = util::trim(readRowValue(i));
            if (toggle) {
                // Same rule as buildArgs: the switch alone decides.
                parts.push_back(r.flag);
                continue;
            }
            if (v.empty()) continue;
            parts.push_back(r.flag);
            parts.push_back(util::quoteArg(v));
        }
        std::wstring exe = store_.serverExe();
        std::wstring head = exe.empty() ? std::wstring(L"llama-server.exe") : exe;
        std::wstring args = util::join(parts, L" ");
        return args.empty() ? head : head + L" " + args;
    }

    // Keeps the preview in step with the editors. Called from every EN_CHANGE
    // and whenever the rows are re-laid out. The strip is painted by hand
    // rather than by a child EDIT: a read-only EDIT brings its own scrollbars
    // and its own background, neither of which can be made to match the theme.
    void updatePreview() {
        std::wstring text = previewCommand();
        if (text == previewText_) return;
        previewText_ = text;
        previewLines_ = -1;   // the wrap count changed with the text
        invalidate();
    }

    void onCommand(int, int code, HWND) override {
        if (code == EN_CHANGE) updatePreview();
    }

    // Splits `text` into at most `maxLines` lines that fit `maxW` pixels of the
    // monospaced face, adding an ellipsis when the rest does not fit. The face
    // is monospaced, so one width sample serves the whole string: measuring
    // every prefix character was O(n^2) and visible while typing long paths.
    std::vector<std::wstring> wrapCommand(HDC dc, const std::wstring& text, int maxW,
                                          int maxLines) const {
        std::vector<std::wstring> lines;
        if (maxW <= 0 || maxLines <= 0 || text.empty()) return lines;

        HGDIOBJ oldFont = ::SelectObject(dc, theme::fontMono());
        SIZE sz{};
        ::GetTextExtentPoint32W(dc, L"MM", 2, &sz);
        ::SelectObject(dc, oldFont);
        int charW = std::max(1, (int)(sz.cx / 2));
        int perLine = std::max(1, maxW / charW);

        size_t start = 0;
        while (start < text.size()) {
            if ((int)lines.size() >= maxLines) {
                // Mark the cut so the user knows the command continues.
                std::wstring& last = lines.back();
                const std::wstring kEllipsis = L"\u2026";
                while (!last.empty() &&
                       (int)((last.size() + kEllipsis.size()) * charW) > maxW)
                    last.pop_back();
                last += kEllipsis;
                return lines;
            }
            size_t take = std::min((size_t)perLine, text.size() - start);
            lines.push_back(text.substr(start, take));
            start += take;
        }
        return lines;
    }

    // Which group key is currently selected on the rail.
    const std::wstring& activeGroup() const {
        static const std::wstring empty;
        if (groups_.empty()) return empty;
        size_t i = (size_t)std::clamp(activeGroup_, 0, (int)groups_.size() - 1);
        return groups_[i];
    }

    bool rowInActiveGroup(const Row& r) const {
        // Only -1 means "all groups". Testing `<= 0` also swallowed group 0, so
        // picking the first real group showed an empty list.
        if (activeGroup_ < 0) return false;
        return util::iequals(r.group, activeGroup());
    }

    void onLayout() override {
        if (nameEdit_) {
            recomputeVisible();
            placeControls();
            return;
        }
        nameEdit_ = makeEdit(hwnd(), theme::fontBody());
        ::SetWindowTextW(nameEdit_, name_.c_str());
        // The row editors have to exist from the start, otherwise nothing in the
        // parameter list accepts typing until a row is added by hand.
        recomputeVisible();
        ensureRowWidgets();
        placeControls();
        // After the editors, so the tip is above them.
        ensureTip();
    }

    void onDestroy() override {
        nameEdit_ = nullptr;
        rowWidgets_.clear();
        if (tip_) {
            ::DestroyWindow(tip_);
            tip_ = nullptr;
        }
    }

    // Which row the pointer is over, or -1. Drives the hover tooltip.
    int rowAt(int y) const {
        Rect rows = rowsRect();
        if (y < rows.y || y >= rows.bottom()) return -1;
        int top = rows.y - scroll_;
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (!(i < visible_.size() && visible_[i])) continue;
            if (y >= top && y < top + rowH()) return (int)i;
            top += rowH();
        }
        return -1;
    }

    void onMouseMove(int x, int y) override {
        int r = rowAt(y);
        // The tooltip belongs to the flag column only. Over the value box the
        // popup used to freeze in place (the EDIT below it swallows the mouse
        // moves), which read as constant flicker while crossing the row.
        bool overFlag = false;
        if (r >= 0) {
            Rect rows = rowsRect();
            int top = rows.y - scroll_;
            for (size_t i = 0; i < rows_.size() && i <= (size_t)r; ++i) {
                if (!(i < visible_.size() && visible_[i])) continue;
                if (i == (size_t)r) {
                    Rect rowRect{rows.x, top, rows.w, rowH()};
                    bool hasBrowse = rows_[i].spec &&
                                     (rows_[i].spec->valueIsFile || rows_[i].spec->valueIsDir);
                    overFlag = x <= rowCells(rowRect, rows_[i].custom, hasBrowse).flag.right();
                    break;
                }
                top += rowH();
            }
        }
        if (!overFlag) r = -1;
        if (r != hoverRow_) {
            hoverRow_ = r;
            updateTip();
        } else if (r >= 0 && (x != hoverX_ || y != hoverY_)) {
            hoverX_ = x;
            hoverY_ = y;
            updateTip();
        }
    }

    void onMouseLeave() override {
        hoverRow_ = -1;
        hideTip();
    }

    // Creates the tip window once, after the editors exist so it sits above
    // them in the child z-order.
    void ensureTip() {
        if (tip_) return;
        registerTipClass();
        // A popup owned by the dialog, not a child: a child window is still
        // clipped to the dialog and shares the z-order with the value boxes,
        // whereas an owned popup always floats above every one of them.
        // WS_EX_TRANSPARENT keeps the pointer events on the dialog so the tip
        // cannot swallow its own mouse moves and freeze.
        tip_ = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                 kTipClass, L"", WS_POPUP, 0, 0, 10, 10, hwnd(), nullptr,
                                 ::GetModuleHandleW(nullptr), nullptr);
        if (tip_) ::SetWindowLongPtrW(tip_, GWLP_USERDATA, (LONG_PTR)&tipText_);
    }

    void updateTip() {
        ensureTip();
        if (!tip_) return;

        std::wstring text = (hoverRow_ >= 0 && hoverRow_ < (int)rows_.size())
                                ? rows_[(size_t)hoverRow_].desc
                                : std::wstring();
        if (text.empty()) {
            ::ShowWindow(tip_, SW_HIDE);
            return;
        }

        // Measuring is the expensive part, so it is only redone when the row
        // changes - not on every mouse move, which is what made the tip feel
        // sluggish.
        if (hoverRow_ != tipRow_ || text != tipText_) {
            tipText_ = text;
            tipRow_ = hoverRow_;

            int pad = theme::M.px(10);
            int maxW = theme::M.px(380);
            HDC screen = ::GetDC(nullptr);
            HGDIOBJ oldFont = ::SelectObject(screen, theme::fontSmall());
            RECT rc{0, 0, maxW - pad * 2, 0};
            ::DrawTextW(screen, tipText_.c_str(), (int)tipText_.size(), &rc,
                        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            ::SelectObject(screen, oldFont);
            ::ReleaseDC(nullptr, screen);

            tipW_ = std::min(maxW, (int)(rc.right - rc.left) + pad * 2 + theme::M.px(6));
            tipH_ = (int)(rc.bottom - rc.top) + pad * 2 + theme::M.px(2);
            int r = theme::M.radiusLarge;
            ::SetWindowRgn(tip_, ::CreateRoundRectRgn(0, 0, tipW_ + 1, tipH_ + 1, r * 2, r * 2),
                           FALSE);
        }

        POINT pt{hoverX_, hoverY_};
        ::ClientToScreen(hwnd(), &pt);

        // Keep the tip inside the dialog. Everything here is screen space
        // because the tip is a popup, not a child.
        RECT wr{};
        ::GetWindowRect(hwnd(), &wr);
        int margin = theme::M.px(8);
        int x = std::min((int)pt.x + theme::M.px(16), (int)wr.right - tipW_ - margin);
        int y = std::min((int)pt.y + theme::M.px(20), (int)wr.bottom - tipH_ - margin);
        x = std::max(x, (int)wr.left + margin);
        y = std::max(y, (int)wr.top + margin);
        ::SetWindowPos(tip_, HWND_TOPMOST, x, y, tipW_, tipH_,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ::InvalidateRect(tip_, nullptr, FALSE);
    }

    void hideTip() {
        if (tip_) ::ShowWindow(tip_, SW_HIDE);
    }


    // ------------------------------------------------------------------ controls
    void ensureRowWidgets() {
        // One editor pair per row, created up front. The parameter list is only
        // a few dozen rows, so there is nothing to save by deferring, and having
        // them all present means scrolling and group filtering never have to
        // create or destroy anything.
        while (rowWidgets_.size() < rows_.size()) rowWidgets_.push_back(RowWidgets{});
        for (size_t i = 0; i < rows_.size(); ++i) {
            RowWidgets& w = rowWidgets_[i];
            if (w.created) continue;
            if (rows_[i].custom) {
                w.flag = makeEdit(hwnd(), theme::fontMono());
                ::SetWindowTextW(w.flag, rows_[i].flag.c_str());
            }
            w.value = makeEdit(hwnd(), theme::fontBody());
            ::SetWindowTextW(w.value, rows_[i].value.c_str());
            if (rows_[i].spec && (rows_[i].spec->valueIsFile || rows_[i].spec->valueIsDir)) {
                // Browse buttons are drawn on the canvas; only the edit boxes are
                // real controls, so they are tracked as hit regions instead.
            }
            w.created = true;
        }
    }

    // Recomputes which rows the active group shows.
    //
    // This used to happen inside the paint pass, which meant placeControls()
    // still laid the child editors out from the *previous* filter - switching
    // group reordered the drawn rows but left every value box where it was, so
    // values sat next to the wrong flags. Visibility is now resolved before
    // anything is placed, and the paint pass only reads it.
    void recomputeVisible() {
        visible_.resize(rows_.size(), true);
        for (size_t i = 0; i < rows_.size(); ++i)
            visible_[i] = (activeGroup_ < 0) || rowInActiveGroup(rows_[i]);
    }

    void placeControls() {
        if (nameEdit_) {
            Rect nf = nameField();
            Rect n = nf.inset(theme::M.px(1));
            // Same vertical centring as the parameter rows: a single-line EDIT
            // sits at the top of its box, so the box is shrunk to one line.
            int lh = theme::lineHeight(theme::fontBody()) + theme::M.px(4);
            n.y = nf.y + (nf.h - lh) / 2;
            n.h = lh;
            ::SetWindowPos(nameEdit_, nullptr, n.x, n.y, n.w, n.h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        Rect rows = rowsRect();
        int y = rows.y - scroll_;
        for (size_t i = 0; i < rows_.size(); ++i) {
            bool vis = i < visible_.size() && visible_[i];
            RowWidgets& w = rowWidgets_[i];
            // Filtered rows take no slot at all: the visible rows stack from
            // the top of the list, exactly as the paint pass draws them.
            // (Advancing y here too left the editors sitting in the slots of
            // the unfiltered list - off-screen for most groups.)
            if (!vis || !w.created) {
                if (w.flag) ::ShowWindow(w.flag, SW_HIDE);
                if (w.value) ::ShowWindow(w.value, SW_HIDE);
                continue;
            }
            Rect row{rows.x, y, rows.w, rowH()};
            y += rowH();
            if (row.bottom() < rows.top() - theme::M.px(4) ||
                row.top() > rows.bottom() + theme::M.px(4)) {
                if (w.flag) ::ShowWindow(w.flag, SW_HIDE);
                if (w.value) ::ShowWindow(w.value, SW_HIDE);
                continue;
            }
            bool hasBrowse = rows_[i].spec &&
                             (rows_[i].spec->valueIsFile || rows_[i].spec->valueIsDir);
            Cells cells = rowCells(row, rows_[i].custom, hasBrowse);

            // A row that is only partly inside the list still paints (the paint
            // pass clips itself with SelectClipRgn), but its editor is a real
            // window and GDI will not clip that for us - it would scroll up over
            // the name field. So editors are only shown for rows that are fully
            // inside the same region the paint pass clips to; a half-visible row
            // is not an editing target anyway.
            const Rect listClip = rowsClip();
            auto place = [&](HWND ctl, const Rect& box, bool editable, bool centerVert = false) {
                Rect b = box.inset(theme::M.px(1));
                if (centerVert) {
                    // A single-line EDIT pins its text to the top of its box,
                    // so the box is shrunk to exactly one line and centred in
                    // the cell - that is what "vertically centred" means here.
                    int lh = theme::lineHeight(theme::fontBody()) + theme::M.px(4);
                    b.y = box.y + (box.h - lh) / 2;
                    b.h = lh;
                }
                bool fullyInside = b.y >= listClip.top() && b.bottom() <= listClip.bottom();
                if (!fullyInside) {
                    ::ShowWindow(ctl, SW_HIDE);
                    return;
                }
                ::SetWindowPos(ctl, nullptr, b.x, b.y, b.w, b.h,
                               SWP_NOZORDER | SWP_NOACTIVATE);
                // A row that is switched off keeps its text but refuses edits.
                ::EnableWindow(ctl, editable ? TRUE : FALSE);
                ::ShowWindow(ctl, SW_SHOW);
            };

            // The flag box is only editable on custom rows while they are on.
            // Centred like the value box: a single-line EDIT pinned to the top
            // of the cell read as misaligned with the value field beside it.
            bool customEditable = rows_[i].custom && rows_[i].enabled;
            if (w.flag) place(w.flag, cells.flag, customEditable, true);
            // A flag-style parameter has nothing to type: its cell shows the
            // description instead (painted in onPaint), so the edit box would
            // only invite typing that can never reach the command line.
            bool toggleRow = rows_[i].spec && rows_[i].spec->isToggle;
            if (w.value) {
                if (toggleRow) {
                    ::ShowWindow(w.value, SW_HIDE);
                } else {
                    place(w.value, cells.value, rows_[i].enabled, true);
                }
            }
        }

        // Refresh the cached command; the strip itself is painted in onPaint.
        updatePreview();
    }

    struct Cells {
        Rect check, flag, value, browse;
    };

    Cells rowCells(const Rect& row, bool custom, bool hasBrowse) const {
        Cells c;
        // The flag column only has to fit "-m  (--model)"; everything left over
        // goes to the value, which is where long model paths get typed.
        int flagW = custom ? theme::M.px(150) : theme::M.px(176);
        // Wide enough for "Choose file" / "选择文件" next to its icon at any
        // language; the old 88 clipped the Chinese label to "选择文...".
        int browseW = hasBrowse ? theme::M.px(116) : 0;
        int removeW = custom ? theme::M.px(34) : 0;
        int gap = theme::M.px(6);
        int pad = theme::M.px(10);
        int checkW = theme::M.px(22);

        // The switch sits in its own gutter so the flag column keeps its
        // alignment whether or not the row is on.
        c.check = Rect{row.x + pad, row.y + theme::M.px(5), checkW, row.h - theme::M.px(10)};
        c.flag = Rect{c.check.right() + theme::M.px(4), row.y + theme::M.px(5), flagW,
                      row.h - theme::M.px(10)};
        int vx = c.flag.right() + gap;
        int vw = row.right() - pad - removeW - (browseW ? browseW + gap : 0) - vx;
        c.value = Rect{vx, c.flag.y, vw, c.flag.h};
        if (browseW)
            c.browse = Rect{c.value.right() + gap, c.flag.y, browseW, c.flag.h};
        return c;
    }

    Rect nameField() const {
        int x = theme::M.px(24);
        return Rect{x, theme::M.px(56) + theme::M.px(30), clientRect().w - x - theme::M.px(24),
                    theme::M.px(32)};
    }

    // -------------------------------------------------------------------- paint
    void onPaint(Canvas& c, const Rect& client) override {
        // Refresh the wrap count before any geometry runs: bodyBottom() carves
        // room for the preview out of the row list and reads this.
        previewLines_ = countLines(c.dc());

        // Chrome
        Rect header{0, 0, client.w, theme::M.px(56)};
        c.fill(header, theme::LayerBg);
        c.line(0, header.bottom() - 1, client.w, header.bottom() - 1, theme::Border);
        c.text(Rect{theme::M.px(22), 0, client.w - theme::M.px(60), header.h},
               isNew_ ? T(Str::CreateConfigTitle) : T(Str::EditConfigTitle), theme::TextPrimary,
               theme::fontSubtitleBold(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Name field
        c.text(Rect{theme::M.px(24), theme::M.px(56) + theme::M.px(8),
                    client.w - theme::M.px(48), theme::M.px(20)},
               T(Str::ConfigName), theme::TextSecondary, theme::fontSmall(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Rect nf = nameField();
        c.fillRound(nf, theme::M.radiusSmall, theme::CardBg);
        c.strokeRound(nf, theme::M.radiusSmall, theme::BorderStrong);
        // The name edit itself is positioned by placeControls() (centred in
        // this field). It must NOT be re-synced here: paint ran with the same
        // geometry, and a second SetWindowPos from the paint pass fought the
        // centred placement, which showed up as the name box twitching on
        // every scroll step. WM_SIZE already re-runs onLayout -> placeControls.

        // ---- body ----
        Rect body{0, contentTop(), client.w, footerTop() - contentTop()};
        c.fill(body, theme::LayerBg);

        Rect rail = railRect();
        Rect rows = rowsRect();

        // A backdrop for the rail so it reads as navigation rather than a list.
        Rect railBg{rail.x - theme::M.px(8), rail.y, rail.w + theme::M.px(16), rail.h};
        COLORREF railBase = theme::isDarkMode() ? theme::blend(theme::LayerBg, RGB(0, 0, 0), 30)
                                                : RGB(240, 240, 240);
        c.fillRound(railBg, theme::M.radiusMedium, railBase);

        // ---- group rail ----
        int gy = rail.y + theme::M.px(10);
        auto drawGroup = [&](int index, const std::wstring& key, const std::wstring& label,
                             int count) {
            Rect item{rail.x, gy, rail.w, theme::M.px(32)};
            gy += item.h + theme::M.px(2);
            bool selected = (activeGroup_ == index);
            // `index` is -1 for the "all parameters" entry, which made the raw
            // id 499 - below ID_GROUP_FIRST, so onClick() never saw it and the
            // row was dead. Shift the whole rail up by one to keep every id in
            // the group range.
            int hitId = ID_GROUP_FIRST + index + 1;
            addHit(item, hitId);
            if (selected)
                c.fillRound(item, theme::M.radiusSmall, theme::AccentSoft);
            else if (isHovered(hitId))
                c.fillRound(item, theme::M.radiusSmall,
                        theme::blend(railBase, RGB(255, 255, 255),
                                     theme::isDarkMode() ? 26 : 140));
            std::wstring text = label;
            if (count > 0) text += util::format(L"  %d", count);
            c.text(Rect{item.x + theme::M.px(12), item.y, item.w - theme::M.px(24), item.h}, text,
                   selected ? theme::Accent : theme::TextSecondary,
                   selected ? theme::fontBodyBold() : theme::fontBody(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            (void)key;
        };

        // "All parameters" pseudo group, then every real group that has rows.
        int totalVisible = 0;
        for (const Row& r : rows_)
            if (r.custom || (r.spec && catalog::groupFromKey(r.group, dummyGroup()))) ++totalVisible;
        // The first entry clears the filter. It used to borrow the
        // "Launch parameters" label, which read as just another group rather
        // than as "show everything".
        drawGroup(-1, L"", T(Str::AllParams), (int)rows_.size());

        for (size_t i = 0; i < groups_.size(); ++i) {
            int count = 0;
            for (const Row& r : rows_)
                if (util::iequals(r.group, groups_[i])) ++count;
            if (count == 0) continue;
            const wchar_t* label = nullptr;
            catalog::Group g;
            if (catalog::groupFromKey(groups_[i], g)) label = catalog::groupName(g);
            static thread_local std::wstring customLabel;
            std::wstring text;
            if (label) {
                text = label;
            } else {
                // A user-invented group: show its raw key.
                text = groups_[i];
            }
            drawGroup((int)i, groups_[i], text, count);
        }

        // ---- rows ----
        Rect clipRect = rowsClip();
        HRGN clip = ::CreateRectRgn(clipRect.left(), clipRect.top(), clipRect.right(),
                                    clipRect.bottom());
        ::SelectClipRgn(c.dc(), clip);

        int y = rows.y - scroll_;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& r = rows_[i];
            // Read-only here: visibility is decided by recomputeVisible() before
            // the controls are placed, so paint and placement cannot disagree.
            bool vis = activeGroup_ < 0 || rowInActiveGroup(r);
            if (!vis) continue;
            // Visible rows stack from the top of the list: a group filter must
            // bring its own parameters into view, not leave them in the slots
            // they occupied in the unfiltered list (which is off-screen).
            Rect row{rows.x, y, rows.w, rowH()};
            y += rowH();

            // Separator, drawn only for rows that are fully visible.
            if (row.bottom() < rows.bottom()) {
                c.line(row.x + theme::M.px(10), row.bottom(), row.right() - theme::M.px(10),
                       row.bottom(), theme::Divider);
            }
            // Rows entirely outside the drawn area register no hit regions:
            // their rectangles would overlap the command preview strip, where a
            // click silently flipped a parameter nobody could see.
            if (row.bottom() <= clipRect.top() || row.top() >= clipRect.bottom()) {
                continue;
            }

            bool hasBrowse = r.spec && (r.spec->valueIsFile || r.spec->valueIsDir);
            Cells cells = rowCells(row, r.custom, hasBrowse);

            // Switch. Off rows are greyed and their editors are read-only, so
            // nothing half-finished can reach the command line.
            int toggleId = ID_ROW_FIRST + (int)i * ID_ROW_STRIDE + ROW_TOGGLE;
            addHit(cells.check, toggleId);
            Rect box{cells.check.cx() - theme::M.px(8), cells.check.cy() - theme::M.px(8),
                     theme::M.px(16), theme::M.px(16)};
            if (r.enabled) {
                c.fillRound(box, theme::M.radiusSmall, theme::Accent);
                c.glyph(box, shell::glyphs::kCheck, theme::TextOnAccent, 10);
            } else {
                COLORREF face = isHovered(toggleId)
                                    ? theme::blend(theme::CardBg, RGB(0, 0, 0), theme::HoverAlpha)
                                    : theme::CardBg;
                c.fillRound(box, theme::M.radiusSmall, face);
                c.strokeRound(box, theme::M.radiusSmall, theme::BorderStrong);
            }

            COLORREF flagColor = r.enabled ? theme::TextPrimary : theme::TextDisabled;
            // Same helper the EDIT control's background uses, so the cell and
            // the control sitting in it can never disagree.
            COLORREF fieldFill = theme::fieldBack(r.enabled);
            bool toggle = r.spec && r.spec->isToggle;

            // Flag column.
            if (r.custom) {
                c.fillRound(cells.flag, theme::M.radiusSmall, fieldFill);
                c.strokeRound(cells.flag, theme::M.radiusSmall,
                              r.enabled ? theme::BorderStrong : theme::Border);
            } else {
                // Built-in rows: the flag is text on the surface, signalling it
                // cannot be edited.
                std::wstring label = store::flagLabel(r.flag);
                HGDIOBJ oldFont = ::SelectObject(c.dc(), theme::fontMono());
                std::wstring shown =
                    util::ellipsize(c.dc(), label, cells.flag.w - theme::M.px(4));
                ::SelectObject(c.dc(), oldFont);
                c.text(Rect{cells.flag.x + theme::M.px(2), cells.flag.y,
                            cells.flag.w - theme::M.px(4), cells.flag.h},
                       shown, flagColor, theme::fontMono(),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }

            // Value column. A switched-off row gets a lighter outline as well as
            // the muted fill, so the cell reads as inert rather than as an
            // empty-but-editable box.
            COLORREF fieldBorder = r.enabled ? theme::BorderStrong : theme::Border;
            c.fillRound(cells.value, theme::M.radiusSmall, fieldFill);
            c.strokeRound(cells.value, theme::M.radiusSmall, fieldBorder);
            if (toggle) {
                // Nothing to type here - the switch on the left is the whole
                // interaction - so the cell carries the description instead.
                c.text(cells.value.inset(theme::M.px(8)),
                       r.desc.empty() ? std::wstring(L"—") : r.desc, theme::TextTertiary,
                       theme::fontCaption(),
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            if (hasBrowse) {
                // Keyed by the row's own index. Using a counter of *drawn* rows
                // here was wrong: filtering by group or scrolling shifts it, so
                // the button ended up acting on a different row - which is why
                // the model picker appeared to do nothing.
                int id = ID_ROW_FIRST + (int)i * ID_ROW_STRIDE + ROW_BROWSE;
                addHit(cells.browse, id, r.enabled);
                shell::button(c, cells.browse,
                              r.spec->valueIsFile ? T(Str::BrowseFile) : T(Str::Browse),
                              shell::ButtonStyle::Secondary,
                              r.enabled && isHovered(id), r.enabled && isPressed(id), false,
                              r.spec->valueIsFile ? shell::glyphs::kFile : shell::glyphs::kFolder,
                              0, r.enabled);
            }

            if (r.custom) {
                int removeId = ID_ROW_FIRST + (int)i * ID_ROW_STRIDE + 3;
                Rect removeRect{cells.value.right() + theme::M.px(6), cells.flag.y,
                                theme::M.px(28), cells.flag.h};
                addHit(removeRect, removeId);
                c.glyph(removeRect, shell::glyphs::kDelete,
                        isHovered(removeId) ? theme::Danger : theme::TextTertiary, 14);
            }
        }

        ::SelectClipRgn(c.dc(), nullptr);
        ::DeleteObject(clip);

        // ---- command preview ----
        c.text(previewCaption(), T(Str::CommandPreview), theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        Rect pv = previewBox();
        c.fillRound(pv, theme::M.radiusSmall, theme::fieldBack(false));
        c.strokeRound(pv, theme::M.radiusSmall, theme::BorderStrong);

        {
            int padX = theme::M.px(10);
            int padY = theme::M.px(7);
            int lineH = theme::lineHeight(theme::fontMono());
            int lines = std::max(1, (pv.h - padY * 2 + theme::M.px(4)) / std::max(1, lineH));
            Rect inner{pv.x + padX, pv.y + padY, pv.w - padX * 2, pv.h - padY * 2};

            HRGN previewClip =
                ::CreateRectRgn(inner.left(), inner.top(), inner.right(), inner.bottom());
            ::SelectClipRgn(c.dc(), previewClip);
            auto cmdLines = wrapCommand(c.dc(), previewText_, inner.w, lines);
            int ly = inner.y;
            for (size_t i = 0;
                 i < cmdLines.size() && ly + lineH <= inner.bottom() + theme::M.px(2); ++i) {
                c.text(Rect{inner.x, ly, inner.w, lineH}, cmdLines[i], theme::TextSecondary,
                       theme::fontMono(), DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
                ly += lineH;
            }
            ::SelectClipRgn(c.dc(), nullptr);
            ::DeleteObject(previewClip);
        }

        // ---- footer ----
        Rect footer{0, footerTop(), client.w, client.h - footerTop()};
        c.fill(footer, theme::LayerBg);
        c.line(0, footer.top(), client.w, footer.top(), theme::Border);

        Rect addBtn{rows.x, footer.y + theme::M.px(12), theme::M.px(140), theme::M.px(34)};
        addHit(addBtn, ID_ADD_ROW);
        shell::button(c, addBtn, T(Str::AddCustomParam), shell::ButtonStyle::Subtle,
                      isHovered(ID_ADD_ROW), isPressed(ID_ADD_ROW), false, shell::glyphs::kAdd);

        int bw = theme::M.px(100);
        int bh = theme::M.px(34);
        Rect ok{client.w - theme::M.px(24) - bw, footer.y + theme::M.px(12), bw, bh};
        Rect cancel{ok.x - theme::M.px(10) - bw, ok.y, bw, bh};
        addHit(ok, ID_OK);
        addHit(cancel, ID_CANCEL);
        shell::button(c, ok, T(Str::Save), shell::ButtonStyle::Primary, isHovered(ID_OK),
                      isPressed(ID_OK), false);
        shell::button(c, cancel, T(Str::Cancel), shell::ButtonStyle::Secondary,
                      isHovered(ID_CANCEL), isPressed(ID_CANCEL), false);

    }

    // ------------------------------------------------------------------- input
    void onMouseWheel(int delta, int, int y) override {
        (void)y;
        Rect rows = rowsRect();
        int total = 0;
        for (size_t i = 0; i < rows_.size(); ++i)
            if (i < visible_.size() && visible_[i]) total += rowH();
        // The maximum follows the clip, not the raw height: painting and
        // editor placement only cover whole rows, so a maximum computed from
        // rows.h left the last row (typically the freshly added custom one)
        // permanently half-cut with its editors hidden.
        int maxScroll = std::max(0, total - rowsClip().h);
        scroll_ -= delta / WHEEL_DELTA * theme::M.px(48);
        scroll_ = std::clamp(scroll_, 0, maxScroll);
        // Both halves have to move together. Repositioning the child editors
        // without repainting left the rows' backgrounds, flags and separators
        // drawn at the old offset while the editors slid to the new one.
        placeControls();
        invalidate();
        // invalidate() only queues a repaint, and WM_PAINT is low priority:
        // during a fast wheel the editors were already at the new offset while
        // the canvas still showed the old one, which read as boxes floating
        // over the wrong rows. UpdateWindow repaints synchronously, so every
        // scroll step lands fully composed.
        ::UpdateWindow(hwnd());
    }

    void onClick(int id) override {
        // Row ids must be tested first: they start at ID_ROW_FIRST (1000), which
        // is well above ID_GROUP_FIRST (500). Checking the group range first
        // swallowed every row click - pressing a row's browse button silently
        // switched the active group instead of opening the file dialog.
        if (id >= ID_ROW_FIRST) {
            int rel = id - ID_ROW_FIRST;
            int row = rel / ID_ROW_STRIDE;
            int field = rel % ID_ROW_STRIDE;
            if (row >= 0 && row < (int)rows_.size()) {
                if (field == 2) {
                    // Browse for the file / folder this row points at.
                    const Row& r = rows_[(size_t)row];
                    std::wstring current = readRowValue((size_t)row);
                    std::wstring picked;
                    bool ok = false;
                    if (r.spec && r.spec->valueIsDir) {
                        picked = current;
                        ok = pickFolder(hwnd(), T(Str::LlamaPath), picked);
                    } else {
                        std::wstring start = current;
                        picked = start;
                        ok = pickFile(hwnd(), T(Str::BrowseFile), L"GGUF model", L"*.gguf", L"gguf",
                                      picked);
                    }
                    if (ok) {
                        // Write to the model as well as the control: the control
                        // is what the user sees, the model is what commit()
                        // reads when the row happens to be filtered out.
                        rows_[(size_t)row].value = picked;
                        if ((size_t)row < rowWidgets_.size() && rowWidgets_[(size_t)row].value)
                            ::SetWindowTextW(rowWidgets_[(size_t)row].value, picked.c_str());
                        invalidate();
                    }
                } else if (field == ROW_TOGGLE) {
                    // Flip the switch: on means "this parameter is passed".
                    bool nowOn = !rows_[(size_t)row].enabled;
                    rows_[(size_t)row].enabled = nowOn;
                    placeControls();
                    invalidate();
                    return;
                } else if (field == ROW_REMOVE) {
                    // Remove a custom row.
                    if (rowWidgets_[(size_t)row].flag)
                        ::DestroyWindow(rowWidgets_[(size_t)row].flag);
                    if (rowWidgets_[(size_t)row].value)
                        ::DestroyWindow(rowWidgets_[(size_t)row].value);
                    rows_.erase(rows_.begin() + row);
                    rowWidgets_.erase(rowWidgets_.begin() + row);
                    visible_.erase(visible_.begin() + row);
                    recomputeVisible();
                    placeControls();
                    invalidate();
                    return;
                }
            }
            return;
        }

        if (id >= ID_GROUP_FIRST) {
            // Mirrors the +1 applied when the rail registers its hits: the
            // first entry is the "all parameters" pseudo group, i.e. -1.
            activeGroup_ = id - ID_GROUP_FIRST - 1;
            scroll_ = 0;
            // The filter changed, so the editors have to be re-laid-out before
            // the repaint, not after.
            recomputeVisible();
            ensureRowWidgets();
            placeControls();
            invalidate();
            // Same reason as in onMouseWheel: land the reflow fully composed.
            ::UpdateWindow(hwnd());
            return;
        }

        switch (id) {
            case ID_ADD_ROW: {
                Row r;
                r.custom = true;
                r.flag = kNewRowFlag;
                r.value.clear();
                r.group = (activeGroup_ >= 0 && activeGroup_ < (int)groups_.size())
                              ? groups_[(size_t)activeGroup_]
                              : L"custom";
                // A row the user has just asked for starts switched on. Rows
                // default to off, and an off row has both its editors disabled -
                // so a new row could not be typed into at all, its value was
                // never entered, and saving looked like it had dropped the
                // parameter the user had just added.
                r.enabled = true;
                rows_.push_back(std::move(r));
                visible_.push_back(true);
                scroll_ = 1 << 20;   // jump to the new row
                recomputeVisible();
                ensureRowWidgets();
                placeControls();
                // Clamp the scroll to the real maximum.
                int total = 0;
                for (size_t i = 0; i < rows_.size(); ++i)
                    if (i < visible_.size() && visible_[i]) total += rowH();
                // Clamp to the real maximum (same whole-row rule as the wheel).
                scroll_ = std::max(0, total - rowsClip().h);
                placeControls();
                // Put the caret in the new flag box with the placeholder
                // selected, so the first keystroke replaces it.
                if (!rowWidgets_.empty() && rowWidgets_.back().flag) {
                    ::SetFocus(rowWidgets_.back().flag);
                    ::SendMessageW(rowWidgets_.back().flag, EM_SETSEL, 0, -1);
                }
                invalidate();
                return;
            }

            case ID_OK: {
                if (!commit()) return;
                close(DialogResult::Ok);
                return;
            }

            case ID_CANCEL:
                close(DialogResult::Cancel);
                return;

            default:
                break;
        }
        invalidate();
    }

    bool onKeyDown(WPARAM key) override {
        if (key == VK_RETURN) {
            if (!commit()) return true;
            close(DialogResult::Ok);
            return true;
        }
        return false;
    }

private:
    std::wstring readRowValue(size_t index) {
        if (index < rowWidgets_.size() && rowWidgets_[index].value)
            return editText(rowWidgets_[index].value);
        return rows_[index].value;
    }

    // Pulls every control back into the model and validates it.
    bool commit() {
        name_ = util::trim(editText(nameEdit_));
        if (name_.empty()) {
            message(hwnd(), T(Str::NameRequired), T(Str::NameRequired));
            return false;
        }
        // Names are the only thing the sidebar shows, so two configurations
        // sharing one would be indistinguishable. The configuration being
        // edited is skipped so re-saving it unchanged is still fine.
        for (const store::Config& c : store_.configs()) {
            if (c.id == config_.id) continue;
            if (util::iequals(util::trim(c.name), name_)) {
                message(hwnd(), T(Str::NameDuplicate), T(Str::NameDuplicate));
                return false;
            }
        }

        std::vector<store::Param> params;
        for (size_t i = 0; i < rows_.size(); ++i) {
            Row& r = rows_[i];
            std::wstring value = readRowValue(i);

            if (r.custom) {
                std::wstring flag = util::trim(editText(rowWidgets_[i].flag));
                std::wstring trimmed = util::trim(value);
                // An added-but-abandoned row still carries the placeholder.
                // Saving it verbatim would put a literal "--" in the command
                // line, so the placeholder counts as "nothing typed yet".
                if (flag == kNewRowFlag) flag.clear();
                if (flag.empty() && trimmed.empty()) continue;   // blank row
                if (flag.empty()) {
                    message(hwnd(), T(Str::InvalidParam), T(Str::InvalidParam));
                    return false;
                }
                if (flag[0] != L'-') {
                    message(hwnd(), T(Str::InvalidParam), T(Str::InvalidParam));
                    return false;
                }
                store::Param p;
                p.flag = flag;
                p.value = value;
                p.group = r.group.empty() ? L"custom" : r.group;
                p.desc = r.desc;
                p.custom = true;
                p.enabled = r.enabled;
                params.push_back(std::move(p));
                continue;
            }

            store::Param p;
            p.flag = r.flag;
            // Flag-style parameters carry no value: "on" when the switch is on
            // and empty when it is off, so the file always agrees with the
            // switch no matter what a stale edit box holds.
            p.value = (r.spec && r.spec->isToggle) ? (r.enabled ? L"on" : L"") : value;
            p.group = r.group;
            p.custom = false;
            p.enabled = r.enabled;
            params.push_back(std::move(p));
        }

        config_.name = name_;
        config_.notes = notes_;
        config_.params = std::move(params);

        std::wstring err;
        if (!store_.upsert(config_, &err)) {
            message(hwnd(), T(Str::SaveFailed), T(Str::SaveFailed));
            return false;
        }
        return true;
    }

    // Compile-time helper: the rail needs to know whether a group key belongs to
    // the catalogue, which groupFromKey reports.
    static catalog::Group& dummyGroup() {
        static catalog::Group g = catalog::Group::Basic;
        return g;
    }

    store::Store& store_;
    store::Config& config_;
    bool isNew_ = false;

    std::wstring name_;
    std::wstring notes_;

    std::vector<Row> rows_;
    std::vector<RowWidgets> rowWidgets_;
    std::vector<bool> visible_;
    std::vector<std::wstring> groups_;

    int activeGroup_ = -1;   // -1 = all groups
    int scroll_ = 0;
    HWND nameEdit_ = nullptr;
    // The command shown in the preview strip, refreshed whenever a row changes.
    std::wstring previewText_;
    // Lines the preview needs when wrapped; cached, negative = re-measure.
    // mutable because the const geometry helpers fill it in lazily.
    mutable int previewLines_ = -1;
    // Hover tooltip state: which row the pointer is over and where it is.
    int hoverRow_ = -1;
    int hoverX_ = 0;
    int hoverY_ = 0;
    HWND tip_ = nullptr;
    std::wstring tipText_;
    int tipRow_ = -2;      // row the current tip was measured for
    int tipW_ = 0;
    int tipH_ = 0;
};

}  // namespace

bool paramEditorDialog(HWND owner, store::Store& store, store::Config& config, bool isNew) {
    ParamEditorDialog dlg(store, config, isNew);
    return dlg.run(owner, isNew ? T(Str::CreateConfigTitle) : T(Str::EditConfigTitle),
                   theme::M.px(880), theme::M.px(640), true);
}

// ============================================================================
//  Import / export (sub-view 4)
// ============================================================================
namespace {

// 96-dpi design units; ImportExportDialog scales them through theme::M.px()
// exactly like kRowH elsewhere. Raw values left every row at 57% size on a
// 175% display.
constexpr int kImportRowH = 40;
constexpr int kSelectAllW = 96;
constexpr int kIdColW = 96;

class ImportExportDialog : public Dialog {
public:
    ImportExportDialog(store::Store& store) : store_(store) {
        selected_.assign(store.configs().size(), true);
    }

    // Called after run(): hands back whether anything changed and which ids were
    // imported, so the caller can select the first new configuration.
    bool takeChanged(std::vector<std::wstring>& out) {
        out = importedIds_;
        return changed_;
    }

protected:
    // The two action cards occupy a fixed band under the header; the list gets
    // everything left above the footer. Keeping these as named helpers means the
    // paint pass and the hit regions can never disagree.
    int cardsTop() const { return theme::M.px(56) + theme::M.px(16); }
    int cardH() const { return theme::M.px(88); }
    int footerH() const { return theme::M.px(74); }

    Rect listRect() const {
        int top = cardsTop() + cardH() + theme::M.px(16);
        return Rect{theme::M.px(24), top, clientRect().w - theme::M.px(48),
                    clientRect().h - top - footerH()};
    }

    // Scaled accessors - the constants are 96-dpi design units.
    int importRowH() const { return theme::M.px(kImportRowH); }
    int selectAllW() const { return theme::M.px(kSelectAllW); }
    int idColW() const { return theme::M.px(kIdColW); }

    void onMouseWheel(int delta, int, int) override {
        const auto& configs = store_.configs();
        Rect list = listRect();
        int contentH = (int)configs.size() * importRowH();
        int maxScroll = std::max(0, contentH - (list.h - theme::M.px(40)));
        importScroll_ = std::clamp(importScroll_ - delta / WHEEL_DELTA * importRowH(), 0, maxScroll);
        invalidate();
    }

    void onPaint(Canvas& c, const Rect& client) override {
        Rect header{0, 0, client.w, theme::M.px(56)};
        c.fill(header, theme::LayerBg);
        c.line(0, header.bottom() - 1, client.w, header.bottom() - 1, theme::Border);
        c.text(Rect{theme::M.px(22), 0, client.w - theme::M.px(60), header.h},
               T(Str::ImportExportTitle), theme::TextPrimary, theme::fontSubtitleBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Both halves stay visible at once so the relationship is obvious: the
        // dialog imports on the left and exports on the right.
        int halfW = (client.w - theme::M.px(24) * 3) / 2;
        Rect importCard{theme::M.px(24), cardsTop(), halfW, cardH()};
        Rect exportCard{importCard.right() + theme::M.px(24), importCard.y, halfW, importCard.h};

        drawActionCard(c, importCard, shell::glyphs::kImport, T(Str::ImportSection),
                       T(Str::ImportHint), T(Str::ChooseFiles), ID_IMPORT_PICK);
        drawActionCard(c, exportCard, shell::glyphs::kExport, T(Str::ExportSection),
                       T(Str::ExportHint), T(Str::ExportSelected), ID_EXPORT_PICK);

        // ---- selection list ----
        Rect list = listRect();
        shell::card(c, list);

        // Header row: caption on the left, "select all" pinned right. The caption
        // is clipped to the gap so the two can never run into each other.
        Rect listHead{list.x + theme::M.px(14), list.y + theme::M.px(10), list.w - theme::M.px(28),
                      theme::M.px(22)};
        Rect allBtn{listHead.right() - selectAllW(), listHead.y + theme::M.px(1), selectAllW(),
                    theme::M.px(20)};
        Rect listTitle{listHead.x, listHead.y, allBtn.x - listHead.x - theme::M.px(10),
                       listHead.h};
        c.text(listTitle, T(Str::Configurations), theme::TextSecondary, theme::fontSmall(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        addHit(allBtn, ID_SELECT_ALL);
        shell::button(c, allBtn, T(Str::SelectAll), shell::ButtonStyle::Subtle,
                      isHovered(ID_SELECT_ALL), isPressed(ID_SELECT_ALL), false);

        // ---- rows ----
        const auto& configs = store_.configs();
        int y = listHead.bottom() + theme::M.px(6) - importScroll_;
        HRGN clip = ::CreateRectRgn(list.left(), y, list.right(), list.bottom() - theme::M.px(8));
        ::SelectClipRgn(c.dc(), clip);
        for (size_t i = 0; i < configs.size(); ++i) {
            Rect row{list.x + theme::M.px(8), y, list.w - theme::M.px(16), importRowH()};
            y += importRowH();
            if (row.bottom() < list.top()) continue;   // scrolled out above
            if (row.top() > list.bottom()) break;

            int id = ID_EXPORT_ROW_FIRST + (int)i;
            addHit(row, id);
            if (isHovered(id))
                c.fillRound(row, theme::M.radiusSmall,
                            theme::blend(theme::CardBg, RGB(0, 0, 0), 8));

            Rect box{row.x + theme::M.px(10), row.cy() - theme::M.px(9), theme::M.px(18),
                     theme::M.px(18)};
            bool checked = i < selected_.size() && selected_[i];
            if (checked) {
                c.fillRound(box, theme::M.radiusSmall, theme::Accent);
                c.glyph(box, shell::glyphs::kCheck, theme::TextOnAccent, 11);
            } else {
                c.fillRound(box, theme::M.radiusSmall, theme::CardBg);
                c.strokeRound(box, theme::M.radiusSmall, theme::BorderStrong);
            }

            // The id is pinned to the right; the name takes whatever is left.
            Rect meta{row.right() - theme::M.px(10) - idColW(), row.y, idColW(), row.h};
            int nameX = box.right() + theme::M.px(10);
            Rect name{nameX, row.y, meta.x - nameX - theme::M.px(10), row.h};
            c.text(name, util::ellipsize(c.dc(), configs[i].name, name.w, theme::fontBody()), theme::TextPrimary,
                   theme::fontBody(), DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            c.text(meta, configs[i].id, theme::TextTertiary, theme::fontCaption(),
                   DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
        ::SelectClipRgn(c.dc(), nullptr);
        ::DeleteObject(clip);

        if (configs.empty()) {
            c.text(Rect{list.x, list.cy() - theme::M.px(10), list.w, theme::M.px(20)},
                   T(Str::NoConfigs), theme::TextTertiary, theme::fontBody(),
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }

        // ---- status line + footer ----
        if (!status_.empty()) {
            Rect status{theme::M.px(24), client.h - theme::M.px(64),
                        client.w - theme::M.px(24) * 2 - theme::M.px(120), theme::M.px(20)};
            COLORREF col = statusOk_ ? theme::Success : theme::Danger;
            c.glyph(Rect{status.x, status.y, theme::M.px(14), status.h},
                    statusOk_ ? shell::glyphs::kCheck : shell::glyphs::kInfo, col, 11);
            c.text(Rect{status.x + theme::M.px(18), status.y, status.w - theme::M.px(18),
                        status.h},
                   status_, col, theme::fontCaption(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }

        int bw = theme::M.px(100);
        int bh = theme::M.px(36);
        Rect closeBtn{client.w - theme::M.px(24) - bw, client.h - theme::M.px(20) - bh, bw, bh};
        addHit(closeBtn, ID_CANCEL);
        shell::button(c, closeBtn, T(Str::Close), shell::ButtonStyle::Secondary,
                      isHovered(ID_CANCEL), isPressed(ID_CANCEL), false);
    }

    // A card with a glyph + title on the first row, the action button pinned to
    // the right of that same row, and the hint on its own row underneath. The
    // hint therefore never has to share vertical space with the button.
    void drawActionCard(Canvas& c, const Rect& card, wchar_t glyph, const std::wstring& title,
                        const std::wstring& hint, const std::wstring& buttonLabel, int buttonId) {
        shell::card(c, card);

        int btnW = theme::M.px(126);
        int btnH = theme::M.px(36);
        Rect btn{card.right() - theme::M.px(14) - btnW, card.y + theme::M.px(14), btnW, btnH};

        Rect icon{card.x + theme::M.px(16), btn.y + (btnH - theme::M.px(22)) / 2, theme::M.px(22),
                  theme::M.px(22)};
        c.glyph(icon, glyph, theme::Accent, 15);

        Rect titleRect{icon.right() + theme::M.px(10), btn.y, btn.x - icon.right() -
                                                                 theme::M.px(20),
                       btnH};
        c.text(titleRect, title, theme::TextPrimary, theme::fontBodyBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        addHit(btn, buttonId);
        shell::button(c, btn, buttonLabel, shell::ButtonStyle::Primary, isHovered(buttonId),
                      isPressed(buttonId), false, glyph);

        Rect hintRect{card.x + theme::M.px(16), btn.bottom() + theme::M.px(10),
                      card.w - theme::M.px(32), theme::M.px(20)};
        c.text(hintRect, hint, theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }

    void onClick(int id) override {
        const auto& configs = store_.configs();

        if (id >= ID_EXPORT_ROW_FIRST) {
            size_t index = (size_t)(id - ID_EXPORT_ROW_FIRST);
            if (index < configs.size()) {
                if (selected_.size() < configs.size()) selected_.resize(configs.size(), true);
                selected_[index] = !selected_[index];
            }
            invalidate();
            return;
        }

        switch (id) {
            case ID_IMPORT_PICK: {
                auto files = pickFiles(hwnd(), T(Str::ImportSection), L"YAML", L"*.yaml");
                if (files.empty()) break;
                int ok = 0;
                std::wstring lastError;
                for (const auto& f : files) {
                    std::wstring assigned, error;
                    if (store_.importConfig(f, assigned, error)) {
                        ++ok;
                        if (!assigned.empty()) importedIds_.push_back(assigned);
                    } else if (lastError.empty()) {
                        lastError = error;
                    }
                }
                if (ok > 0) {
                    statusOk_ = true;
                    status_ = util::format(L"%s %d / %d", T(Str::ImportedCount), ok,
                                           (int)files.size());
                    changed_ = true;
                } else {
                    statusOk_ = false;
                    status_ = lastError.empty() ? std::wstring(T(Str::ImportError)) : lastError;
                }
                selected_.assign(store_.configs().size(), true);
                invalidate();
                return;
            }

            case ID_EXPORT_PICK: {
                std::vector<size_t> chosen;
                for (size_t i = 0; i < configs.size() && i < selected_.size(); ++i)
                    if (selected_[i]) chosen.push_back(i);
                if (chosen.empty()) {
                    statusOk_ = false;
                    status_ = T(Str::PleaseSelectParam);
                    break;
                }
                std::wstring dir;
                if (!pickFolder(hwnd(), T(Str::ExportSection), dir)) break;

                int ok = 0;
                for (size_t i : chosen) {
                    std::wstring stem = configs[i].name.empty() ? configs[i].id : configs[i].name;
                    // Sanitise the name so it is always a legal file name.
                    for (wchar_t& ch : stem)
                        if (wcschr(L"\\/:*?\"<>|", ch)) ch = L'_';
                    std::wstring path = paths::suggestExportFile(dir, stem);
                    std::wstring error;
                    if (store_.exportConfig(configs[i], path, error)) ++ok;
                }
                statusOk_ = ok == (int)chosen.size();
                status_ = util::format(L"%s %d / %d", T(Str::Exported), ok, (int)chosen.size());
                break;
            }

            case ID_SELECT_ALL: {
                if (selected_.size() < configs.size()) selected_.resize(configs.size(), true);
                bool allOn = true;
                for (size_t i = 0; i < configs.size(); ++i)
                    if (i < selected_.size() && !selected_[i]) allOn = false;
                selected_.assign(configs.size(), !allOn);
                break;
            }

            case ID_CANCEL:
                close(changed_ ? DialogResult::Ok : DialogResult::Cancel);
                return;

            default:
                break;
        }
        invalidate();
    }

private:
    store::Store& store_;
    std::vector<bool> selected_;
    int importScroll_ = 0;
    std::vector<std::wstring> importedIds_;
    std::wstring status_;
    bool statusOk_ = false;
    bool changed_ = false;
};

}  // namespace

bool importExportDialog(HWND owner, store::Store& store,
                        std::vector<std::wstring>& importedIds) {
    ImportExportDialog dlg(store);
    dlg.run(owner, T(Str::ImportExportTitle), theme::M.px(820), theme::M.px(640));
    return dlg.takeChanged(importedIds);
}

// ============================================================================
//  Help (sub-view 5)
// ============================================================================
namespace {

class HelpDialog : public Dialog {
public:
    explicit HelpDialog(const HelpContext& ctx) : ctx_(ctx) {}

protected:
    void onPaint(Canvas& c, const Rect& client) override {
        Rect header{0, 0, client.w, theme::M.px(56)};
        c.fill(header, theme::LayerBg);
        c.line(0, header.bottom() - 1, client.w, header.bottom() - 1, theme::Border);
        c.text(Rect{theme::M.px(22), 0, client.w - theme::M.px(60), header.h}, T(Str::HelpTitle),
               theme::TextPrimary, theme::fontSubtitleBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        int x = theme::M.px(24);
        int w = client.w - theme::M.px(48);
        int y = theme::M.px(56) + theme::M.px(18);

        auto section = [&](Str title, Str body, int height) {
            Rect card{x, y, w, height};
            shell::card(c, card);
            c.text(Rect{card.x + theme::M.px(16), card.y + theme::M.px(10),
                        card.w - theme::M.px(32), theme::M.px(22)},
                   T(title), theme::TextPrimary, theme::fontBodyBold(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            c.textBlock(Rect{card.x + theme::M.px(16), card.y + theme::M.px(36),
                             card.w - theme::M.px(32), height - theme::M.px(46)},
                        T(body), theme::TextSecondary, theme::fontBody(),
                        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
            y += height + theme::M.px(12);
        };

        section(Str::HelpQuickStart, Str::HelpQuickStartBody, theme::M.px(140));
        section(Str::HelpParams, Str::HelpParamsBody, theme::M.px(104));
        section(Str::HelpTroubleshoot, Str::HelpTroubleshootBody, theme::M.px(150));

        // About strip with the paths that users need to know.
        Rect about{x, y, w, theme::M.px(74)};
        shell::card(c, about);
        c.text(Rect{about.x + theme::M.px(16), about.y + theme::M.px(10),
                    about.w - theme::M.px(32), theme::M.px(20)},
               T(Str::HelpAbout), theme::TextPrimary, theme::fontBodyBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        c.text(Rect{about.x + theme::M.px(16), about.y + theme::M.px(32),
                    about.w - theme::M.px(32), theme::M.px(18)},
               util::format(L"%s %s", T(Str::HelpVersion), ctx_.version.c_str()),
               theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        c.text(Rect{about.x + theme::M.px(16), about.y + theme::M.px(50),
                    about.w - theme::M.px(180), theme::M.px(18)},
               util::format(L"%s %s", T(Str::HelpDataDir), ctx_.dataDir.c_str()),
               theme::TextTertiary, theme::fontCaption(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

        Rect openBtn{about.right() - theme::M.px(16) - theme::M.px(150), about.y + theme::M.px(24),
                     theme::M.px(150), theme::M.px(30)};
        addHit(openBtn, ID_OPEN_DATA);
        shell::button(c, openBtn, T(Str::OpenDataDir), shell::ButtonStyle::Secondary,
                      isHovered(ID_OPEN_DATA), isPressed(ID_OPEN_DATA), false,
                      shell::glyphs::kFolder);

        int bh = theme::M.px(34);
        int bw = theme::M.px(100);
        Rect closeBtn{client.w - theme::M.px(24) - bw, client.h - theme::M.px(18) - bh, bw, bh};
        addHit(closeBtn, ID_CANCEL);
        shell::button(c, closeBtn, T(Str::Close), shell::ButtonStyle::Secondary,
                      isHovered(ID_CANCEL), isPressed(ID_CANCEL), false);
    }

    void onClick(int id) override {
        if (id == ID_OPEN_DATA) {
            ::ShellExecuteW(hwnd(), L"open", ctx_.dataDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return;
        }
        if (id == ID_CANCEL) close(DialogResult::Ok);
    }

private:
    HelpContext ctx_;
};

}  // namespace

bool helpDialog(HWND owner, const HelpContext& ctx) {
    HelpDialog dlg(ctx);
    return dlg.run(owner, T(Str::HelpTitle), theme::M.px(720), theme::M.px(700), true);
}

// ============================================================================
//  Log viewer
// ============================================================================
namespace {

class LogDialog : public Dialog {
public:
    explicit LogDialog(std::vector<std::wstring> lines)
        : lines_(std::move(lines)) {}
    LogDialog(std::vector<std::wstring> lines,
              std::function<std::vector<std::wstring>()> provider,
              const std::wstring& emptyHint)
        : lines_(std::move(lines)), provider_(std::move(provider)),
          emptyHint_(emptyHint) {
        // Pages depend on the console area, which is only known at paint time
        // (it moves with DPI and resizing). -1 means "not computed yet"; the
        // first paint lands on the newest lines.
        page_ = -1;
    }

protected:
    void onLayout() override {
        // Live mode: pull the newest lines a couple of times a second. The
        // log view used to be a frozen snapshot, which read as "blank" when
        // it was opened before the server had printed anything.
        if (provider_) ::SetTimer(hwnd(), 1, 500, nullptr);
    }

    void onDestroy() override {
        ::KillTimer(hwnd(), 1);
    }
    void onPaint(Canvas& c, const Rect& client) override {
        Rect header{0, 0, client.w, theme::M.px(56)};
        c.fill(header, theme::LayerBg);
        c.line(0, header.bottom() - 1, client.w, header.bottom() - 1, theme::Border);
        c.text(Rect{theme::M.px(22), 0, client.w - theme::M.px(260), header.h}, T(Str::RunLog),
               theme::TextPrimary, theme::fontSubtitleBold(),
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        // Page indicator + navigation.
        Rect info{client.w - theme::M.px(260), 0, theme::M.px(140), header.h};
        c.text(info, util::format(L"%d / %d", page_ + 1, pages_), theme::TextTertiary,
               theme::fontCaption(), DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        Rect prev{client.w - theme::M.px(112), header.cy() - theme::M.px(13), theme::M.px(28),
                  theme::M.px(26)};
        Rect next{prev.right() + theme::M.px(6), prev.y, prev.w, prev.h};
        Rect bottom{next.right() + theme::M.px(6), prev.y, theme::M.px(28), prev.h};
        addHit(prev, ID_PREV);
        addHit(next, ID_NEXT);
        addHit(bottom, ID_BOTTOM);
        c.glyph(prev, shell::glyphs::kBack, page_ > 0 ? theme::TextPrimary : theme::TextDisabled,
                13);
        c.glyph(next, shell::glyphs::kRefresh,
                page_ < pages_ - 1 ? theme::TextPrimary : theme::TextDisabled, 13);
        c.glyph(bottom, shell::glyphs::kImport, theme::TextPrimary, 13);

        // Console surface. A fixed light grey read as a glaring white slab in
        // the dark theme, so the muted field colour tracks the palette.
        Rect area{theme::M.px(16), theme::M.px(56) + theme::M.px(12),
                  client.w - theme::M.px(32),
                  client.h - theme::M.px(56) - theme::M.px(70)};
        c.fillRound(area, theme::M.radiusSmall, theme::fieldBack(false));
        c.strokeRound(area, theme::M.radiusSmall, theme::Border);

        // The page split must come from the real capacity of the console area.
        // The constructor's constant guess (40 lines) ignored DPI and resizing:
        // on a 175% display one page only fits ~24 lines, so every page skipped
        // its tail and those log lines could never be seen.
        // Live mode pulls the current lines; snapshot mode uses the stored copy.
        const std::vector<std::wstring> lines =
            provider_ ? provider_() : lines_;
        int lineH = theme::M.px(18);
        int perPage = std::max(1, (area.h - theme::M.px(16)) / lineH);
        int pages = std::max(1, (int)((lines.size() + perPage - 1) / perPage));
        if (pages != pages_) {
            pages_ = pages;
            page_ = page_ < 0 ? pages_ - 1 : std::clamp(page_, 0, pages_ - 1);
        }
        size_t start = (size_t)page_ * (size_t)perPage;
        if (start > lines.size()) start = lines.size();

        Rect inner = area.inset(theme::M.px(8));
        int shown = 0;
        for (size_t i = start; i < lines.size() && shown < perPage; ++i, ++shown) {
            Rect lr{inner.x, inner.y + shown * lineH, inner.w, lineH};
            COLORREF col = theme::TextSecondary;
            std::wstring low = util::lower(lines[i]);
            if (util::contains(low, L"error") || util::contains(low, L"failed"))
                col = theme::Danger;
            else if (util::contains(low, L"warn")) col = theme::Warning;
            else if (util::contains(low, L"listening") || util::contains(low, L"server is"))
                col = theme::Success;
            c.text(lr, util::ellipsize(c.dc(), lines[i], lr.w, theme::fontMono()), col, theme::fontMono(),
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        }
        if (lines.empty()) {
            c.textBlock(inner, emptyHint_.empty() ? T(Str::LogEmpty) : emptyHint_,
                        theme::TextTertiary, theme::fontBody());
        }

        int bw = theme::M.px(100);
        int bh = theme::M.px(34);
        Rect closeBtn{client.w - theme::M.px(16) - bw, client.h - theme::M.px(16) - bh, bw, bh};
        addHit(closeBtn, ID_CANCEL);
        shell::button(c, closeBtn, T(Str::Close), shell::ButtonStyle::Secondary,
                      isHovered(ID_CANCEL), isPressed(ID_CANCEL), false);
    }

    void onMouseWheel(int delta, int, int) override {
        if (delta > 0 && page_ > 0) --page_;
        else if (delta < 0 && page_ < pages_ - 1) ++page_;
        invalidate();
    }

    void onClick(int id) override {
        switch (id) {
            case ID_PREV:
                if (page_ > 0) --page_;
                break;
            case ID_NEXT:
                if (page_ < pages_ - 1) ++page_;
                break;
            case ID_BOTTOM:
                page_ = pages_ - 1;
                break;
            case ID_CANCEL:
                close(DialogResult::Ok);
                return;
            default:
                break;
        }
        invalidate();
    }

private:
    std::vector<std::wstring> lines_;
    std::function<std::vector<std::wstring>()> provider_;
    std::wstring emptyHint_;
    int page_ = 0;
    int pages_ = 1;
};

}  // namespace

LogViewer::LogViewer(std::vector<std::wstring> lines) : lines_(std::move(lines)) {}

bool LogViewer::show(HWND owner, const std::wstring& title) {
    LogDialog dlg(lines_, provider_, emptyHint_);
    return dlg.run(owner, title, theme::M.px(880), theme::M.px(620), true);
}

}  // namespace views