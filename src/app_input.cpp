// app_input.cpp - translating mouse input into actions.
//
// Hit regions are registered during painting, so this file only has to find the
// region under the cursor and dispatch the action it carries. That keeps input
// handling free of any geometry of its own.
#include "app.h"

#include <algorithm>
#include <cmath>

#include "core/i18n.h"
#include "core/paths.h"
#include "core/util.h"
#include "ui/theme.h"

using shell::Rect;

namespace app {

namespace {

// Extra identification so the export action can reuse the shared dialog.
bool editConfigDialog(HWND owner, store::Store& store, store::Config& config, bool isNew) {
    return views::paramEditorDialog(owner, store, config, isNew);
}

}  // namespace

int hitIndexAt(const std::vector<Hit>& hits, int x, int y) {
    // Later entries win: panels are painted back to front, so the last region
    // registered over a point is the visually topmost one.
    for (int i = (int)hits.size() - 1; i >= 0; --i) {
        const Hit& h = hits[(size_t)i];
        if (h.action == Action::None) continue;
        if (h.rect.contains(x, y)) return i;
    }
    return -1;
}

// Topmost slice of the memory card under the pointer, or -1. Slices are
// registered during the last paint, so this reads one frame old - the same
// contract as the button hit list.
int App::sliceTipAt(int x, int y) const {
    for (size_t i = 0; i < sliceTips_.size(); ++i) {
        if (sliceTips_[i].rect.contains(x, y)) return (int)i;
    }
    return -1;
}

void App::onMouseMove(int x, int y) {
    int idx = hitIndexAt(hits_, x, y);
    if (idx >= 0 && !hits_[(size_t)idx].enabled) idx = -1;
    int tip = sliceTipAt(x, y);
    if (idx != hoverIndex_ || tip != sliceHover_) {
        hoverIndex_ = idx;
        sliceHover_ = tip;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::onMouseLeave() {
    if (hoverIndex_ != -1 || pressIndex_ != -1 || sliceHover_ != -1) {
        hoverIndex_ = -1;
        pressIndex_ = -1;
        sliceHover_ = -1;
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void App::onLButtonDown(int x, int y) {
    pressIndex_ = hitIndexAt(hits_, x, y);
    if (pressIndex_ >= 0 && !hits_[(size_t)pressIndex_].enabled) pressIndex_ = -1;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::onLButtonUp(int x, int y) {
    int idx = hitIndexAt(hits_, x, y);
    if (idx >= 0 && !hits_[(size_t)idx].enabled) idx = -1;

    // A click only counts when press and release land on the same region.
    if (idx >= 0 && idx == pressIndex_) {
        Hit hit = hits_[(size_t)idx];
        pressIndex_ = -1;
        hoverIndex_ = -1;
        dispatch(hit);
        return;
    }
    pressIndex_ = -1;
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

void App::dispatch(const Hit& hit) {
    switch (hit.action) {
        case Action::SelectConfig: {
            if (selectedId_ != hit.payload) {
                selectedId_ = hit.payload;
                view_ = View::Detail;
                contentScroll_ = 0;
            }
            break;
        }

        case Action::NewConfig: {
            store::Config cfg;
            cfg.name.clear();
            cfg.params = store::defaultParams();
            // Give a fresh configuration the thread count and GPU layers that
            // suit this machine out of the box.
            SYSTEM_INFO si{};
            ::GetSystemInfo(&si);
            int cores = (int)si.dwNumberOfProcessors;
            if (cores > 0) {
                std::wstring threads = util::format(L"%d", std::max(1, cores / 2));
                for (store::Param& p : cfg.params) {
                    if (p.flag == L"-t") p.value = threads;
                    if (p.flag == L"-tb") p.value = threads;
                }
            }
            if (editConfigDialog(hwnd_, store_, cfg, true)) {
                refresh();
                if (!cfg.id.empty()) {
                    selectedId_ = cfg.id;
                    view_ = View::Detail;
                }
            }
            break;
        }

        case Action::Modify: {
            store::Config* cfg = store_.find(hit.payload);
            if (!cfg) break;
            store::Config copy = *cfg;
            if (editConfigDialog(hwnd_, store_, copy, false)) {
                refresh();
                selectedId_ = copy.id;
                view_ = View::Detail;
            }
            break;
        }

        case Action::Delete: {
            const store::Config* cfg = store_.find(hit.payload);
            if (!cfg) break;
            std::wstring body = util::format(L"%s\n\n%s", cfg->name.c_str(), T(Str::DeleteConfirmBody));
            if (views::confirm(hwnd_, T(Str::DeleteConfirmTitle), body, T(Str::Delete), true)) {
                if (store_.remove(hit.payload)) {
                    if (selectedId_ == hit.payload) clearSelection();
                    refresh();
                }
            }
            break;
        }

        case Action::Start:
            startConfig(hit.payload);
            break;

        case Action::Stop:
            stopServer();
            break;

        case Action::OpenSettings: {
            bool changed = false;
            if (views::settingsDialog(hwnd_, store_, changed)) {
                // The dialog previews the theme while it is open, but the
                // setting may have been abandoned with Cancel - so re-apply
                // whatever is actually stored, then repaint the whole window.
                theme::setThemeMode(theme::modeFromSetting(store_.settings().theme));
                applyChrome();
                refresh();
                placeSearchEdit();
                ::InvalidateRect(hwnd_, nullptr, TRUE);
            }
            break;
        }

        case Action::OpenImport: {
            // One entry point for both directions: the dialog shows the import
            // and export halves side by side.
            std::vector<std::wstring> imported;
            if (views::importExportDialog(hwnd_, store_, imported)) {
                refresh();
                if (!imported.empty()) {
                    selectedId_ = imported.front();
                    view_ = View::Detail;
                }
            }
            break;
        }

        case Action::OpenWeb:
            openChatPage();
            break;

        case Action::OpenInBrowser:
            openChatInBrowser();
            break;

        case Action::CloseChat:
            // Leave the embedded browser and go back to whatever the selection
            // implies: the live view while the server runs, else the detail view.
            setView(processAlive() ? View::Running : (selected() ? View::Detail : View::Welcome));
            break;

        case Action::ReloadChat:
            webView_.reload();
            break;

        case Action::MinimizeMonitor:
            // Shrink the live view to the corner ball; the monitor is simply
            // "not the active view" while the server runs.
            setView(selected() ? View::Detail : View::Welcome);
            break;

        case Action::RestoreMonitor:
            // The ball is always there; a monitor without a selected config
            // would snap back to Welcome, so pick the first one in that case.
            if (!selected() && !store_.configs().empty())
                selectedId_ = store_.configs().front().id;
            if (selected()) setView(View::Running);
            break;

        case Action::OpenHelp: {
            views::HelpContext ctx;
            ctx.version = L"1.0.0";
            ctx.dataDir = paths::dataRoot();
            ctx.llamaPath = store_.effectiveLlamaDir();
            views::helpDialog(hwnd_, ctx);
            break;
        }

        case Action::OpenChatPage:
            openChatPage();
            break;

        case Action::CopyCommand:
            copyCommandToClipboard();
            break;

        case Action::OpenLog:
            openLogWindow();
            break;

        case Action::OpenDataDir:
            openDataDir();
            break;

        case Action::ClearSearch:
            searchText_.clear();
            if (searchEdit_) ::SetWindowTextW(searchEdit_, L"");
            ::InvalidateRect(hwnd_, nullptr, FALSE);
            break;

        case Action::ToggleNotes:
            notesOpen_ = !notesOpen_;
            break;

        case Action::None:
        default:
            break;
    }
    // Several actions above switch the view without going through setView()
    // (SelectConfig, NewConfig, Modify, ...). Any of them can leave the chat
    // view, and the embedded browser is a child window that sits on top of
    // everything the canvas draws - without this call the "loading chat page"
    // pane stayed visible over the newly selected view.
    syncWebView();
    ::InvalidateRect(hwnd_, nullptr, FALSE);
}

}  // namespace app
