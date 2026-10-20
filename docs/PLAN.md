# Plan - next steps

Living plan for the Mafia II: Definitive Edition co-op mod. Update it as items land. Last updated 2026-10-19
(Free Ride code written, single player, waiting for an in-game test: D-FR; re-notes.md 5.1). Read with `re-notes.md` (what we
know about the game) and `../README-dev.md` (build, launch, the test checklist).

## Where we are

| Phase | State |
|---|---|
| 1 Launcher, injection, Present hook | Works in game (2026-10-18), through the user's ReShade |
| 2 Connection + co-op panels | Main menu CO-OP entry and page work in game and look like the game's own (entries do nothing yet); pause menu entry built, not reached in a test yet; core's overlay panels behind F8 as a fallback; no networking |
| 3 Second player | Not started |
| 4 Shared world | Not started |
| 5 Polish, release | Not started |

## Working rules

- Clean-room: no code or data from other multiplayer mods (Mafia II ones included). Allowed: the game's own files
  (static analysis, read-only), public documentation (Microsoft, zlib/deflate RFCs, Lua manual) and our own code
  (JustAddMP core and the Mafia DE repo are ours and may be copied).
- The game folder is read-only. We never write into it; patched data goes to our own folder (`<dll dir>\cache\`)
  and is built at run time from the user's own game files - no game assets in this repo.
- Authenticity: whatever the player sees should look like Mafia II DE's own UI. Menus are the game's own (data
  patched into its menu system, `src/game/menu_patch.cpp`); our overlay panels (core's look) are only a fallback.
- Tests are part of features: every item below includes its automated tests where logic can run without the game
  (`build.cmd test`); there are no separate test items.
- This build is self-contained (no JustAddMP core submodule yet; core files it needed are copied with a note of
  the core commit they came from). Moving onto core is item B1.
- Commit on main; no Claude attribution lines; the coordinator pushes after a test.

## Next steps, in order

Tick an item (`- [x]`) in the same commit that lands it. Nothing is ticked until it worked in a test launch.

### A. Menus render in the game (phase 1 + 2, current work)
- [x] **Launcher and injection**: `Mafia2Coop.exe` finds the game through Steam (app 1030830, exe in `pc\`), starts
  it suspended with `SteamAppId`, loads `mafia2coop.dll` by an early APC (fallback remote thread), logs to
  `logs\`. Gate: `logs\mafia2coop.log` shows `init: done`, the game reaches the main menu.
- [x] **Present hook and overlay over Mafia II DE** (D3D11 flip-model swapchain, with the user's ReShade dxgi.dll in
  between): gate: `render: first frame via Present...` in the log, F7 status text draws.
- [ ] **CO-OP entry in the main menu** (after STORY) opening the game-drawn CO-OP page (EXTRAS look: title CO-OP,
  HOST CO-OP GAME, FREE RIDE, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS; BACK / Esc / B return), keyboard, mouse and pad
  navigation by the game itself. Gate: screenshot of our page next to the game's EXTRAS page - same fonts, colours,
  highlight, footer, sounds. Status 2026-10-18: works with keyboard and mouse and matches EXTRAS (screenshots in the
  test log); still to check: pad navigation and glyphs, the sounds (the test ran muted, no pad). 2026-10-19: FREE
  RIDE added as the second entry (both pages; does nothing yet), not seen in game yet. Later the same day: the
  main page's FREE RIDE starts Free Ride (D-FR) and that page's footer A glyph is hidden for good to fit it.
  2026-10-20: the main menu itself also has FREE RIDE, right after CO-OP (D-FR); not seen in game yet.
- [ ] **CO-OP entry in the pause menu** (after OPTIONS) opening the same page in the pause OPTIONS look. Gate: as above
  against the pause OPTIONS page. Status: built (the hidden cheats entry), the archive loads; needs a loaded save.
- [ ] **Overlay fallback**: F8 opens core's CO-OP panels (only for testing while the native pages are not working;
  remove once they are - the main menu's native page works since 2026-10-18; remove after the pause menu is checked).
- [ ] **Input blocking for our overlay panels** (DirectInput, xinput1_4, GetAsyncKeyState, window messages). Only
  matters for the fallback and later for our own dialogs.

### B. Connection (phase 2)
- [ ] **B1 Move onto JustAddMP core**: `external/core` submodule pinned to a core release tag, drop the copied
  files (`src/core`, `src/render`, `src/dll`, `third_party`), GameModule + GetGameInfo as in Mafia DE; core changes
  needed for Mafia II (xinput1_4, GetAsyncKeyState blocking, exe in a subfolder, steam_api64 imported by the game)
  go to core first.
- [ ] **B2 Entries do things**: HOST / JOIN FRIEND / JOIN BY IP / LOBBY / SETTINGS call core's Coop (FREE RIDE: item
  D-FR). Needs a way for a menu action to reach our code: find the engine's action dispatcher (actions are hashed with FNV-1 64,
  re-notes.md 3) and hook it for our own action names, or watch screen activation. The game's own dialog
  screens (`Screens/Dialog/SimpleDialog`, `ListDialog`, keyboard `dialog.launchKeyboard`) for JOIN BY IP and
  messages, so they look native too.
- [ ] **B3 Native look for core's other panels** (lobby, settings, join friend, banners): pages / dialogs in the
  game's menu system; banners in the game's HUD style (find how the HUD shows its own notices).
- [ ] **B4 Steam**: the game imports steam_api64 itself; check that core's own `SteamAPI_Init` + callback pumping
  coexists with the game's (shared module, two pumpers), invites and the friends list.
- [ ] **B5 Two instances on one PC**: the startup / launcher mutexes (re-notes.md 1), window placement.
- [ ] **B6 Version.dll proxy ("start from Steam")** and the release package.

### C. Second player (phase 3)
- [ ] Lua bridge: find how the game runs its scripts (Lua 5.1 seems linked into the exe - the `Lua_*DLL.dll` files in
  `pc\` are unused 32-bit leftovers; Mafia DE used Havok Script), a dev console.
- [ ] Local player state (position, heading, animation state, weapon, car seat) and puppets for remote players.
- [ ] Cars: copies, seats, ownership hand-off.
- [ ] Damage and deaths.

### D. Shared world (phase 4)
- [ ] **D-FR Free Ride** (co-op without the story: both players in an open Empire Bay, cars, traffic, police, a
  chosen time of day and weather). Research: re-notes.md 5. Most promising approach: start the game's own,
  still shipped FREE RIDE debug mission (base game: mission `freeride` number 0, game `FirstCity`, stream-map
  game "Free Raid", script `/missions/FreerideMain`; parts 1-6 = summer day / rain / night, winter day / snow /
  night). Steps: (1) the CO-OP page's FREE RIDE entry starts it in single player through the game's own menu
  action `game.mission.load` (data only, same-size patch; a part picker page in the game's style for the six
  weathers); (2) our own FreerideMain replacement (no test cars and NPCs, no `SaveGame(1)`, our start position)
  through the Lua bridge (item C) or a patched script resource; (3) host starts Free Ride, the guest is sent
  the same part and loads it, then phase 3 (players, cars) and D's time / weather / traffic sync run on it.
  Done includes tests: the menu patch test checks the entry's action and parts; a two-instance test drives
  around together. Open questions: the meaning of `game.mission.load`'s value (mission number or index?) and
  whether `gameLine` 1 is right for the base game; does the debug mission still load in the DE (assets, the
  `Free Raid` stream-map lines) and does the city come up with traffic, police and shops; its `SaveGame(1)`
  (may overwrite a story save - back up saves before any test); whether `game.start operation="FreeRide"` (in the
  exe, unused by any menu) is a cleaner entry; the JA free ride (`gameLine 6`, mission 60) as the alternative
  with Joe as the player; how the guest's game is told which part to load; pause menu behaviour (RETRY / EXIT)
  in a debug mission.
  Status 2026-10-19 (code written, nothing run in game; not ticked): steps (1) and (2) for single player.
  - Statically answered (re-notes.md 5.1): `game.mission.load`'s value is the mission **number** (the game's debug
    part list builds `<use action="game.mission.load" part="\uN" game=GAME gameLine=LINE>NUMBER</use>` from the
    mission list, whose defaults are game FirstCity and gameLine 1); gameLine 1 = the story's save id = the base
    game. `game.start operation="FreeRide"` is no route (its hashed name is never compared; it acts like any
    unknown operation) - not wired.
  - (1) The main CO-OP page's FREE RIDE runs `game.mission.load` mission 0 part 1 (**Summer Day**, the default):
    a season picker page does not fit any free screen of the main menu (about 450 bytes per entry; LoadMission
    is the CO-OP page, MissionParts holds not even one entry). To fit the one action, the CO-OP page's footer A
    glyph is hidden for good (its pad-only check dropped). FREE RIDE in the pause menu does nothing yet.
  - (2) `freeraid_main.sds` is served through the archive redirect with our own script (`src/game/freeride.cpp`,
    compiled by our Lua 5.1 / 4-byte-float compiler `src/game/lua51_compile.cpp`): no SaveGame, no Joe / Henry,
    no chase cars, no garage fill, police on, start at (129.164, 917.259, -21.260) on the street of the game's
    test cars; resource size and prefix word kept. Tests: `freeride_test` (format, compiler, our script run in a
    test VM for every part, the real archive), `menu_patch_test` (the FREE RIDE action, the footer).
  - 2026-10-20 (0.17): FREE RIDE also as an entry of the main menu itself, right after CO-OP (THE STORY, CO-OP,
    FREE RIDE, ADDITIONAL CONTENT, EXTRAS, OPTIONS, EXIT), with the same `game.mission.load` action as the CO-OP
    page's FREE RIDE (which stays). A copy of the game's ADDITIONAL CONTENT button (skin, font, colours, size), one
    row under CO-OP, the entries below one row down; it takes the place of the main menu's hidden promotion button
    `btnPromoAction` (re-notes.md 3), both main menu resources keep their exact size. `menu_patch_test` checks the
    order, style, rows, action and sizes. Not seen in game yet (not ticked).
  - Next: the in-game check (README-dev "Free Ride"), then a season picker (needs a bigger host screen or shared
    strings in the binary XML - check whether the game's binary XML reader resolves string offsets freely),
    FREE RIDE from the pause menu, then step (3).
- [ ] Time of day / weather, story progress (chapters, checkpoints, objectives), cutscenes, mission NPCs and cars,
  traffic and police, items. Research first, as for Mafia DE (docs/research-world-sync.md there).

### E. Polish and release (phase 5)
- [ ] Name tags / map markers in the game's style, settings, reconnect, README with screenshots, release package,
  website entry update.

## What carries over from Mafia DE, and what must be re-researched

Carries over (our own code, game-agnostic or nearly):
- All of JustAddMP core: launcher (with the exe-in-subfolder path), injection, crash handler, logging, MinHook,
  Present hook + D3D11 overlay (the game is D3D11), networking (UDP, Steam P2P, sessions, lobby, reconnect,
  interest management, remote motion controllers), the co-op panels' logic (their look must become Mafia II's),
  test input scripting.
- The protocol and the sync design: host-authoritative star, player state, car seats and ownership, damage events,
  world-state messages (docs/protocol.md in Mafia DE and core).
- The way native menus were extended: patch the game's own UI data at load time, never on disk (Mafia DE patched
  Scaleform AS3 bytecode; here it is binary XML in SDS archives).
- Process: static research first, game-api.md written as things get verified, sync-trace logs, two instances side
  by side for tests.

Must be re-researched for Mafia II DE (different engine generation, nothing of Mafia DE's offsets applies):
- Every engine address, pattern and struct (Mafia DE's re-notes.md is for a different exe).
- The menu system (done statically: binary XML screens, re-notes.md 3) and how menu actions reach C++.
- The script layer: Lua 5.1 (apparently linked into the exe) and the game's script API (Mafia DE used Havok Script with
  a different binding layer); which game objects scripts can reach.
- Player, human, car and AI objects: spawning puppets, driving them, seats, animation, damage.
- Steam: the game's own steam_api64 import (Mafia DE memory-loaded an old one).
- Input: xinput1_4 by ordinal, DirectInput 8, GetAsyncKeyState (Mafia DE: xinput9_1_0).
- Single-instance checks, save system, mission / chapter structure, cutscenes, the HUD and its notices.
