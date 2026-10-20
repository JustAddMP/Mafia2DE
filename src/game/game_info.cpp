// Mafia II: Definitive Edition descriptor. Linked into Mafia2Coop.exe and mafia2coop.dll, so it must stay free of
// other dependencies. The file names here must match what build.cmd produces.
#include "game/game_info.h"

namespace mc {
namespace {

// The exe this build was looked at with (docs/re-notes.md 1): Steam buildid 5172789 (appmanifest_1030830.acf),
// "pc\Mafia II Definitive Edition.exe", 37324288 bytes, PE timestamp 0x5EE8BA60 (2020-06-16).
constexpr GameBuild kBuilds[] = {
    {0xF35F0DD0u, "Steam build 5172789"},
};

constexpr GameInfo kInfo = {
    .modName = "Mafia2Coop",
    .modVersion = "0.0.1",
    .updateId = "mafia2-de",
    .launcherName = L"Mafia2Coop.exe",
    .dllName = L"mafia2coop.dll",
    .iniName = L"Mafia2Coop.ini",
    .logPrefix = L"mafia2coop",
    .envPrefix = "MAFIA2COOP_",
    .defaultPort = 27961,
    .gameName = "Mafia II: Definitive Edition",
    .gameExe = L"pc\\Mafia II Definitive Edition.exe",
    .steamAppId = 1030830,
    .steamInstallDir = L"Mafia II Definitive Edition",
    .builds = kBuilds,
    .buildCount = sizeof(kBuilds) / sizeof(kBuilds[0]),
};

}  // namespace

const GameInfo& GetGameInfo() { return kInfo; }

}  // namespace mc
