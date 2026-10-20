#pragma once
// The update notice: once per game start the dll asks justaddmp.com for the newest Mafia II co-op release
// (core/update_check.h, copied from JustAddMP core) and, when it is newer than this build (GameInfo::modVersion), the
// game's own CO-OP pages (main menu and pause menu) show a line under their list in the look of the game's own notice
// on THE STORY page (game/menu_patch.h):
//   UPDATE AVAILABLE: V0.2.0 AT JUSTADDMP.COM
// The menus are data the game reads once, when it opens its menu archives (about 9 s after our dll starts), so the
// line goes into the patched archives then: the first archive build waits for a check still running, at most
// kMenuWaitMs. A check that has not answered by then, or fails, means no notice this time (one log line).
// No switch: nothing shows unless a newer release exists.
//
// Testing: Mafia2Coop.exe --update-as 0.0.0 (check as if this were 0.0.0; dll: MAFIA2COOP_UPDATE_AS) and
// --update-url <url> (ask another endpoint; dll: MAFIA2COOP_UPDATE_URL).
#include <string>
#include <string_view>

namespace mc::game::update_notice {

constexpr unsigned kMenuWaitMs = 5000;

// The line for a newer release `latest` as the endpoint wrote it ("v0.2.0" -> "UPDATE AVAILABLE: V0.2.0 AT
// JUSTADDMP.COM"); "" when `latest` is not a version (or absurdly long for one line).
std::string MenuLine(std::string_view latest);

// Starts the check on its own thread (dll init; once). Version and URL from GameInfo unless the launcher's
// --update-as / --update-url say otherwise.
void Start();

// The notice line for the menus, or "" (up to date, no release, failed, not started, or still running after the
// wait). Only the first call waits (at most waitMs) and logs; later calls answer at once.
std::string Line(unsigned waitMs = kMenuWaitMs);

}  // namespace mc::game::update_notice
