# Plan - next steps

Living plan for the Mafia II: Definitive Edition co-op mod. Update it as items land. Last updated 2026-10-18
(menu-only build, nothing tested in game yet). Read with `re-notes.md` (what we know about the game, all static so
far) and `../README-dev.md` (build, launch, the test checklist).

## Where we are

| Phase | State |
|---|---|
| 1 Launcher, injection, Present hook | Built, not tested in game |
| 2 Connection + co-op panels | CO-OP entries and pages in the game's own main and pause menus built (entries do nothing yet); core's overlay panels behind F8 as a fallback; no networking |
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
- [ ] **Launcher and injection**: `Mafia2Coop.exe` finds the game through Steam (app 1030830, exe in `pc\`), starts
  it suspended with `SteamAppId`, loads `mafia2coop.dll` by an early APC (fallback remote thread), logs to
  `logs\`. Gate: `logs\mafia2coop.log` shows `init: done`, the game reaches the main menu.
- [ ] **Present hook and overlay over Mafia II DE** (D3D11 flip-model swapchain, with the user's ReShade dxgi.dll in
  between): gate: `render: first frame via Present...` in the log, F7 status text draws.
- [ ] **CO-OP entry in the main menu** (after STORY) opening the game-drawn CO-OP page (EXTRAS look: title CO-OP,
  HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS; BACK / Esc / B return), keyboard, mouse and pad
  navigation by the game itself. Gate: screenshot of our page next to the game's EXTRAS page - same fonts, colours,
  highlight, footer, sounds.
- [ ] **CO-OP entry in the pause menu** (after OPTIONS) opening the same page in the pause OPTIONS look. Gate: as above
  against the pause OPTIONS page.
- [ ] **Overlay fallback**: F8 opens core's CO-OP panels (only for testing while the native pages are not working;
  remove once they are).
- [ ] **Input blocking for our overlay panels** (DirectInput, xinput1_4, GetAsyncKeyState, window messages). Only
  matters for the fallback and later for our own dialogs.

### B. Connection (phase 2)
- [ ] **B1 Move onto JustAddMP core**: `external/core` submodule pinned to a core release tag, drop the copied
  files (`src/core`, `src/render`, `src/dll`, `third_party`), GameModule + GetGameInfo as in Mafia DE; core changes
  needed for Mafia II (xinput1_4, GetAsyncKeyState blocking, exe in a subfolder, steam_api64 imported by the game)
  go to core first.
- [ ] **B2 Entries do things**: HOST / JOIN FRIEND / JOIN BY IP / LOBBY / SETTINGS call core's Coop. Needs a way
  for a menu action to reach our code: find the engine's action dispatcher (actions are hashed with FNV-1 64,
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
