// yaml.h - a deliberately small YAML subset.
//
// We only need to read and write configuration files that this program itself
// produces (plus hand-edited ones), so instead of pulling in a dependency we
// support the subset that matters:
//
//   * nested mappings and sequences, indentation based
//   * `- key: value` sequences of mappings
//   * plain, single-quoted and double-quoted scalars
//   * block scalars (`|`, `|-`, `>-`) for multi-line text such as notes
//   * `#` comments
//
// Every scalar is kept as a string, which removes any ambiguity when round
// tripping and keeps the on-disk file readable.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace yaml {

class Node {
public:
    enum class Type { Null, Scalar, Map, Seq };

    Node() = default;
    explicit Node(Type t) : type_(t) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isMap() const { return type_ == Type::Map; }
    bool isSeq() const { return type_ == Type::Seq; }
    bool isScalar() const { return type_ == Type::Scalar; }

    // ---- construction ----
    static Node map() { return Node(Type::Map); }
    static Node seq() { return Node(Type::Seq); }
    static Node scalar(const std::wstring& v);

    void set(const std::wstring& key, Node value);
    void set(const std::wstring& key, const std::wstring& value);
    void push(Node value);

    // ---- read ----
    const std::wstring& asString() const { return scalar_; }
    // Returns an empty node when the key is absent.
    const Node& operator[](const std::wstring& key) const;
    const Node& at(size_t index) const;
    size_t size() const;
    bool has(const std::wstring& key) const;

    const std::vector<std::pair<std::wstring, Node>>& entries() const { return map_; }
    const std::vector<Node>& items() const { return seq_; }

    // ---- typed conveniences ----
    std::wstring str(const std::wstring& key, const std::wstring& fallback = L"") const;
    std::wstring strAt(size_t index, const std::wstring& fallback = L"") const;
    int          i32(const std::wstring& key, int fallback = 0) const;
    // 64-bit variant: epoch seconds overflow a 32-bit long after 2038.
    int64_t      i64(const std::wstring& key, int64_t fallback = 0) const;
    bool         boolean(const std::wstring& key, bool fallback = false) const;

private:
    Type type_ = Type::Null;
    std::wstring scalar_;
    std::vector<std::pair<std::wstring, Node>> map_;
    std::vector<Node> seq_;
};

// Parses `text`. Returns false and fills `error` when the document cannot be
// understood.
bool parse(const std::wstring& text, Node& out, std::wstring& error);

// Serialises a node tree using canonical 2-space indentation.
std::wstring dump(const Node& node);

// ------------------------------------------------------------------ helpers --
// Escapes a scalar for use inside a double-quoted YAML string.
std::wstring quote(const std::wstring& value);
bool needsQuotes(const std::wstring& value);

}  // namespace yaml