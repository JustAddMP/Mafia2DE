// menu_patch_test: the pieces that put CO-OP into Mafia II DE's own menus, without the game running:
//   - inflate: zlib streams with fixed and dynamic Huffman codes and a stored block, bad checksums refused;
//   - bxml: a made-up binary XML tree written and read back (names, texts, attributes, nesting);
//   - sds: a made-up archive written (raw blocks) and read back;
//   - the real thing, when the game is installed (read-only): gui-main.sds and gui.sds are read, patched and
//     written; the patched archives must read back, have the CO-OP entry in the main menu (the former hidden debug
//     entry after STORY) and the pause menu (the former hidden cheats entry after OPTIONS), the two CO-OP pages with
//     our six entries (HOST CO-OP GAME, FREE RIDE, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS, in that order, each
//     button a copy of the template list's own button) in the debug screens those entries open, the main page's FREE
//     RIDE entry running the game's mission loader for the free ride mission (game.mission.load, mission 0 part 1,
//     FirstCity, gameLine 1 - docs/re-notes.md 5) and no other entry doing anything, the footer kept (BACK, SELECT;
//     the A glyph either with its controller check or hidden for good), every changed resource exactly as big as
//     before with its prefix word unchanged (the game drops the whole archive otherwise - seen in game), and every
//     other resource byte-identical.
//     The patched archives and reports go to out\menu_patch\ for inspection. Game folder: env M2_GAME_DIR (the
//     folder that holds pc\), else the default Steam library paths; skipped (not failed) when not found.
// Exits non-zero on any failure.
#include "game/bxml.h"
#include "game/inflate.h"
#include "game/menu_patch.h"
#include "game/sds.h"
#include "zlib_samples.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace mc::game;

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& name, const std::string& detail = {}) {
    std::printf("%s  %-70s %s\n", ok ? "PASS" : "FAIL", name.c_str(), ok ? "" : detail.c_str());
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

std::vector<uint8_t> ReadAll(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()) : std::vector<uint8_t>{};
}

void WriteAll(const std::wstring& path, const void* p, size_t n) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(p), static_cast<std::streamsize>(n));
}

void TestInflate() {
    std::vector<uint8_t> out;
    Check(ZlibInflate(kZlibSmall, sizeof kZlibSmall, out) && std::string(out.begin(), out.end()) == "HOST CO-OP GAME",
          "inflate: fixed Huffman codes");
    out.clear();
    size_t used = 0;
    Check(ZlibInflate(kZlibBig, sizeof kZlibBig, out, &used) && out.size() == kBigSize &&
              Adler32(out.data(), out.size()) == kBigAdler && used == sizeof kZlibBig,
          "inflate: dynamic Huffman codes", std::to_string(out.size()));
    // stored block: 78 01, BFINAL=1 BTYPE=00, LEN, NLEN, data, Adler-32
    const char* text = "JOIN BY IP";
    std::vector<uint8_t> z = {0x78, 0x01, 0x01, 10, 0, 0xF5, 0xFF};
    z.insert(z.end(), text, text + 10);
    const uint32_t ad = Adler32(reinterpret_cast<const uint8_t*>(text), 10);
    for (int s = 24; s >= 0; s -= 8) z.push_back(static_cast<uint8_t>(ad >> s));
    out.clear();
    Check(ZlibInflate(z.data(), z.size(), out) && std::string(out.begin(), out.end()) == text, "inflate: stored block");
    z.back() ^= 1;
    out.clear();
    Check(!ZlibInflate(z.data(), z.size(), out), "inflate: a wrong Adler-32 is refused");
    std::vector<uint8_t> bad(kZlibBig, kZlibBig + sizeof kZlibBig);
    bad[40] ^= 0x55;
    out.clear();
    Check(!ZlibInflate(bad.data(), bad.size(), out), "inflate: corrupt data is refused");
}

// A binary XML resource prefix like the game's: u32, target system, 1, name, (then the binary flag 0 is ours).
std::vector<uint8_t> Prefix(const std::string& name) {
    std::vector<uint8_t> p = {0x28, 0x13, 0x83, 0xC5};
    auto put = [&](const std::string& s) {
        const uint32_t n = static_cast<uint32_t>(s.size());
        p.insert(p.end(), reinterpret_cast<const uint8_t*>(&n), reinterpret_cast<const uint8_t*>(&n) + 4);
        p.insert(p.end(), s.begin(), s.end());
    };
    put("UnknownTargetSystem");
    p.push_back(1);
    put(name);
    return p;
}

void TestBxml() {
    bxml::Document d;
    d.prefix = Prefix("/config/gui/Screens/MainMenu/Test");
    d.root = std::make_unique<bxml::Node>();
    auto& config = d.root->Add("config");
    auto& settings = config.Add("settings");
    auto& def = settings.Add("definition");
    def.hasText = true;
    def.text = "\\sTest";
    auto& fade = settings.Add("fade-in");
    fade.SetAttr("type", "blend");
    fade.hasText = true;
    fade.text = "\\f0";
    auto& w = config.Add("widgets").Add("widget");
    w.SetAttr("name", "btnCoop");
    auto& use = w.Add("events").Add("use");
    use.SetAttr("action", "screen.activate");
    use.SetAttr("operation", "replace");
    use.hasText = true;
    use.text = "Coop";
    const std::vector<uint8_t> bytes = bxml::Write(d);
    bxml::Document back;
    std::string err;
    const bool ok = bxml::Read(bytes, back, &err);
    Check(ok, "bxml: a written tree reads back", err);
    if (!ok) return;
    Check(bxml::ToText(*back.root) == bxml::ToText(*d.root), "bxml: same tree after the round trip", bxml::ToText(*back.root));
    Check(back.prefix == d.prefix, "bxml: the resource prefix (hash, target system, name) is kept");
    Check(bxml::Write(back) == bytes, "bxml: writing it again gives the same bytes");
    bxml::Node* found = back.root->FindByAttr("widget", "name", "btnCoop");
    Check(found && found->Child("events") && found->Child("events")->Child("use") &&
              found->Child("events")->Child("use")->text == "Coop",
          "bxml: FindByAttr finds the widget and its use event");
}

void TestSds() {
    sds::Archive a;
    a.header.assign(0x48, 0);
    std::memcpy(a.header.data(), "SDS\0", 4);
    a.header[4] = 19;
    std::memcpy(a.header.data() + 8, "PC\0\0", 4);
    a.header[0x2C] = 1;
    // type table: 2 types (XML id 0, MemFile id 1)
    auto put32 = [](std::vector<uint8_t>& b, uint32_t v) {
        b.insert(b.end(), reinterpret_cast<const uint8_t*>(&v), reinterpret_cast<const uint8_t*>(&v) + 4);
    };
    put32(a.typeTable, 2);
    for (auto [id, name] : {std::pair<uint32_t, std::string>{0, "XML"}, {1, "MemFile"}}) {
        put32(a.typeTable, id);
        put32(a.typeTable, static_cast<uint32_t>(name.size()));
        a.typeTable.insert(a.typeTable.end(), name.begin(), name.end());
        put32(a.typeTable, 0);
    }
    std::string index = "<?xml version=\"1.0\"?>\n<xml>\n";
    for (int i = 0; i < 3; ++i) {
        sds::Resource r;
        r.typeId = i == 1 ? 1 : 0;
        r.typeName = i == 1 ? "MemFile" : "XML";
        r.version = 3;
        r.ram[1] = static_cast<uint32_t>(100 * i);
        r.sourceName = "/test/res" + std::to_string(i);
        r.data.assign(static_cast<size_t>(0x3000 * (i + 1)), static_cast<uint8_t>(0x41 + i));  // crosses blocks
        a.resources.push_back(r);
        index += "\t<ResourceInfo>\n\t\t<TypeName>" + r.typeName + "</TypeName>\n\t\t<SourceDataDescription>" +
                 r.sourceName + "</SourceDataDescription>\n\t</ResourceInfo>\n";
    }
    index += "</xml>\n";
    a.index.assign(index.begin(), index.end());
    const std::vector<uint8_t> file = sds::Write(a);
    sds::Archive back;
    std::string err;
    const bool ok = sds::Read(file, back, &err);
    Check(ok, "sds: a written archive reads back", err);
    if (!ok) return;
    bool same = back.resources.size() == 3;
    for (size_t i = 0; same && i < 3; ++i)
        same = back.resources[i].data == a.resources[i].data && back.resources[i].sourceName == a.resources[i].sourceName &&
               back.resources[i].typeName == a.resources[i].typeName && back.resources[i].ram[1] == a.resources[i].ram[1];
    Check(same, "sds: same resources (data, names, types, memory needs)");
    uint32_t h12, h44, vram;
    std::memcpy(&h12, file.data() + 0x0C, 4);
    std::memcpy(&h44, file.data() + 0x44, 4);
    std::memcpy(&vram, file.data() + 0x20, 4);
    Check(h12 == sds::Fnv1_32(file.data(), 12) && h44 == sds::Fnv1_32(file.data() + 0x10, 0x34),
          "sds: header hashes (FNV-1 of 0..11 and 0x10..0x43)");
    Check(vram == 300, "sds: the header sums the resources' memory needs", std::to_string(vram));
}

std::wstring FindGameDir() {
    wchar_t env[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"M2_GAME_DIR", env, MAX_PATH)) return env;
    for (const wchar_t* p : {L"G:\\SteamLibrary\\steamapps\\common\\Mafia II Definitive Edition",
                             L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mafia II Definitive Edition",
                             L"D:\\SteamLibrary\\steamapps\\common\\Mafia II Definitive Edition"}) {
        if (GetFileAttributesW((std::wstring(p) + L"\\pc\\sds\\gui\\gui-main.sds").c_str()) != INVALID_FILE_ATTRIBUTES)
            return p;
    }
    return {};
}

std::string Text(sds::Archive& a, const char* name) {
    sds::Resource* r = a.Find(name);
    bxml::Document d;
    if (!r || !bxml::Read(r->data, d, nullptr)) return {};
    return bxml::ToText(*d.root);
}

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

int Count(const std::string& hay, const std::string& needle) {
    int n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

const std::vector<std::string> kExpectedEntries = {"HOST CO-OP GAME", "FREE RIDE", "JOIN FRIEND", "JOIN BY IP", "LOBBY", "SETTINGS"};

// Every label once, as a literal, in this order.
bool InOrder(const std::string& page, const std::vector<std::string>& labels) {
    size_t at = 0;
    for (const auto& l : labels) {
        const std::string t = "<text text=\"" + l + "\" textid=\"0\"/>";
        if (Count(page, t) != 1) return false;
        const size_t p = page.find(t);
        if (p < at) return false;
        at = p;
    }
    return true;
}

bxml::Node* FirstListbox(bxml::Node* n) {
    const std::string* ty = n->Attr("type");
    if (n->name == "widget" && ty && *ty == "listbox") return n;
    for (auto& c : n->children)
        if (bxml::Node* f = FirstListbox(c.get())) return f;
    return nullptr;
}

double Num(const std::string* v) { return v ? std::atof(v->c_str()) : -1; }

// Our page's list buttons against the template page's first list button: the same properties (skin, font, colours,
// size, text format, ...) apart from the label, the position (row i at i * row height) and visibility / tooltip.
bool SameButtons(sds::Archive& page, const char* pageLayout, sds::Archive& tplArc, const char* tplLayout, std::string& why) {
    bxml::Document pd, td;
    sds::Resource* pr = page.Find(pageLayout);
    sds::Resource* tr = tplArc.Find(tplLayout);
    if (!pr || !tr || !bxml::Read(pr->data, pd, nullptr) || !bxml::Read(tr->data, td, nullptr)) return (why = "unreadable"), false;
    bxml::Node* pl = FirstListbox(pd.root.get());
    bxml::Node* tl = FirstListbox(td.root.get());
    bxml::Node* pi = pl ? pl->Child("items") : nullptr;
    bxml::Node* ti = tl ? tl->Child("items") : nullptr;
    if (!pi || !ti || ti->children.empty() || pi->children.size() != kExpectedEntries.size()) return (why = "lists"), false;
    bxml::Node* tb = ti->children.front()->Child("properties");
    for (size_t i = 0; i < pi->children.size(); ++i) {
        bxml::Node* b = pi->children[i]->Child("properties");
        if (!b || !tb) return (why = "no properties"), false;
        for (const char* k : {"skin", "color", "textformat", "size", "metrics", "hierarchy", "dock"}) {
            bxml::Node* x = b->Child(k);
            bxml::Node* y = tb->Child(k);
            if ((x == nullptr) != (y == nullptr) || (x && y && bxml::ToText(*x) != bxml::ToText(*y))) {
                // numbers may carry other trailing zeros (size padding): compare their values
                bool same = x && y && x->attrs.size() == y->attrs.size();
                for (size_t a = 0; same && a < x->attrs.size(); ++a)
                    same = x->attrs[a].first == y->attrs[a].first &&
                           (x->attrs[a].second == y->attrs[a].second || Num(&x->attrs[a].second) == Num(&y->attrs[a].second));
                if (!same) return (why = kExpectedEntries[i] + ": " + k + " differs"), false;
            }
        }
        bxml::Node* t = b->Child("text");
        if (!t || !t->Attr("text") || *t->Attr("text") != kExpectedEntries[i]) return (why = "label " + std::to_string(i)), false;
        const double row = Num(tb->Child("size") ? tb->Child("size")->Attr("y") : nullptr);
        const double y = Num(b->Child("position") ? b->Child("position")->Attr("y") : nullptr);
        if (std::abs(y - row * static_cast<double>(i)) > 0.0002) return (why = kExpectedEntries[i] + ": row position"), false;
        bxml::Node* cd = b->Child("commondata");
        if (!cd || !cd->Attr("visible") || *cd->Attr("visible") != "true") return (why = kExpectedEntries[i] + ": hidden"), false;
    }
    return true;
}

void TestGameFiles(const std::wstring& outDir) {
    const std::wstring game = FindGameDir();
    if (game.empty()) {
        std::printf("SKIP  the game's archives (set M2_GAME_DIR to the Mafia II Definitive Edition folder)\n");
        return;
    }
    const std::wstring gui = game + L"\\pc\\sds\\gui\\";
    const auto rawMain = ReadAll(gui + L"gui-main.sds"), rawGui = ReadAll(gui + L"gui.sds");
    sds::Archive main, mainOrig, g, gOrig;
    std::string err;
    Check(sds::Read(rawMain, main, &err) && sds::Read(rawMain, mainOrig, &err), "game: gui-main.sds reads", err);
    Check(sds::Read(rawGui, g, &err) && sds::Read(rawGui, gOrig, &err), "game: gui.sds reads", err);
    if (g_failures) return;
    // unchanged XML resources round-trip through bxml (same tree)
    int xmlOk = 0, xmlAll = 0;
    for (auto* a : {&mainOrig, &gOrig})
        for (auto& r : a->resources) {
            if (r.typeName != "XML") continue;
            bxml::Document d;
            if (!bxml::Read(r.data, d, nullptr)) continue;  // a plain-text one
            ++xmlAll;
            bxml::Document d2;
            if (bxml::Read(bxml::Write(d), d2, nullptr) && bxml::ToText(*d2.root) == bxml::ToText(*d.root)) ++xmlOk;
        }
    Check(xmlAll > 100 && xmlOk == xmlAll, "game: every binary XML resource round-trips (" + std::to_string(xmlOk) + "/" + std::to_string(xmlAll) + ")");

    const auto r1 = menu_patch::PatchGuiMain(main);
    for (const auto& l : r1.log) std::printf("      gui-main: %s\n", l.c_str());
    Check(r1.ok, "game: gui-main.sds patched");
    if (!r1.ok)
        for (const auto& [n, x] : r1.dumps) std::printf("<!-- %s -->\n%s\n", n.c_str(), x.c_str());
    const auto r2 = menu_patch::PatchGui(g, mainOrig);
    for (const auto& l : r2.log) std::printf("      gui: %s\n", l.c_str());
    Check(r2.ok, "game: gui.sds patched");
    if (!r1.ok || !r2.ok) return;

    const auto outMain = sds::Write(main), outGui = sds::Write(g);
    sds::Archive backMain, backGui;
    Check(sds::Read(outMain, backMain, &err), "game: patched gui-main.sds reads back", err);
    Check(sds::Read(outGui, backGui, &err), "game: patched gui.sds reads back", err);
    CreateDirectoryW(outDir.c_str(), nullptr);
    WriteAll(outDir + L"\\gui-main.sds", outMain.data(), outMain.size());
    WriteAll(outDir + L"\\gui.sds", outGui.data(), outGui.size());
    for (auto* rp : {&r1, &r2}) {
        std::string t;
        for (const auto& [n, x] : rp->dumps) t += "<!-- " + n + " -->\n" + x + "\n";
        WriteAll(outDir + (rp == &r1 ? L"\\gui-main.patch.txt" : L"\\gui.patch.txt"), t.data(), t.size());
    }

    // main menu: the hidden debug entry after STORY is now CO-OP, always shown, still opening its own screen
    const std::string mm = Text(backMain, "/config/gui/ScreenLayouts/MainMenu");
    const size_t story = mm.find("name=\"btnStory\""), coop = mm.find("name=\"btnLoadMission\"");
    Check(story < coop && coop != std::string::npos && Has(mm.substr(coop, 600), "<text text=\"CO-OP\" textid=\"0\"/>"),
          "main menu layout: CO-OP (literal label) right after STORY");
    Check(!Has(mm, "DEBUG Load Chapter"), "main menu layout: the debug label is gone");
    const std::string ms = Text(backMain, "/config/gui/Screens/MainMenu/MainMenu");
    Check(Has(ms, "<use action=\"screen.activate\" operation=\"replace\">LoadMission</use>") && !Has(ms, ">\\u145<") &&
              Count(ms, "<activate action=\"menu.visibility\">\\u100</activate>") >= 2,
          "main menu screen: CO-OP always shown, opens the LoadMission screen", ms);
    const std::string page = Text(backMain, "/config/gui/ScreenLayouts/LoadMission");
    Check(menu_patch::CoopEntries() == kExpectedEntries, "CO-OP entries: HOST CO-OP GAME, FREE RIDE, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS");
    Check(InOrder(page, kExpectedEntries) && Has(page, "listboxExtras") && Count(page, "name=\"btnCoop") == 6,
          "main CO-OP page: the six entries, in order, in the EXTRAS list");
    std::string why;
    Check(SameButtons(backMain, "/config/gui/ScreenLayouts/LoadMission", mainOrig, "/config/gui/ScreenLayouts/Extras", why),
          "main CO-OP page: FREE RIDE and the others are EXTRAS buttons (skin, font, colours, size; one row apart)", why);
    Check(Has(page, "<text text=\"CO-OP\" textid=\"0\"/>") && Has(page, "<widget name=\"layoutLoadMission\""),
          "main CO-OP page: title CO-OP, the host's root widget name");
    const std::string pageScreen = Text(backMain, "/config/gui/Screens/MainMenu/LoadMission");
    Check(Has(pageScreen, "<definition>\\sLoadMission</definition>") && Has(pageScreen, "<cancel action=\"screen.back\"/>") &&
              Has(pageScreen, "<focus>btnCoop0") && !Has(pageScreen, "menu.mission"),
          "main CO-OP page screen: its layout, Esc = back, focus on the first entry", pageScreen);
    {   // FREE RIDE starts the free ride mission through the game's own mission loader (docs/re-notes.md 5)
        const std::string load = "<use action=\"game.mission.load\" part=\"\\u1\" game=\"\\sFirstCity\" gameLine=\"\\u1\">\\u0</use>";
        const size_t label = page.find("text=\"FREE RIDE\"");
        const size_t lp = label == std::string::npos ? std::string::npos : page.rfind("<widget name=\"btnCoop", label);
        std::string rideName;
        if (lp != std::string::npos) rideName = page.substr(lp + 14, page.find('"', lp + 14) - lp - 14);
        const size_t sw = rideName.empty() ? std::string::npos : pageScreen.find("<widget name=\"" + rideName + "\">");
        const size_t next = sw == std::string::npos ? sw : pageScreen.find("</widget>", sw);
        Check(!rideName.empty() && sw != std::string::npos && Count(pageScreen, "game.mission.load") == 1 &&
                  pageScreen.find(load, sw) < next && pageScreen.find("<cursor-over", sw) < next,
              "main CO-OP page: FREE RIDE (" + rideName + ") = game.mission.load mission 0 part 1, FirstCity, gameLine 1; hover kept",
              pageScreen);
        Check(Count(pageScreen, "<use action=") == 3,
              "main CO-OP page: no other entry does anything yet (uses: FREE RIDE, BACK, SELECT)", pageScreen);
    }
    {   // the footer: B BACK unchanged; the A glyph hidden for good if its controller check had to go for the size
        const bool glyphCheck = Has(pageScreen, "<widget name=\"imgA\">");
        const size_t ia = page.find("<widget name=\"imgA\"");
        const bool hidden = ia != std::string::npos && Has(page.substr(ia, 300), "visible=\"false\"");
        Check(glyphCheck != hidden && Has(pageScreen, "<widget name=\"btnAccept\">") && Has(pageScreen, "container.notifySelectedItem") &&
                  Has(pageScreen, "<widget name=\"btnBack\">"),
              std::string("main CO-OP page footer: BACK and SELECT kept; A glyph ") + (glyphCheck ? "shown for a pad (controller check kept)" : "hidden for good (controller check dropped for the size)"));
    }

    // pause menu: the hidden cheats entry after OPTIONS is now CO-OP
    const std::string gm = Text(backGui, "/config/gui/ScreenLayouts/GameMenu");
    const size_t opt = gm.find("name=\"btnOptions\""), cheats = gm.find("name=\"btnCheats\"");
    Check(opt < cheats && cheats != std::string::npos && Has(gm.substr(cheats, 600), "<text text=\"CO-OP\" textid=\"0\"/>"),
          "pause menu layout: CO-OP right after OPTIONS");
    const std::string gs = Text(backGui, "/config/gui/Screens/GameMenu/GameMenu");
    Check(Has(gs, ">Cheats</use>") && !Has(gs, ">\\u130<"), "pause menu screen: CO-OP always shown, opens the Cheats screen");
    const std::string pp = Text(backGui, "/config/gui/ScreenLayouts/Cheats");
    Check(InOrder(pp, kExpectedEntries) && Has(pp, "listboxOptions") && Count(pp, "name=\"btnCoop") == 6,
          "pause CO-OP page: the six entries, in order, in the OPTIONS list");
    why.clear();
    Check(SameButtons(backGui, "/config/gui/ScreenLayouts/Cheats", mainOrig, "/config/gui/ScreenLayouts/OptionsGameMenu", why),
          "pause CO-OP page: FREE RIDE and the others are pause OPTIONS buttons", why);
    const std::string ps = Text(backGui, "/config/gui/Screens/GameMenu/Cheats");
    Check(Has(ps, "<definition>\\sCheats</definition>") && !Has(ps, "options.back") && !Has(ps, "game.cheat"),
          "pause CO-OP page screen: its layout, plain back, no cheats", ps);
    Check(!Has(ps, "game.mission.load"), "pause CO-OP page: FREE RIDE does nothing there yet (starting a mission from a running game is not wired)");
    why.clear();
    for (const auto* scr : {&pageScreen, &ps})
        for (int i = 0; i < 6; ++i)
            if (!Has(*scr, "<widget name=\"btnCoop" + std::to_string(i)) ||
                !Has(*scr, "<cursor-over action=\"container.selectItem\""))
                why += "screen widget btnCoop" + std::to_string(i) + " missing ";
    Check(why.empty() && Count(pageScreen, "\">btnCoop") == 6 && Count(ps, "\">btnCoop") == 6,
          "both CO-OP page screens: every entry (FREE RIDE too) selected by mouse hover", why);
    // the screens name exactly the layouts' buttons (the names may carry filler characters to keep the size)
    why.clear();
    for (const auto& [lay, scr] : {std::pair<const std::string*, const std::string*>{&page, &pageScreen}, {&pp, &ps}})
        for (size_t p = lay->find("<widget name=\"btnCoop"); p != std::string::npos; p = lay->find("<widget name=\"btnCoop", p + 1)) {
            const size_t q = p + 14, e = lay->find('"', q);
            const std::string nm = lay->substr(q, e - q);
            if (!Has(*scr, "<widget name=\"" + nm + "\">") || !Has(*scr, "\">" + nm + "</cursor-over>")) why += nm + " ";
        }
    Check(why.empty(), "both CO-OP pages: screen and layout name the same entry widgets", why);

    // the game's check: a changed resource keeps its exact size and its prefix word
    auto sameShape = [](const sds::Archive& orig, const sds::Archive& back, const std::vector<std::string>& changed,
                        std::string& bad) {
        for (size_t i = 0; i < orig.resources.size() && i < back.resources.size(); ++i)
            for (const auto& c : changed)
                if (orig.resources[i].sourceName == c &&
                    (orig.resources[i].data.size() != back.resources[i].data.size() ||
                     std::memcmp(orig.resources[i].data.data(), back.resources[i].data.data(), 4) != 0))
                    bad += c + " (" + std::to_string(orig.resources[i].data.size()) + " -> " +
                           std::to_string(back.resources[i].data.size()) + ") ";
        return bad.empty();
    };
    const std::vector<std::string> changedMain = {"/config/gui/Screens/MainMenu/MainMenu", "/config/gui/ScreenLayouts/MainMenu",
                                                  "/config/gui/Screens/MainMenu/LoadMission", "/config/gui/ScreenLayouts/LoadMission"};
    const std::vector<std::string> changedGui = {"/config/gui/Screens/GameMenu/GameMenu", "/config/gui/ScreenLayouts/GameMenu",
                                                 "/config/gui/Screens/GameMenu/Cheats", "/config/gui/ScreenLayouts/Cheats"};
    std::string bad;
    Check(sameShape(mainOrig, backMain, changedMain, bad) && sameShape(gOrig, backGui, changedGui, bad),
          "both: every changed resource keeps its size and prefix word", bad);
    Check(backMain.resources.size() == mainOrig.resources.size() && backGui.resources.size() == gOrig.resources.size(),
          "both: no resource added or removed");

    // everything else unchanged
    auto unchanged = [](const sds::Archive& orig, const sds::Archive& back, const std::vector<std::string>& changed) {
        if (orig.resources.size() != back.resources.size()) return false;
        for (size_t i = 0; i < orig.resources.size(); ++i) {
            bool isChanged = false;
            for (const auto& c : changed) isChanged = isChanged || orig.resources[i].sourceName == c;
            if (!isChanged && orig.resources[i].data != back.resources[i].data) return false;
        }
        return true;
    };
    Check(unchanged(mainOrig, backMain, changedMain), "gui-main.sds: every other resource is byte-identical (daemons too)");
    Check(unchanged(gOrig, backGui, changedGui), "gui.sds: every other resource is byte-identical");
    Check(backMain.index == mainOrig.index && backGui.index == gOrig.index, "both: the resource index text is unchanged");
}

}  // namespace

int main(int argc, char** argv) {
    TestInflate();
    TestBxml();
    TestSds();
    std::wstring outDir = L"menu_patch";
    wchar_t exe[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
        std::wstring e = exe;
        outDir = e.substr(0, e.find_last_of(L'\\')) + L"\\menu_patch";
    }
    if (argc > 1 && std::strcmp(argv[1], "--no-game") == 0)
        std::printf("SKIP  the game's archives (--no-game)\n");
    else
        TestGameFiles(outDir);
    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
