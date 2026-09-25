// catalog.h - the catalogue of llama.cpp launch parameters.
//
// Built-in entries are shown as read-only rows in the editor (their flag text
// cannot be changed, which prevents a typo from breaking the launch) while
// their values remain editable. Anything the catalogue does not cover can be
// added as a custom row with a free-form flag.
#pragma once

#include <string>
#include <vector>

#include "core/i18n.h"

namespace catalog {

// Groups are used both for the sidebar of the editor and for chapter headings
// in the detail view.
enum class Group {
    Basic = 0,
    Sampling,
    Server,
    Performance,
    Gpu,
    Chat,
    Custom,
    Count,
};

struct Spec {
    const wchar_t* flag;         // canonical form used on the command line, e.g. "-m"
    const wchar_t* aliases;      // extra forms, e.g. "--model"
    const wchar_t* shortName;    // e.g. "-m" (may be empty)
    Group group;
    bool isToggle;               // "on" / empty; no value to type
    bool isNumber;
    bool valueIsFile;            // show a "File..." browse button
    bool valueIsDir;             // show a "Folder..." browse button
    const wchar_t* defaultVal;   // pre-filled value, empty means "not passed"
    const wchar_t* placeholder;  // hint text for the value box
    const wchar_t* enDesc;
    const wchar_t* zhDesc;
};

// All built-in parameters, in display order.
const std::vector<Spec>& specs();

const Spec* find(const std::wstring& flag);

// Human readable label such as "--model  (-m)".
std::wstring label(const Spec& spec);

const wchar_t* groupName(Group g);
const wchar_t* groupKey(Group g);
bool groupFromKey(const std::wstring& key, Group& out);

// Sensible defaults for a fresh configuration: one entry per built-in spec.
struct ParamValue {
    std::wstring flag;
    std::wstring value;
    std::wstring group;     // group key, or free text for custom rows
    std::wstring desc;
    bool custom = false;
};

std::vector<ParamValue> defaultParams();

// True when the value counts as "enabled" for a toggle.
bool toggleOn(const std::wstring& value);

}  // namespace catalog