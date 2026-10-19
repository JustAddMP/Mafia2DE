#pragma once
// Serves the game patched copies of some of its archives without touching the game folder: kernel32 CreateFileW /
// CreateFileA (and the attribute queries) are hooked, and when the game opens one of these it gets
// <dll dir>\cache\<pid>-<name> instead, built on that first open from the game's own file (read, patched in memory,
// written once per game process):
//   ...\sds\gui\gui-main.sds, ...\sds\gui\gui.sds   the CO-OP entries and pages (game/menu_patch.h)
//   ...\sds\missionscript\freeraid_main.sds          our Free Ride script (game/freeride.h)
// If anything fails the game opens its own file (logged).
#include <string>

namespace mc::game::sds_redirect {

// Init thread, as early as possible (before the game loads its menus). False if the hooks failed.
bool Install(const std::wstring& dllDir);

}  // namespace mc::game::sds_redirect
