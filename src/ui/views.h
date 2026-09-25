// views.h - the dialogs and the parameter editor table.
#pragma once

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "core/catalog.h"
#include "core/store.h"
#include "ui/dialog.h"
#include "ui/shell.h"

namespace views {

using shell::Rect;

// Widget ids. Each dialog only registers the ids it actually draws, so a single
// flat enum is enough and there is nothing to keep in sync.
enum : int {
    ID_NONE = 0,
    ID_OK = 1,
    ID_CANCEL = 2,
    ID_BROWSE_FILE = 3,
    ID_BROWSE_FOLDER = 4,
    ID_AUTODETECT = 5,
    ID_BACKUP_NOW = 6,
    ID_LANG_ZH = 7,
    ID_LANG_EN = 8,
    ID_IMPORT_PICK = 9,
    ID_EXPORT_PICK = 10,
    ID_SELECT_ALL = 11,
    ID_OPEN_DATA = 12,
    ID_BACKUP_ALL = 13,
    ID_ADD_ROW = 14,
    ID_THEME_SYSTEM = 15,
    ID_THEME_LIGHT = 16,
    ID_THEME_DARK = 17,
    ID_EXPORT_ROW_FIRST = 100,
    // The export list addresses configurations as ID_EXPORT_ROW_FIRST + index.
    // It starts well clear of the plain ids above so the two spaces can never
    // overlap.
    // The group rail of the parameter editor addresses groups as
    // ID_GROUP_FIRST + index; index 0 is the "all parameters" pseudo group.
    ID_GROUP_FIRST = 500,
    ID_PREV = 600,
    ID_NEXT = 601,
    ID_BOTTOM = 602,
    // Parameter editor rows are addressed as
    // ID_ROW_FIRST + index * ID_ROW_STRIDE + field.
    ID_ROW_FIRST = 1000,
};

constexpr int ID_ROW_STRIDE = 10;
enum : int {
    ROW_FLAG = 0,
    ROW_VALUE = 1,
    ROW_BROWSE = 2,
    ROW_REMOVE = 3,
    ROW_TOGGLE = 4,   // the on/off switch at the left of every parameter row
};

// Destructive or noteworthy actions go through this, so every confirmation in
// the app looks and behaves the same.
bool confirm(HWND owner, const std::wstring& title, const std::wstring& body,
             const std::wstring& okLabel, bool danger);

void message(HWND owner, const std::wstring& title, const std::wstring& body);

// File / folder pickers built on the common dialogs, so no extra dependency is
// needed and the OS supplies the modern picker UI.
bool pickFile(HWND owner, const std::wstring& title, const std::wstring& filterText,
              const std::wstring& filterPattern, const std::wstring& defaultExt,
              std::wstring& out);
std::vector<std::wstring> pickFiles(HWND owner, const std::wstring& title,
                                    const std::wstring& filterText,
                                    const std::wstring& filterPattern);
bool pickFolder(HWND owner, const std::wstring& title, std::wstring& out);

// Reserved ids for the parameter editor: a row is addressed as
// ID_ROW_FIRST + index * ID_ROW_STRIDE + field, and each row owns a small block
// of dynamic child controls.

// ---------------------------------------------------------------------------
// Dialogs. Each shows a modal window and returns true when the user applied the
// change. `owner` is disabled for the duration.
// ---------------------------------------------------------------------------
bool settingsDialog(HWND owner, store::Store& store, bool& languageChanged);

// `config` is edited in place; `isNew` selects the dialog title and whether the
// id is preserved.
bool paramEditorDialog(HWND owner, store::Store& store, store::Config& config, bool isNew);

// Import moves configurations in, export writes the selected ones out. They
// share one dialog (and one toolbar button) because the two halves answer the
// same question. `importedIds` receives the ids of anything newly imported.
bool importExportDialog(HWND owner, store::Store& store,
                        std::vector<std::wstring>& importedIds);

struct HelpContext {
    std::wstring version;
    std::wstring dataDir;
    std::wstring llamaPath;
};
bool helpDialog(HWND owner, const HelpContext& ctx);

// A read-only console view of the server output.
class LogViewer {
public:
    explicit LogViewer(std::vector<std::wstring> lines);
    bool show(HWND owner, const std::wstring& title);

private:
    std::vector<std::wstring> lines_;
};

}  // namespace views