#pragma once
// The CO-OP entries in Mafia II: Definitive Edition's own menus. The game's menus are data: binary XML "screens"
// (events and actions per widget) and "screen layouts" (widgets, skins, fonts, positions) inside
// /sds/gui/gui-main.sds and /sds/gui/gui.sds (docs/re-notes.md 3). We patch that data, so the game draws and drives
// the CO-OP page itself - its fonts, skins, highlight, sounds, footer prompts and transitions:
//   main menu   a CO-OP entry after STORY opens a CO-OP page built like the game's EXTRAS page (title, list,
//               footer) with HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS; Esc / B / BACK go back
//   pause menu  a CO-OP entry after OPTIONS opens the same page built like the pause menu's OPTIONS page
// No new resources are added: the CO-OP pages reuse two screen/layout pairs the retail game never opens
// (main menu: MafiaLogo, pause menu: Cheats) and the menus' daemons get a "Coop" screen pointing at them.
// The entries on the CO-OP pages do nothing yet (menu-only build).
#include "game/sds.h"

#include <string>
#include <vector>

namespace mc::game::menu_patch {

struct Result {
    bool ok = false;
    std::vector<std::string> log;     // what was changed (or why not)
    std::vector<std::pair<std::string, std::string>> dumps;  // resource name -> readable XML of the patched tree
};

// gui-main.sds: main menu entry + page, both daemons' "Coop" screens.
Result PatchGuiMain(sds::Archive& guiMain);
// gui.sds: pause menu entry + page. `guiMain` (unpatched or patched) supplies the OPTIONS page layout template.
Result PatchGui(sds::Archive& gui, const sds::Archive& guiMain);

// The labels on the pages (also used by the tests).
const std::vector<std::string>& CoopEntries();

}  // namespace mc::game::menu_patch
