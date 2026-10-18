#pragma once
// The game's binary XML resources (SDS type "XML", e.g. /config/gui/Screens/MainMenu/MainMenu), read into a tree and
// written back. Layout found from the game's files (docs/re-notes.md 3); writing an unchanged tree gives the same
// bytes except the alignment padding (the game leaves junk there; we write zeros).
//   prefix   u32 (unknown, kept), u32 length + target system ("UnknownTargetSystem"), u8 1, u32 length + name,
//            u8 0 (binary; 1 = plain text follows)
//   body     u8 version 4, u32 node count, u32 string table size, string table, node table
//   strings  entries { u32 type, u32 0, NUL-terminated text }; type 4 = element / attribute name or attribute
//            value, type 1 = an element's text (4-byte aligned in the table). Offset 0 = an empty type-4 entry.
//   nodes    { u32 name offset, u32 text offset (0 = none), u32 id, u32 child count, child ids..., u32 attribute
//            count, { u32 name offset, u32 value offset }... } in id order; node 0 is an unnamed root.
// Texts are kept as the game's bytes (Windows-1252), typed values keep their prefix ("\s" string, "\u" unsigned,
// "\f" float).
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mc::game::bxml {

struct Node {
    std::string name;
    bool hasText = false;
    std::string text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<Node>> children;

    const std::string* Attr(const std::string& key) const;
    void SetAttr(const std::string& key, const std::string& value);
    Node* Child(const std::string& childName);  // first child with this name
    // first descendant (depth first) element `elem` whose attribute `key` is `value`
    Node* FindByAttr(const std::string& elem, const std::string& key, const std::string& value);
    Node& Add(const std::string& childName);  // appends a child
    std::unique_ptr<Node> Clone() const;
};

struct Document {
    std::vector<uint8_t> prefix;  // everything before the body (kept as read)
    uint8_t version = 4;
    std::unique_ptr<Node> root;   // the unnamed node 0
};

bool Read(const std::vector<uint8_t>& data, Document& out, std::string* error);
std::vector<uint8_t> Write(const Document& doc);

// Readable XML text of a tree (logs and tests).
std::string ToText(const Node& n, int indent = 0);

}  // namespace mc::game::bxml
