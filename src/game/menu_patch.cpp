#include "game/menu_patch.h"

#include "game/bxml.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace mc::game::menu_patch {
namespace {

using bxml::Node;

constexpr char kCoopLabel[] = "CO-OP";
constexpr char kVisibleAlways[] = "\\u100";  // the visibility condition of the main menu's EXIT entry (shown on PC)

// The game checks every XML resource against a stored value that depends on the resource's byte size (found in the
// first test launch: a same-size edit loads; one byte more anywhere in a resource - or a changed prefix word - and
// the game drops the whole archive: black screen, no menus). So no resource may change its size and none is added:
// the CO-OP entries reuse the menus' hidden debug entries (which already open screens of their own), the CO-OP pages
// are built into those debug screens, and every patched resource is padded back to its original size.

// Main menu: the hidden debug entry "DEBUG Load Chapter" (btnLoadMission, right after STORY, hidden by visibility
// condition 145) becomes CO-OP; its screen LoadMission (only reachable from that entry) becomes the CO-OP page,
// built from the EXTRAS page.
constexpr char kMainScreen[] = "/config/gui/Screens/MainMenu/MainMenu";
constexpr char kMainLayout[] = "/config/gui/ScreenLayouts/MainMenu";
constexpr char kMainList[] = "listboxMainMenu";
constexpr char kMainEntry[] = "btnLoadMission";
constexpr char kMainTplScreen[] = "/config/gui/Screens/MainMenu/Extras";
constexpr char kMainTplLayout[] = "/config/gui/ScreenLayouts/Extras";
constexpr char kMainHostScreen[] = "/config/gui/Screens/MainMenu/LoadMission";
constexpr char kMainHostLayout[] = "/config/gui/ScreenLayouts/LoadMission";
constexpr char kMainHostLayoutName[] = "LoadMission";

// Pause menu: the hidden cheats entry (btnCheats, right after OPTIONS, hidden by condition 130) becomes CO-OP; its
// screen Cheats becomes the CO-OP page, built from the pause menu's OPTIONS page.
constexpr char kPauseScreen[] = "/config/gui/Screens/GameMenu/GameMenu";
constexpr char kPauseLayout[] = "/config/gui/ScreenLayouts/GameMenu";
constexpr char kPauseList[] = "listboxGameMenu";
constexpr char kPauseEntry[] = "btnCheats";
constexpr char kPauseTplScreen[] = "/config/gui/Screens/GameMenu/Options";
constexpr char kPauseTplLayout[] = "/config/gui/ScreenLayouts/OptionsGameMenu";  // in gui-main.sds
constexpr char kPauseHostScreen[] = "/config/gui/Screens/GameMenu/Cheats";
constexpr char kPauseHostLayout[] = "/config/gui/ScreenLayouts/Cheats";
constexpr char kPauseHostLayoutName[] = "Cheats";

struct Ctx {
    Result& res;
    void Log(const std::string& s) { res.log.push_back(s); }
    bool Fail(const std::string& s) {
        res.log.push_back("FAILED: " + s);
        return false;
    }
};

std::string Fmt4(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", v);
    return b;
}

double AttrNum(const Node* n, const char* key, double def = 0) {
    const std::string* v = n ? n->Attr(key) : nullptr;
    return v ? std::atof(v->c_str()) : def;
}

Node* Widget(Node* root, const std::string& name) { return root->FindByAttr("widget", "name", name); }

Node* FindIf(Node* n, const std::function<bool(Node*)>& pred) {
    if (pred(n)) return n;
    for (auto& c : n->children)
        if (Node* f = FindIf(c.get(), pred)) return f;
    return nullptr;
}

size_t IndexOf(const Node* parent, const Node* child) {
    for (size_t i = 0; i < parent->children.size(); ++i)
        if (parent->children[i].get() == child) return i;
    return parent->children.size();
}

void Insert(Node* parent, size_t at, std::unique_ptr<Node> n) {
    if (at > parent->children.size()) at = parent->children.size();
    parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(at), std::move(n));
}

void RemoveChildren(Node* parent, const std::string& name) {
    auto& c = parent->children;
    for (size_t i = 0; i < c.size();)
        if (c[i]->name == name) c.erase(c.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
}

// A list button for one of our entries, cloned from one of the game's own buttons: same skin, font, size; our
// label as literal text (textid 0, as the game's own literal labels do), no tooltip, visible, not frozen.
std::unique_ptr<Node> MakeButton(const Node& tpl, const std::string& name, const std::string& label, double y) {
    auto b = tpl.Clone();
    b->SetAttr("name", name);
    if (Node* props = b->Child("properties")) {
        RemoveChildren(props, "freeze");
        if (Node* cd = props->Child("commondata")) {
            cd->SetAttr("disabled", "false");
            cd->SetAttr("visible", "true");
        }
        if (Node* t = props->Child("text")) {
            t->SetAttr("text", label);
            t->SetAttr("textid", "0");
        }
        if (Node* t = props->Child("tooltip")) t->SetAttr("textId", "0");
        if (Node* p = props->Child("position")) p->SetAttr("y", Fmt4(y));
    }
    return b;
}

double RowHeight(Node* widget) {
    Node* props = widget->Child("properties");
    const double h = AttrNum(props ? props->Child("size") : nullptr, "y", 0.062);
    return h > 0.01 ? h : 0.062;
}

// The screen-side widget entry of a list button: hovering selects it (as the game's own entries do).
std::unique_ptr<Node> MakeScreenWidget(const std::string& name, const std::string& list) {
    auto w = std::make_unique<Node>();
    w->name = "widget";
    w->SetAttr("name", name);
    Node& co = w->Add("events").Add("cursor-over");
    co.SetAttr("action", "container.selectItem");
    co.SetAttr("widget", list);
    co.hasText = true;
    co.text = name;
    return w;
}

struct Res {
    sds::Resource* res = nullptr;
    bxml::Document doc;
};

bool Load(Ctx& c, sds::Archive& a, const char* name, Res& out) {
    out.res = a.Find(name);
    if (!out.res) return c.Fail(std::string("resource ") + name + " not found");
    std::string err;
    if (!bxml::Read(out.res->data, out.doc, &err)) return c.Fail(std::string(name) + ": " + err);
    return true;
}

void Store(Ctx& c, Res& r) {
    r.res->data = bxml::Write(r.doc);
    c.res.dumps.emplace_back(r.res->sourceName, bxml::ToText(*r.doc.root));
}

bool ReadDoc(Ctx& c, const sds::Archive& a, const char* name, bxml::Document& out) {
    const sds::Resource* r = nullptr;
    for (const auto& x : a.resources)
        if (x.sourceName == name) r = &x;
    if (!r) return c.Fail(std::string("resource ") + name + " not found");
    std::string err;
    if (!bxml::Read(r->data, out, &err)) return c.Fail(std::string(name) + ": " + err);
    return true;
}

// ---- keeping every resource's size ----

size_t Size(const Res& r) { return bxml::Write(r.doc).size(); }

// Layout padding. Two things the game reads without changing what it shows:
//  - plain decimal numbers ("0.0000", "-1.0000") written with more or fewer trailing zeros - the same value;
//  - invisible empty group boxes (the game's own layouts have hidden group boxes, e.g. gbPromoHighlight).
// (Label literals are NOT slack: a non-empty text="..." is drawn instead of the textid string - seen in game.)
bool IsDecimal(const std::string& v) {
    size_t i = v.size() && v[0] == '-' ? 1 : 0;
    const size_t dot = v.find('.');
    if (dot == std::string::npos || dot == i || dot + 1 >= v.size()) return false;
    for (size_t k = i; k < v.size(); ++k)
        if (k != dot && (v[k] < '0' || v[k] > '9')) return false;
    return true;
}

struct NumAttr {
    Node* node;
    size_t attr;   // index into node->attrs
    size_t base;   // the value's length as the game wrote it
};

void CollectNumbers(Node* n, std::vector<NumAttr>& out) {
    for (size_t i = 0; i < n->attrs.size(); ++i)
        if (IsDecimal(n->attrs[i].second)) out.push_back({n, i, n->attrs[i].second.size()});
    for (auto& c : n->children) CollectNumbers(c.get(), out);
}

constexpr size_t kMaxExtraZeros = 6;  // "0.0000" -> at most "0.0000000000": short enough for any number parser

std::unique_ptr<Node> MakeFiller(size_t i) {
    auto w = std::make_unique<Node>();
    w->name = "widget";
    w->SetAttr("name", "coopPad" + std::to_string(i));
    w->SetAttr("type", "groupbox");
    Node& props = w->Add("properties");
    Node& cd = props.Add("commondata");
    cd.SetAttr("disabled", "true");
    cd.SetAttr("visible", "false");
    cd.SetAttr("tabIndex", "1");
    props.Add("name");
    Node& pos = props.Add("position");
    for (const char* k : {"x", "y", "xOrigin", "yOrigin", "rotation"}) pos.SetAttr(k, "0.0000");
    Node& size = props.Add("size");
    size.SetAttr("x", "0.0000");
    size.SetAttr("y", "0.0000");
    return w;
}

// Brings a layout back to exactly `target` bytes: trailing zeros of its numbers off (too big) or on (too small),
// invisible filler group boxes for large gaps.
bool FitLayout(Ctx& c, Res& r, size_t target) {
    Node* top = r.doc.root->Child("widget");
    if (!top) return c.Fail(r.res->sourceName + ": no root widget");
    // too big: drop trailing zeros ("0.0000" -> "0.0")
    std::vector<NumAttr> nums;
    CollectNumbers(r.doc.root.get(), nums);
    for (auto& na : nums) {
        if (Size(r) <= target) break;
        std::string& v = na.node->attrs[na.attr].second;
        while (v.size() > v.find('.') + 2 && v.back() == '0') v.pop_back();
    }
    // much too small: invisible fillers
    size_t fillers = 0;
    Node* kids = top->Child("children");
    if (!kids) kids = &top->Add("children");
    nums.clear();
    CollectNumbers(r.doc.root.get(), nums);
    while (Size(r) < target && target - Size(r) > nums.size() * kMaxExtraZeros / 2) {
        kids->children.push_back(MakeFiller(fillers++));
        if (Size(r) > target) {  // one filler too many: take it back
            kids->children.pop_back();
            --fillers;
            break;
        }
    }
    nums.clear();
    CollectNumbers(r.doc.root.get(), nums);
    // fine: one trailing zero at a time (each is one byte; layouts have no aligned element texts)
    for (size_t round = 0; round < kMaxExtraZeros + 4 && Size(r) < target; ++round)
        for (auto& na : nums) {
            const size_t now = Size(r);
            if (now >= target) break;
            std::string& v = na.node->attrs[na.attr].second;
            if (v.size() < na.base + kMaxExtraZeros) v.push_back('0');
        }
    if (Size(r) == target) {
        if (fillers) c.Log(r.res->sourceName + ": " + std::to_string(fillers) + " invisible filler(s) to keep its size");
        return true;
    }
    return c.Fail(r.res->sourceName + ": cannot keep its size (" + std::to_string(Size(r)) + " vs " +
                  std::to_string(target) + " bytes)");
}

// Screen padding: no-op events at the very end of the tree (the last widget's events) - removing a menu variable
// that does not exist, as the game's own screens remove theirs. Nothing follows them in the string table, so the
// last one's name can take up the remaining bytes exactly. Names stay short (several events for a large gap).
bool FitScreen(Ctx& c, Res& r, size_t target) {
    if (Size(r) == target) return true;
    Node* config = r.doc.root->Child("config");
    Node* widgets = config ? config->Child("widgets") : nullptr;
    Node* last = widgets && !widgets->children.empty() ? widgets->children.back().get() : nullptr;
    if (!last) return c.Fail(r.res->sourceName + ": no widget to pad");
    Node* ev = last->Child("events");
    if (!ev) ev = &last->Add("events");
    constexpr size_t kMaxName = 24;
    for (int i = 0; i < 4096 && Size(r) < target; ++i) {
        Node& pad = ev->Add("hide");
        pad.SetAttr("action", "menu.removeVariable");
        const std::string base = "CoopPad" + std::to_string(i);
        pad.SetAttr("name", base);
        const size_t now = Size(r);
        if (now > target) {
            ev->children.pop_back();
            break;
        }
        if (target - now <= kMaxName - base.size()) {
            std::string v = base;
            v.resize(base.size() + (target - now), 'x');
            pad.SetAttr("name", v);
            break;
        }
    }
    // what is left (less than one more event): a few more characters on each event's name
    for (int round = 0; round < 64 && Size(r) < target; ++round)
        for (auto& e : ev->children) {
            const std::string* nm = e->Attr("name");
            if (!nm || nm->rfind("CoopPad", 0) != 0 || Size(r) >= target) continue;
            if (nm->size() < 160) e->SetAttr("name", *nm + "x");
        }
    if (Size(r) == target) return true;
    return c.Fail(r.res->sourceName + ": cannot keep its size (" + std::to_string(Size(r)) + " vs " +
                  std::to_string(target) + " bytes)");
}

// ---- the entries and pages ----

// Turns a hidden debug entry of a menu into CO-OP: the layout's button gets our literal label (the game's own
// literal labels use textid 0) and the tooltip of the entry above it; in the screen its visibility condition becomes
// the one the game uses for entries that are always shown. Its `use` (open its own screen) stays.
bool RepurposeEntry(Ctx& c, Res& screen, Res& layout, const char* list, const char* entry) {
    const size_t screenSize = screen.res->data.size(), layoutSize = layout.res->data.size();
    Node* lb = Widget(layout.doc.root.get(), list);
    Node* items = lb ? lb->Child("items") : nullptr;
    Node* btn = items ? Widget(items, entry) : nullptr;
    Node* props = btn ? btn->Child("properties") : nullptr;
    Node* text = props ? props->Child("text") : nullptr;
    if (!text) return c.Fail(std::string("layout: ") + list + " / " + entry + " not found");
    text->SetAttr("text", kCoopLabel);
    text->SetAttr("textid", "0");
    const size_t at = IndexOf(items, btn);
    if (at > 0 && at < items->children.size())
        if (Node* above = items->children[at - 1]->Child("properties"))
            if (Node* tip = above->Child("tooltip"))
                if (Node* own = props->Child("tooltip")) own->attrs = tip->attrs;
    if (Node* cd = props->Child("commondata")) {
        cd->SetAttr("disabled", "false");
        cd->SetAttr("visible", "true");
    }

    Node* config = screen.doc.root->Child("config");
    Node* widgets = config ? config->Child("widgets") : nullptr;
    Node* sw = widgets ? Widget(widgets, entry) : nullptr;
    Node* ev = sw ? sw->Child("events") : nullptr;
    if (!ev) return c.Fail(std::string("screen: widget ") + entry + " not found");
    int vis = 0;
    for (auto& e : ev->children) {
        const std::string* action = e->Attr("action");
        if (action && *action == "menu.visibility" && e->hasText) {
            e->text = kVisibleAlways;
            ++vis;
        }
    }
    if (!vis) return c.Fail(std::string("screen: ") + entry + " has no visibility condition");
    if (!FitLayout(c, layout, layoutSize) || !FitScreen(c, screen, screenSize)) return false;
    c.Log(std::string("entry ") + kCoopLabel + ": " + entry + " in " + list + " (" + std::to_string(vis) +
          " visibility condition(s) -> always shown)");
    return true;
}

// Builds the CO-OP page into a host screen/layout pair from a template page (a screen + its layout): same look; the
// title says CO-OP; the list holds our entries; Esc / B / the footer's BACK go back to the menu. Both host resources
// keep their size; the host layout keeps its root widget name.
bool BuildPage(Ctx& c, const bxml::Document& tplScreen, const bxml::Document& tplLayout, Res& hostScreen,
               Res& hostLayout, const char* hostLayoutName) {
    const size_t screenSize = hostScreen.res->data.size(), layoutSize = hostLayout.res->data.size();
    // ---- layout ----
    auto layout = tplLayout.root->Clone();
    Node* top = layout->Child("widget");
    if (!top) return c.Fail("template layout has no root widget");
    if (Node* hostTop = hostLayout.doc.root->Child("widget"))
        if (const std::string* nm = hostTop->Attr("name")) top->SetAttr("name", *nm);
    Node* title = FindIf(layout.get(), [](Node* n) {
        const std::string* nm = n->Attr("name");
        const std::string* ty = n->Attr("type");
        return n->name == "widget" && nm && ty && *ty == "statictext" && nm->rfind("stTitle", 0) == 0;
    });
    if (!title) return c.Fail("template layout has no stTitle* text");
    if (Node* props = title->Child("properties"))
        if (Node* t = props->Child("text")) {
            t->SetAttr("text", kCoopLabel);
            t->SetAttr("textid", "0");
        }
    Node* lb = FindIf(layout.get(), [](Node* n) {
        const std::string* ty = n->Attr("type");
        return n->name == "widget" && ty && *ty == "listbox";
    });
    Node* items = lb ? lb->Child("items") : nullptr;
    if (!items || items->children.empty()) return c.Fail("template layout has no list");
    const std::string list = *lb->Attr("name");
    std::vector<std::string> tplItems;
    for (const auto& it : items->children)
        if (const std::string* nm = it->Attr("name")) tplItems.push_back(*nm);
    const auto tplButton = items->children.front()->Clone();
    const double row = RowHeight(tplButton.get());
    items->children.clear();
    const auto& entries = CoopEntries();
    std::vector<std::string> names;
    for (size_t i = 0; i < entries.size(); ++i) {
        names.push_back("btnCoop" + std::to_string(i));
        items->children.push_back(MakeButton(*tplButton, names.back(), entries[i], row * static_cast<double>(i)));
    }

    // ---- screen ----
    auto screen = tplScreen.root->Clone();
    Node* config = screen->Child("config");
    Node* settings = config ? config->Child("settings") : nullptr;
    Node* widgets = config ? config->Child("widgets") : nullptr;
    Node* events = config ? config->Child("events") : nullptr;
    if (!settings || !widgets || !events) return c.Fail("template screen is not a config/settings/events/widgets tree");
    if (Node* d = settings->Child("definition")) d->text = std::string("\\s") + hostLayoutName;
    else return c.Fail("template screen has no definition");
    if (Node* f = settings->Child("focus")) f->text = names.front();
    events->children.clear();
    Node& cancel = events->Add("cancel");
    cancel.SetAttr("action", "screen.back");
    auto& w = widgets->children;
    for (size_t i = 0; i < w.size();) {
        const std::string* nm = w[i]->Attr("name");
        bool drop = false;
        for (const auto& t : tplItems) drop = drop || (nm && *nm == t);
        if (drop) w.erase(w.begin() + static_cast<std::ptrdiff_t>(i));
        else ++i;
    }
    size_t at = 0;
    for (const auto& nm : names) Insert(widgets, at++, MakeScreenWidget(nm, list));
    if (Node* back = Widget(widgets, "btnBack")) {
        Node* ev = back->Child("events");
        if (!ev) ev = &back->Add("events");
        if (Node* use = ev->Child("use")) {
            use->attrs.clear();
            use->SetAttr("action", "screen.back");
            use->hasText = false;
            use->children.clear();
        }
    }

    hostLayout.doc.root = std::move(layout);  // the host resources keep their own prefix (name, check value)
    hostScreen.doc.root = std::move(screen);
    // The host screen may be smaller than the template's (main menu: LoadMission 3142 bytes, EXTRAS 5937): drop
    // what the page can do without until it fits - remembering the focus, then the blend type of the fades.
    const size_t padCost = 160;  // about what the pad event FitScreen adds costs
    Node* cfg = hostScreen.doc.root->Child("config");
    Node* st = cfg ? cfg->Child("settings") : nullptr;
    if (st && Size(hostScreen) + padCost > screenSize) {
        RemoveChildren(st, "remember-focus");
        c.Log(hostScreen.res->sourceName + ": no remember-focus (size)");
    }
    if (st && Size(hostScreen) + padCost > screenSize) {
        for (const char* f : {"fade-in", "fade-out"})
            if (Node* n = st->Child(f)) n->attrs.clear();
        c.Log(hostScreen.res->sourceName + ": plain fades (size)");
    }
    if (!FitLayout(c, hostLayout, layoutSize) || !FitScreen(c, hostScreen, screenSize)) return false;
    c.Log(std::string("page built in ") + hostScreen.res->sourceName + " / " + hostLayout.res->sourceName + " (" +
          std::to_string(entries.size()) + " entries, list " + list + ")");
    return true;
}

}  // namespace

const std::vector<std::string>& CoopEntries() {
    static const std::vector<std::string> k = {"HOST CO-OP GAME", "JOIN FRIEND", "JOIN BY IP", "LOBBY", "SETTINGS"};
    return k;
}

Result PatchGuiMain(sds::Archive& a) {
    Result res;
    Ctx c{res};
    Res screen, layout, hostScreen, hostLayout;
    bxml::Document tplScreen, tplLayout;
    if (!Load(c, a, kMainScreen, screen) || !Load(c, a, kMainLayout, layout) ||
        !Load(c, a, kMainHostScreen, hostScreen) || !Load(c, a, kMainHostLayout, hostLayout) ||
        !ReadDoc(c, a, kMainTplScreen, tplScreen) || !ReadDoc(c, a, kMainTplLayout, tplLayout))
        return res;
    if (!RepurposeEntry(c, screen, layout, kMainList, kMainEntry)) return res;
    if (!BuildPage(c, tplScreen, tplLayout, hostScreen, hostLayout, kMainHostLayoutName)) return res;
    for (Res* r : {&screen, &layout, &hostScreen, &hostLayout}) Store(c, *r);
    res.ok = true;
    return res;
}

Result PatchGui(sds::Archive& a, const sds::Archive& guiMain) {
    Result res;
    Ctx c{res};
    Res screen, layout, hostScreen, hostLayout;
    bxml::Document tplScreen, tplLayout;
    if (!Load(c, a, kPauseScreen, screen) || !Load(c, a, kPauseLayout, layout) ||
        !Load(c, a, kPauseHostScreen, hostScreen) || !Load(c, a, kPauseHostLayout, hostLayout) ||
        !ReadDoc(c, a, kPauseTplScreen, tplScreen) || !ReadDoc(c, guiMain, kPauseTplLayout, tplLayout))
        return res;
    if (!RepurposeEntry(c, screen, layout, kPauseList, kPauseEntry)) return res;
    if (!BuildPage(c, tplScreen, tplLayout, hostScreen, hostLayout, kPauseHostLayoutName)) return res;
    for (Res* r : {&screen, &layout, &hostScreen, &hostLayout}) Store(c, *r);
    res.ok = true;
    return res;
}

}  // namespace mc::game::menu_patch
