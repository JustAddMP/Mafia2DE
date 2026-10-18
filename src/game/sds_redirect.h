#pragma once
// Serves the game patched copies of its two menu archives (game/menu_patch.h) without touching the game folder:
// kernel32 CreateFileW / CreateFileA are hooked, and when the game opens ...\sds\gui\gui-main.sds or
// ...\sds\gui\gui.sds it gets <dll dir>\cache\<pid>-<name> instead, built on that first open from the game's own file
// (read, patched in memory, written once per game process). If anything fails the game opens its own file and the
// menus are unchanged (logged).
#include <string>

namespace mc::game::sds_redirect {

// Init thread, as early as possible (before the game loads its menus). False if the hooks failed.
bool Install(const std::wstring& dllDir);

}  // namespace mc::game::sds_redirect
