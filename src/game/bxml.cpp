#include "game/bxml.h"

#include <cstring>
#include <map>

namespace mc::game::bxml {
namespace {

bool Fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

void Put32(std::vector<uint8_t>& b, uint32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}

struct Reader {
    const std::vector<uint8_t>& b;
    size_t pos;
    bool ok = true;
    uint32_t U32() {
        if (pos + 4 > b.size()) {
            ok = false;
            return 0;
        }
        uint32_t v;
        std::memcpy(&v, b.data() + pos, 4);
        pos += 4;
        return v;
    }
};

struct RawNode {
    uint32_t name = 0, text = 0;
    std::vector<uint32_t> children;
    std::vector<std::pair<uint32_t, uint32_t>> attrs;
};

class Writer {
public:
    std::vector<uint8_t> strings;
    std::vector<uint8_t> nodes;
    uint32_t count = 0;

    Writer() { Add("", 4); }

    uint32_t Add(const std::string& s, uint32_t type) {
        if (type == 1)
            while (strings.size() % 4) strings.push_back(0);
        const auto off = static_cast<uint32_t>(strings.size());
        Put32(strings, type);
        Put32(strings, 0);
        strings.insert(strings.end(), s.begin(), s.end());
        strings.push_back(0);
        return off;
    }

    // Ids in depth-first pre-order, strings in the game's order: name, attributes, text.
    struct Pending {
        uint32_t name, text, id;
        std::vector<std::pair<uint32_t, uint32_t>> attrs;
        std::vector<uint32_t> children;
    };
    std::vector<Pending> all;

    uint32_t Walk(const Node& n) {
        const uint32_t id = static_cast<uint32_t>(all.size());
        all.push_back({});
        Pending p;
        p.id = id;
        p.name = n.name.empty() ? 0 : Add(n.name, 4);
        for (const auto& [k, v] : n.attrs) p.attrs.emplace_back(Add(k, 4), Add(v, 4));
        p.text = n.hasText ? Add(n.text, 1) : 0;
        for (const auto& c : n.children) p.children.push_back(Walk(*c));
        all[id] = std::move(p);
        return id;
    }
};

}  // namespace

const std::string* Node::Attr(const std::string& key) const {
    for (const auto& [k, v] : attrs)
        if (k == key) return &v;
    return nullptr;
}

void Node::SetAttr(const std::string& key, const std::string& value) {
    for (auto& [k, v] : attrs)
        if (k == key) {
            v = value;
            return;
        }
    attrs.emplace_back(key, value);
}

Node* Node::Child(const std::string& childName) {
    for (auto& c : children)
        if (c->name == childName) return c.get();
    return nullptr;
}

Node* Node::FindByAttr(const std::string& elem, const std::string& key, const std::string& value) {
    if (name == elem) {
        const std::string* v = Attr(key);
        if (v && *v == value) return this;
    }
    for (auto& c : children)
        if (Node* f = c->FindByAttr(elem, key, value)) return f;
    return nullptr;
}

Node& Node::Add(const std::string& childName) {
    children.push_back(std::make_unique<Node>());
    children.back()->name = childName;
    return *children.back();
}

std::unique_ptr<Node> Node::Clone() const {
    auto n = std::make_unique<Node>();
    n->name = name;
    n->hasText = hasText;
    n->text = text;
    n->attrs = attrs;
    for (const auto& c : children) n->children.push_back(c->Clone());
    return n;
}

bool Read(const std::vector<uint8_t>& b, Document& doc, std::string* error) {
    Reader r{b, 4};
    const uint32_t targetLen = r.U32();
    if (!r.ok || r.pos + targetLen + 1 > b.size()) return Fail(error, "bad target system");
    r.pos += targetLen + 1;
    const uint32_t nameLen = r.U32();
    if (!r.ok || r.pos + nameLen + 1 > b.size()) return Fail(error, "bad name");
    r.pos += nameLen;
    if (b[r.pos] != 0) return Fail(error, "plain-text XML resource (not binary)");
    doc.prefix.assign(b.begin(), b.begin() + r.pos);
    r.pos += 1;
    if (r.pos >= b.size()) return Fail(error, "truncated");
    doc.version = b[r.pos++];
    const uint32_t nodeCount = r.U32(), strSize = r.U32();
    if (!r.ok || r.pos + strSize > b.size() || nodeCount == 0) return Fail(error, "bad counts");
    const size_t S = r.pos;
    r.pos += strSize;
    auto str = [&](uint32_t off, std::string& out) {
        if (off + 8 >= strSize) return false;
        const auto* start = b.data() + S + off + 8;
        const auto* end = static_cast<const uint8_t*>(std::memchr(start, 0, strSize - off - 8));
        if (!end) return false;
        out.assign(reinterpret_cast<const char*>(start), end - start);
        return true;
    };
    std::vector<RawNode> raw(nodeCount);
    for (uint32_t i = 0; i < nodeCount; ++i) {
        RawNode n;
        n.name = r.U32();
        n.text = r.U32();
        const uint32_t id = r.U32();
        const uint32_t nc = r.U32();
        if (!r.ok || id >= nodeCount || nc > nodeCount) return Fail(error, "bad node");
        for (uint32_t k = 0; k < nc; ++k) n.children.push_back(r.U32());
        const uint32_t na = r.U32();
        if (!r.ok || na > 4096) return Fail(error, "bad attributes");
        for (uint32_t k = 0; k < na; ++k) {
            const uint32_t a = r.U32(), v = r.U32();
            n.attrs.emplace_back(a, v);
        }
        if (!r.ok) return Fail(error, "truncated node table");
        raw[id] = std::move(n);
    }
    if (r.pos != b.size()) return Fail(error, "bytes after the node table");
    // build the tree from node 0 (guarding against cycles)
    std::vector<uint8_t> seen(nodeCount, 0);
    std::string err;
    auto build = [&](auto& self, uint32_t id) -> std::unique_ptr<Node> {
        if (id >= nodeCount || seen[id]) return nullptr;
        seen[id] = 1;
        const RawNode& rn = raw[id];
        auto n = std::make_unique<Node>();
        if (rn.name && !str(rn.name, n->name)) return nullptr;
        if (rn.text) {
            n->hasText = true;
            if (!str(rn.text, n->text)) return nullptr;
        }
        for (const auto& [a, v] : rn.attrs) {
            std::string k, val;
            if (!str(a, k) || !str(v, val)) return nullptr;
            n->attrs.emplace_back(std::move(k), std::move(val));
        }
        for (uint32_t c : rn.children) {
            auto child = self(self, c);
            if (!child) return nullptr;
            n->children.push_back(std::move(child));
        }
        return n;
    };
    doc.root = build(build, 0);
    if (!doc.root) return Fail(error, "bad node tree");
    return true;
}

std::vector<uint8_t> Write(const Document& doc) {
    Writer w;
    w.Walk(*doc.root);
    std::vector<uint8_t> nodes;
    for (const auto& p : w.all) {
        Put32(nodes, p.name);
        Put32(nodes, p.text);
        Put32(nodes, p.id);
        Put32(nodes, static_cast<uint32_t>(p.children.size()));
        for (uint32_t c : p.children) Put32(nodes, c);
        Put32(nodes, static_cast<uint32_t>(p.attrs.size()));
        for (const auto& [a, v] : p.attrs) {
            Put32(nodes, a);
            Put32(nodes, v);
        }
    }
    std::vector<uint8_t> out = doc.prefix;
    out.push_back(0);
    out.push_back(doc.version);
    Put32(out, static_cast<uint32_t>(w.all.size()));
    Put32(out, static_cast<uint32_t>(w.strings.size()));
    out.insert(out.end(), w.strings.begin(), w.strings.end());
    out.insert(out.end(), nodes.begin(), nodes.end());
    return out;
}

std::string ToText(const Node& n, int indent) {
    std::string pad(static_cast<size_t>(indent) * 2, ' ');
    if (n.name.empty()) {
        std::string s;
        for (const auto& c : n.children) s += ToText(*c, indent);
        return s;
    }
    std::string s = pad + "<" + n.name;
    for (const auto& [k, v] : n.attrs) s += " " + k + "=\"" + v + "\"";
    if (n.children.empty()) {
        if (n.hasText) return s + ">" + n.text + "</" + n.name + ">\n";
        return s + "/>\n";
    }
    s += ">" + (n.hasText ? n.text : std::string()) + "\n";
    for (const auto& c : n.children) s += ToText(*c, indent + 1);
    return s + pad + "</" + n.name + ">\n";
}

}  // namespace mc::game::bxml
