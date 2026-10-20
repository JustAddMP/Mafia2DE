#pragma once
// The game descriptor (a trimmed copy of JustAddMP core's include/mc/game_info.h, 8c9c0fd): names of the mod's
// files and what the launcher needs to find and start Mafia II: Definitive Edition. Defined in game_info.cpp, which
// depends on nothing else, so the launcher and the dll agree on every name.
#include <cstddef>
#include <cstdint>

namespace mc {

// One game exe build, identified by the CRC32 of the whole exe file.
struct GameBuild {
    uint32_t crc32;
    const char* label;
};

struct GameInfo {
    // ---- the mod ----
    const char* modName;           // "Mafia2Coop": banners, logs, kernel object names
    const char* modVersion;        // shown in the log and the settings panel; the update check compares with it
    const char* updateId;          // the update check asks https://justaddmp.com/api/version/<updateId>
    const wchar_t* launcherName;   // "Mafia2Coop.exe"
    const wchar_t* dllName;        // "mafia2coop.dll" (next to the launcher)
    const wchar_t* iniName;        // "Mafia2Coop.ini" (next to the launcher; only game_path / inject_method)
    const wchar_t* logPrefix;      // logs\<prefix>.log, logs\<prefix>-<tag>.log
    const char* envPrefix;         // launcher --key value -> <prefix>KEY=value for the dll
    uint16_t defaultPort;          // shown by the panels only (no networking in this build)

    // ---- the game ----
    const char* gameName;            // "Mafia II: Definitive Edition"
    const wchar_t* gameExe;          // exe path relative to the install folder ("pc\\Mafia II Definitive Edition.exe")
    uint32_t steamAppId;             // 1030830
    const wchar_t* steamInstallDir;  // default folder under steamapps\common when the app manifest has none
    const GameBuild* builds;         // known exe builds (the launcher logs whether the exe is one of them)
    size_t buildCount;
};

const GameInfo& GetGameInfo();

}  // namespace mc
