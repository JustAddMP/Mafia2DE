#pragma once
// Free Ride (docs/PLAN.md D-FR, docs/re-notes.md 5): a single-player free roam in Empire Bay started from the FREE
// RIDE entry of the main menu's CO-OP page.
//   1. The entry runs the game's own mission loader: <use action="game.mission.load" part="\u<part>"
//      game="\sFirstCity" gameLine="\u1">\u0</use> - exactly what the game's debug mission list makes for the base
//      game's FREE RIDE debug mission (mission 0, parts 1-6 = the six season / weather presets); see re-notes 5 for
//      the evidence in the exe. Built into the menu by game/menu_patch.cpp.
//   2. That mission's script archive /sds/missionscript/freeraid_main.sds is served with our own script in place
//      of the game's FreerideScript.lua (game/sds_redirect.cpp): the same start sequence (the season's city, the
//      weather preset, the city part around the start, traffic, speech, radio, control, fade in) without the test
//      content (Joe, Henry, the Hunter / Escaper chase, the garage filled with every car), without SaveGame (no
//      save is written), with police switched on and our own start position. Compiled by game/lua51_compile.h
//      for the game's Lua 5.1 with 4-byte floats and padded to the original script's exact size (the game checks
//      resource sizes, re-notes 4).
#include "game/lua51.h"
#include "game/sds.h"

#include <string>
#include <vector>

namespace mc::game::freeride {

// The mission loader action of the FREE RIDE entry.
constexpr unsigned kMission = 0;       // <mission name="freeride" number="0"> in /config/gui/Missions
constexpr char kGame[] = "FirstCity";  // the mission list's default game (no game="..." on the entry)
constexpr unsigned kGameLine = 1;      // the mission list's default gameLine = the story's save id
constexpr unsigned kDefaultPart = 1;   // Summer Day (the season picker is planned; see docs/PLAN.md D-FR)

// The six parts of the FREE RIDE mission (the mission list's part titles) and the day presets our script sets.
struct Preset {
    unsigned part;
    const char* label;      // for a picker page: the game's capitals style
    const char* dayPreset;  // pc\sds\skies\freeride.sds
    bool winter;
};
const std::vector<Preset>& Presets();
const Preset* PresetOf(unsigned part);

// Our start position: on the street where the game's free ride script parks its two test cars (Hunter at
// 129.172, 910.934, -22.050 and Escaper at 129.156, 923.584, -22.470: one lane, two cars in a row), half-way
// between them, 1 m above the cars' height so the player drops onto the road. UNVERIFIED in game.
struct Pos {
    float x, y, z;
};
constexpr Pos kStart{129.164f, 917.259f, -21.260f};

// Our script (Lua source as compiled; the start position and presets above are filled in).
std::string Source();
// Compiles Source() (lua51::Compile).
bool Compile(lua51::Proto& out, std::string* error);
// The chunk padded to exactly `size` bytes: the main function's source name ("=JustAddMP FreeRide", then NUL bytes)
// takes up the difference. False if the compiled script is bigger than `size`.
bool BuildChunk(size_t size, std::vector<uint8_t>& out, std::string* error);

constexpr char kScriptName[] = "/scripts/missions/FreerideMain/FreerideScript.lua";

struct Result {
    bool ok = false;
    std::vector<std::string> log;
    std::string listing;  // lua51::List of the chunk we put in
};
// freeraid_main.sds: replaces the data of the script kScriptName in the archive's Script resource with our chunk
// (same size; the script's data hash updated; the resource's size and prefix word unchanged; every other resource
// untouched).
Result PatchArchive(sds::Archive& a);

}  // namespace mc::game::freeride
