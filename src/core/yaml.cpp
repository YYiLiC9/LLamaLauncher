#include "core/yaml.h"

#include <cstdlib>
#include <utility>

#include "core/util.h"

namespace yaml {

// --------------------------------------------------------------------- Node --
Node Node::scalar(const std::wstring& v) {
    Node n(Type::Scalar);
    n.scalar_ = v;
    return n;
}

void Node::set(const std::wstring& key, Node value) {
    for (auto& kv : map_) {
        if (kv.first == key) {
            kv.second = std::move(value);
            return;
        }
    }
    if (type_ == Type::Scalar || type_ == Type::Null) type_ = Type::Map;
    if (type_ != Type::Map) type_ = Type::Map;
    map_.emplace_back(key, std::move(value));
}

void Node::set(const std::wstring& key, const std::wstring& value) { set(key, scalar(value)); }

void Node::push(Node value) {
    type_ = Type::Seq;
    seq_.push_back(std::move(value));
}

static const Node kEmptyNode{};

const Node& Node::operator[](const std::wstring& key) const {
    for (const auto& kv : map_)
        if (kv.first == key) return kv.second;
    return kEmptyNode;
}

const Node& Node::at(size_t index) const {
    if (index >= seq_.size()) return kEmptyNode;
    return seq_[index];
}

size_t Node::size() const { return isSeq() ? seq_.size() : map_.size(); }

bool Node::has(const std::wstring& key) const {
    for (const auto& kv : map_)
        if (kv.first == key) return true;
    return false;
}

std::wstring Node::str(const std::wstring& key, const std::wstring& fallback) const {
    const Node& n = (*this)[key];
    if (!n.isScalar()) return fallback;
    return n.asString();
}

int Node::i32(const std::wstring& key, int fallback) const {
    const Node& n = (*this)[key];
    if (!n.isScalar()) return fallback;
    const std::wstring& s = n.asString();
    if (s.empty()) return fallback;
    wchar_t* end = nullptr;
    long v = ::wcstol(s.c_str(), &end, 10);
    if (end == s.c_str()) return fallback;
    return (int)v;
}

int64_t Node::i64(const std::wstring& key, int64_t fallback) const {
    const Node& n = (*this)[key];
    if (!n.isScalar()) return fallback;
    const std::wstring& s = n.asString();
    if (s.empty()) return fallback;
    wchar_t* end = nullptr;
    // i32 goes through a 32-bit long, which epochs past 2038 do not survive.
    int64_t v = ::_wcstoi64(s.c_str(), &end, 10);
    if (end == s.c_str()) return fallback;
    return v;
}

bool Node::boolean(const std::wstring& key, bool fallback) const {
    const Node& n = (*this)[key];
    if (!n.isScalar()) return fallback;
    std::wstring v = util::lower(util::trim(n.asString()));
    if (v == L"true" || v == L"yes" || v == L"on" || v == L"1") return true;
    if (v == L"false" || v == L"no" || v == L"off" || v == L"0") return false;
    return fallback;
}

std::wstring Node::strAt(size_t index, const std::wstring& fallback) const {
    const Node& n = at(index);
    if (!n.isScalar()) return fallback;
    return n.asString();
}

// ------------------------------------------------------------------- quoting --
std::wstring quote(const std::wstring& value) {
    std::wstring out = L"\"";
    for (wchar_t c : value) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'"':  out += L"\\\""; break;
            case L'\n': out += L"\\n"; break;
            case L'\r': out += L"\\r"; break;
            case L'\t': out += L"\\t"; break;
            default:
                if (c < 0x20)
                    out += util::format(L"\\x%02X", (unsigned)c);
                else
                    out.push_back(c);
        }
    }
    out += L"\"";
    return out;
}

bool needsQuotes(const std::wstring& value) {
    if (value.empty()) return true;
    if (value.front() == L' ' || value.back() == L' ') return true;
    if (value.find(L'\n') != std::wstring::npos) return true;
    if (value.find(L'\t') != std::wstring::npos) return true;
    if (value.find(L": ") != std::wstring::npos) return true;
    if (value.find(L" #") != std::wstring::npos) return true;
    if (value.find(L'"') != std::wstring::npos) return true;

    wchar_t c = value.front();
    if (c == L'&' || c == L'*' || c == L'!' || c == L'|' || c == L'>' || c == L'\'' || c == L'%' ||
        c == L'@' || c == L'`' || c == L'[' || c == L']' || c == L'{' || c == L'}' || c == L',' ||
        c == L'#' || c == L'?')
        return true;
    // A leading '-' is a sequence marker only when followed by a space.
    if (c == L'-' && (value.size() == 1 || value[1] == L' ')) return true;
    if (c == L':' && (value.size() == 1 || value[1] == L' ')) return true;

    std::wstring low = util::lower(value);
    if (low == L"true" || low == L"false" || low == L"null" || low == L"yes" || low == L"no" ||
        low == L"on" || low == L"off" || low == L"~")
        return true;
    return false;
}

// -------------------------------------------------------------------- parse --
namespace {

struct Line {
    enum class Kind { Mapping, SeqItem };
    Kind kind = Kind::Mapping;
    int indent = 0;
    std::wstring key;                  // mapping key, or the first key of "- key: value"
    std::wstring value;                // inline scalar value (when hasInline)
    bool hasInline = false;
    std::wstring blockScalar;          // resolved block-scalar text
    bool usesBlockScalar = false;
};

// Removes a trailing comment, honouring quoted sections.
std::wstring stripComment(const std::wstring& s) {
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (inDouble) {
            if (c == L'\\')
                ++i;
            else if (c == L'"')
                inDouble = false;
            continue;
        }
        if (inSingle) {
            if (c == L'\'') inSingle = false;
            continue;
        }
        if (c == L'"')
            inDouble = true;
        else if (c == L'\'')
            inSingle = true;
        else if (c == L'#' && (i == 0 || s[i - 1] == L' ' || s[i - 1] == L'\t'))
            return s.substr(0, i);
    }
    return s;
}

int countIndent(const std::wstring& s) {
    int n = 0;
    for (wchar_t c : s) {
        if (c == L' ') ++n;
        else if (c == L'\t') n += 2;
        else break;
    }
    return n;
}

std::wstring unquote(const std::wstring& s) {
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"') {
        std::wstring out;
        for (size_t i = 1; i + 1 < s.size(); ++i) {
            wchar_t c = s[i];
            if (c == L'\\' && i + 2 <= s.size() - 1) {
                wchar_t n = s[++i];
                switch (n) {
                    case L'n': out.push_back(L'\n'); break;
                    case L'r': out.push_back(L'\r'); break;
                    case L't': out.push_back(L'\t'); break;
                    case L'"': out.push_back(L'"'); break;
                    case L'\\': out.push_back(L'\\'); break;
                    case L'x': {
                        if (i + 2 < s.size()) {
                            wchar_t hex[3] = {s[i + 1], s[i + 2], 0};
                            out.push_back((wchar_t)wcstol(hex, nullptr, 16));
                            i += 2;
                        }
                        break;
                    }
                    default: out.push_back(n);
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }
    if (s.size() >= 2 && s.front() == L'\'' && s.back() == L'\'') {
        return util::replaceAll(s.substr(1, s.size() - 2), L"''", L"'");
    }
    return s;
}

// Splits "key: value" at the first top-level colon. Returns false when the text
// is not a mapping entry.
bool splitKeyValue(const std::wstring& text, std::wstring& key, std::wstring& value) {
    bool inSingle = false, inDouble = false;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (inDouble) {
            if (c == L'\\')
                ++i;
            else if (c == L'"')
                inDouble = false;
            continue;
        }
        if (inSingle) {
            if (c == L'\'') inSingle = false;
            continue;
        }
        if (c == L'"')
            inDouble = true;
        else if (c == L'\'')
            inSingle = true;
        else if (c == L':' && (i + 1 >= text.size() || text[i + 1] == L' ' || text[i + 1] == L'\t')) {
            key = util::trim(text.substr(0, i));
            value = util::trim(text.substr(i + 1));
            return !key.empty();
        }
    }
    return false;
}

bool isBlockIndicator(const std::wstring& v) {
    return v == L"|" || v == L"|-" || v == L"|+" || v == L">" || v == L">-" || v == L">+";
}

class Parser {
public:
    explicit Parser(const std::vector<Line>& lines) : lines_(lines) {}

    Node parseRoot() {
        if (lines_.empty()) return Node();
        return parseBlock(lines_[0].indent);
    }

private:
    const std::vector<Line>& lines_;
    size_t pos_ = 0;

    Node parseBlock(int indent) {
        if (pos_ >= lines_.size()) return Node();
        if (lines_[pos_].indent < indent) return Node();
        if (lines_[pos_].kind == Line::Kind::SeqItem) return parseSeq(lines_[pos_].indent);
        return parseMap(lines_[pos_].indent);
    }

    Node parseSeq(int indent) {
        Node node = Node::seq();
        while (pos_ < lines_.size()) {
            const Line& l = lines_[pos_];
            if (l.indent < indent || l.kind != Line::Kind::SeqItem) break;

            if (l.usesBlockScalar) {
                node.push(Node::scalar(l.blockScalar));
                ++pos_;
                continue;
            }
            if (l.hasInline && !l.key.empty()) {
                // "- key: value" opens a mapping that continues on the
                // following, deeper-indented lines.
                Node item = Node::map();
                item.set(l.key, Node::scalar(unquote(l.value)));
                ++pos_;
                if (pos_ < lines_.size() && lines_[pos_].indent > l.indent &&
                    lines_[pos_].kind == Line::Kind::Mapping) {
                    Node rest = parseMap(lines_[pos_].indent);
                    for (const auto& kv : rest.entries()) item.set(kv.first, kv.second);
                }
                node.push(std::move(item));
            } else if (l.hasInline) {
                node.push(Node::scalar(unquote(l.value)));
                ++pos_;
            } else {
                ++pos_;
                if (pos_ < lines_.size() && lines_[pos_].indent > indent) {
                    node.push(parseBlock(lines_[pos_].indent));
                } else {
                    node.push(Node());
                }
            }
        }
        return node;
    }

    Node parseMap(int indent) {
        Node node = Node::map();
        while (pos_ < lines_.size()) {
            const Line& l = lines_[pos_];
            if (l.indent < indent) break;
            if (l.indent > indent) {
                ++pos_;   // defensive: skip stray indentation
                continue;
            }
            if (l.kind != Line::Kind::Mapping) break;
            if (l.key.empty()) {
                ++pos_;
                continue;
            }

            if (l.usesBlockScalar) {
                node.set(l.key, Node::scalar(l.blockScalar));
                ++pos_;
                continue;
            }
            if (l.hasInline) {
                node.set(l.key, Node::scalar(unquote(l.value)));
                ++pos_;
                continue;
            }
            ++pos_;
            if (pos_ < lines_.size() && lines_[pos_].indent > indent) {
                node.set(l.key, parseBlock(lines_[pos_].indent));
            } else {
                node.set(l.key, Node());
            }
        }
        return node;
    }
};

}  // namespace

bool parse(const std::wstring& text, Node& out, std::wstring& error) {
    std::vector<std::wstring> rawLines = util::split(text, L'\n');
    std::vector<Line> lines;

    for (size_t i = 0; i < rawLines.size(); ++i) {
        std::wstring raw = rawLines[i];
        if (!raw.empty() && raw.back() == L'\r') raw.pop_back();

        int indent = countIndent(raw);
        size_t lead = 0;
        while (lead < raw.size() && (raw[lead] == L' ' || raw[lead] == L'\t')) ++lead;
        std::wstring body = raw.substr(lead);
        if (body.empty()) continue;
        if (body == L"---" || body == L"...") continue;

        Line line;
        line.indent = indent;

        // ---------------- sequence item ----------------
        if (body[0] == L'-' && (body.size() == 1 || body[1] == L' ' || body[1] == L'\t')) {
            line.kind = Line::Kind::SeqItem;
            std::wstring rest = body.size() > 1 ? util::trim(body.substr(1)) : L"";
            if (rest.empty()) {
                line.hasInline = false;
            } else {
                std::wstring k, v;
                if (splitKeyValue(rest, k, v)) {
                    line.key = unquote(k);
                    if (isBlockIndicator(v)) {
                        line.usesBlockScalar = true;
                    } else {
                        line.value = v;
                        line.hasInline = true;
                    }
                } else {
                    line.value = rest;
                    line.hasInline = true;
                }
            }
            lines.push_back(line);
            continue;
        }

        // ---------------- mapping entry ----------------
        std::wstring clean = util::trim(stripComment(body));
        if (clean.empty()) continue;

        std::wstring k, v;
        if (!splitKeyValue(clean, k, v)) {
            line.key = unquote(clean);
            lines.push_back(line);
            continue;
        }
        line.key = unquote(k);
        if (isBlockIndicator(v)) {
            line.usesBlockScalar = true;
            // Everything more indented than this key belongs to the scalar.
            std::wstring collected;
            int baseIndent = indent;
            size_t j = i + 1;
            for (; j < rawLines.size(); ++j) {
                std::wstring sub = rawLines[j];
                if (!sub.empty() && sub.back() == L'\r') sub.pop_back();
                size_t sLead = 0;
                while (sLead < sub.size() && (sub[sLead] == L' ' || sub[sLead] == L'\t')) ++sLead;
                if (sLead <= (size_t)baseIndent && !sub.substr(sLead).empty()) break;
                if (util::trim(sub).empty()) {
                    collected += L"\n";
                    continue;
                }
                if (!collected.empty() && collected.back() != L'\n') collected += L"\n";
                collected += sub.substr(sLead);
            }
            // Trim a trailing newline produced by blank separator lines.
            while (!collected.empty() && collected.back() == L'\n') collected.pop_back();
            line.blockScalar = collected;
            i = (j > 0) ? j - 1 : i;
            lines.push_back(line);
            continue;
        }
        // An empty value ("params:") is a placeholder for the indented block
        // that follows, not an inline scalar. Marking it inline made the
        // parser store an empty string for the key and then drop the entire
        // block as "stray indentation" - which is how every saved parameter
        // list silently reverted to defaults on load.
        line.value = v;
        line.hasInline = !v.empty();
        lines.push_back(line);
    }

    if (lines.empty()) {
        out = Node();
        return true;
    }

    Parser p(lines);
    out = p.parseRoot();
    (void)error;
    return true;
}

// --------------------------------------------------------------------- dump --
namespace {

void dumpMap(std::wstring& out, const Node& node, int indent);

void dumpScalarOrNested(std::wstring& out, const Node& value, int indent, const std::wstring& prefix) {
    // `prefix` carries the "- key:" or "key:" text already written.
    if (value.isScalar()) {
        const std::wstring& s = value.asString();
        if (s.find(L'\n') != std::wstring::npos) {
            out += prefix + L" |-\n";
            std::wstring pad((size_t)(indent + 1) * 2, L' ');
            for (const auto& l : util::split(s, L'\n')) out += pad + l + L"\n";
        } else {
            out += prefix + L" " + (needsQuotes(s) ? quote(s) : s) + L"\n";
        }
        return;
    }
    if (value.isNull()) {
        out += prefix + L"\n";
        return;
    }
    out += prefix + L"\n";
    if (value.isMap()) dumpMap(out, value, indent + 1);
}

void dumpMap(std::wstring& out, const Node& node, int indent) {
    std::wstring pad((size_t)indent * 2, L' ');
    for (const auto& [key, value] : node.entries()) {
        std::wstring k = needsQuotes(key) ? quote(key) : key;

        if (value.isSeq()) {
            if (value.items().empty()) {
                out += pad + k + L": []\n";
                continue;
            }
            out += pad + k + L":\n";
            std::wstring itemPad((size_t)(indent + 1) * 2, L' ');
            for (const Node& item : value.items()) {
                if (item.isScalar()) {
                    const std::wstring& s = item.asString();
                    out += itemPad + L"- " + (needsQuotes(s) ? quote(s) : s) + L"\n";
                } else if (item.isMap()) {
                    bool first = true;
                    for (const auto& [ik, iv] : item.entries()) {
                        std::wstring ikq = needsQuotes(ik) ? quote(ik) : ik;
                        std::wstring prefix =
                            first ? (itemPad + L"- " + ikq + L":") : (itemPad + L"  " + ikq + L":");
                        dumpScalarOrNested(out, iv, indent + 1, prefix);
                        first = false;
                    }
                    if (item.entries().empty()) out += itemPad + L"- {}\n";
                } else if (item.isNull()) {
                    out += itemPad + L"-\n";
                } else {
                    out += itemPad + L"-\n";
                    dumpMap(out, item, indent + 2);
                }
            }
            continue;
        }

        dumpScalarOrNested(out, value, indent, pad + k + L":");
    }
}

}  // namespace

std::wstring dump(const Node& node) {
    if (node.isNull()) return {};
    std::wstring out;
    if (node.isMap()) {
        dumpMap(out, node, 0);
        return out;
    }
    Node root = Node::map();
    root.set(L"value", node);
    dumpMap(out, root, 0);
    return out;
}

}  // namespace yaml