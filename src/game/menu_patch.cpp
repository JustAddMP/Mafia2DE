#include "game/menu_patch.h"

#include "game/bxml.h"

#include <cstdio>
#include <cstdlib>
#include <functional>

namespace mc::game::menu_patch {
namespace {

using bxml::Node;

constexpr char kCoopScreen[] = "Coop";  // the screen name the daemons get
constexpr char kCoopLabel[] = "CO-OP";

// Main menu: entry after STORY, page = EXTRAS look, hosted in the unused MafiaLogo screen/layout pair.
constexpr char kMainScreen[] = "/config/gui/Screens/MainMenu/MainMenu";
constexpr char kMainLayout[] = "/config/gui/ScreenLayouts/MainMenu";
constexpr char kMainDaemon[] = "/config/gui/Daemons/MainMenu";
constexpr char kMainList[] = "listboxMainMenu";
constexpr char kMainAfter[] = "btnStory";
constexpr char kMainTplScreen[] = "/config/gui/Screens/MainMenu/Extras";
constexpr char kMainTplLayout[] = "/config/gui/ScreenLayouts/Extras";
constexpr char kMainHostScreen[] = "/config/gui/Screens/MainMenu/MafiaLogo";
constexpr char kMainHostLayout[] = "/config/gui/ScreenLayouts/MafiaLogo";
constexpr char kMainHostSource[] = "MainMenu/MafiaLogo";
constexpr char kMainHostLayoutName[] = "MafiaLogo";

// Pause menu: entry after OPTIONS, page = the pause menu's OPTIONS look, hosted in the debug Cheats pair.
constexpr char kPauseScreen[] = "/config/gui/Screens/GameMenu/GameMenu";
constexpr char kPauseLayout[] = "/config/gui/ScreenLayouts/GameMenu";
constexpr char kPauseDaemon[] = "/config/gui/Daemons/GameMenu";
constexpr char kPauseList[] = "listboxGameMenu";
constexpr char kPauseAfter[] = "btnOptions";
constexpr char kPauseTplScreen[] = "/config/gui/Screens/GameMenu/Options";
constexpr char kPauseTplLayout[] = "/config/gui/ScreenLayouts/OptionsGameMenu";  // in gui-main.sds
constexpr char kPauseHostScreen[] = "/config/gui/Screens/GameMenu/Cheats";
constexpr char kPauseHostLayout[] = "/config/gui/ScreenLayouts/Cheats";
constexpr char kPauseHostSource[] = "GameMenu/Cheats";
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

double PosY(Node* widget) {
    Node* props = widget->Child("properties");
    return AttrNum(props ? props->Child("position") : nullptr, "y");
}
void SetPosY(Node* widget, double y) {
    if (Node* props = widget->Child("properties"))
        if (Node* p = props->Child("position")) p->SetAttr("y", Fmt4(y));
}
double RowHeight(Node* widget) {
    Node* props = widget->Child("properties");
    const double h = AttrNum(props ? props->Child("size") : nullptr, "y", 0.062);
    return h > 0.01 ? h : 0.062;
}

// The screen-side widget entry of a list button: hovering selects it (as the game's own entries do); `use` opens a
// screen when given.
std::unique_ptr<Node> MakeScreenWidget(const std::string& name, const std::string& list, const std::string& openScreen) {
    auto w = std::make_unique<Node>();
    w->name = "widget";
    w->SetAttr("name", name);
    Node& ev = w->Add("events");
    if (!openScreen.empty()) {
        Node& use = ev.Add("use");
        use.SetAttr("action", "screen.activate");
        use.SetAttr("operation", "replace");
        use.hasText = true;
        use.text = openScreen;
    }
    Node& co = ev.Add("cursor-over");
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

// Adds our entry to a menu: the layout gets a button cloned from `after` (inserted below it, the rows under it move
// down one row), the screen gets its events (open the "Coop" screen).
bool AddEntry(Ctx& c, Res& screen, Res& layout, const char* list, const char* after) {
    Node* lb = Widget(layout.doc.root.get(), list);
    Node* items = lb ? lb->Child("items") : nullptr;
    Node* tpl = items ? Widget(items, after) : nullptr;
    if (!tpl) return c.Fail(std::string("layout: ") + list + " / " + after + " not found");
    if (Widget(items, "btnCoop")) return c.Fail("layout already has btnCoop");
    const double row = RowHeight(tpl), y = PosY(tpl) + row;
    const size_t at = IndexOf(items, tpl) + 1;
    for (size_t i = at; i < items->children.size(); ++i) {
        Node* w = items->children[i].get();
        if (w->name == "widget" && PosY(w) >= y - 0.0001) SetPosY(w, PosY(w) + row);
    }
    auto button = MakeButton(*tpl, "btnCoop", kCoopLabel, y);
    if (Node* props = button->Child("properties"))  // its own tab index (the template's + 1)
        if (Node* cd = props->Child("commondata"))
            cd->SetAttr("tabIndex", std::to_string(static_cast<int>(AttrNum(cd, "tabIndex")) + 1));
    Insert(items, at, std::move(button));

    Node* config = screen.doc.root->Child("config");
    Node* widgets = config ? config->Child("widgets") : nullptr;
    Node* sAfter = widgets ? Widget(widgets, after) : nullptr;
    if (!sAfter) return c.Fail(std::string("screen: widget ") + after + " not found");
    Insert(widgets, IndexOf(widgets, sAfter) + 1, MakeScreenWidget("btnCoop", list, kCoopScreen));
    c.Log(std::string("entry ") + kCoopLabel + " added after " + after + " in " + list + " (row " + Fmt4(y) + ")");
    return true;
}

bool AddDaemonScreen(Ctx& c, Res& daemon, const char* source) {
    Node* config = daemon.doc.root->Child("config");
    Node* screens = config ? config->Child("screens") : nullptr;
    if (!screens) return c.Fail("daemon has no <screens>");
    if (screens->FindByAttr("screen", "name", kCoopScreen)) return c.Fail("daemon already has a Coop screen");
    Node& s = screens->Add("screen");
    s.SetAttr("name", kCoopScreen);
    s.SetAttr("source", source);
    c.Log(std::string("daemon ") + daemon.res->sourceName + ": screen Coop -> " + source);
    return true;
}

// Builds the CO-OP page into the host pair from a template page (a screen + its layout): same look; the title says
// CO-OP; the list holds our entries; Esc / B / the footer's BACK go back to the menu.
bool BuildPage(Ctx& c, const bxml::Document& tplScreen, const bxml::Document& tplLayout, Res& hostScreen,
               Res& hostLayout, const char* hostLayoutName) {
    // ---- layout ----
    auto layout = tplLayout.root->Clone();
    Node* top = layout->Child("widget");
    if (!top) return c.Fail("template layout has no root widget");
    top->SetAttr("name", "layoutCoop");
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
    for (const auto& nm : names) Insert(widgets, at++, MakeScreenWidget(nm, list, ""));
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

    hostLayout.doc.root = std::move(layout);  // the host resources keep their own prefix (name, hash)
    hostScreen.doc.root = std::move(screen);
    c.Log(std::string("page built in ") + hostScreen.res->sourceName + " / " + hostLayout.res->sourceName + " (" +
          std::to_string(entries.size()) + " entries, list " + list + ")");
    return true;
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

}  // namespace

const std::vector<std::string>& CoopEntries() {
    static const std::vector<std::string> k = {"HOST CO-OP GAME", "JOIN FRIEND", "JOIN BY IP", "LOBBY", "SETTINGS"};
    return k;
}

Result PatchGuiMain(sds::Archive& a) {
    Result res;
    Ctx c{res};
    Res screen, layout, daemon, pauseDaemon, hostScreen, hostLayout;
    bxml::Document tplScreen, tplLayout;
    if (!Load(c, a, kMainScreen, screen) || !Load(c, a, kMainLayout, layout) || !Load(c, a, kMainDaemon, daemon) ||
        !Load(c, a, kPauseDaemon, pauseDaemon) || !Load(c, a, kMainHostScreen, hostScreen) ||
        !Load(c, a, kMainHostLayout, hostLayout) || !ReadDoc(c, a, kMainTplScreen, tplScreen) ||
        !ReadDoc(c, a, kMainTplLayout, tplLayout))
        return res;
    if (!AddEntry(c, screen, layout, kMainList, kMainAfter)) return res;
    if (!AddDaemonScreen(c, daemon, kMainHostSource)) return res;
    if (!AddDaemonScreen(c, pauseDaemon, kPauseHostSource)) return res;
    if (!BuildPage(c, tplScreen, tplLayout, hostScreen, hostLayout, kMainHostLayoutName)) return res;
    for (Res* r : {&screen, &layout, &daemon, &pauseDaemon, &hostScreen, &hostLayout}) Store(c, *r);
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
    if (!AddEntry(c, screen, layout, kPauseList, kPauseAfter)) return res;
    if (!BuildPage(c, tplScreen, tplLayout, hostScreen, hostLayout, kPauseHostLayoutName)) return res;
    for (Res* r : {&screen, &layout, &hostScreen, &hostLayout}) Store(c, *r);
    res.ok = true;
    return res;
}

}  // namespace mc::game::menu_patch
