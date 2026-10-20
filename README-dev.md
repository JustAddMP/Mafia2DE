# Mafia II: Definitive Edition co-op - developer notes

Menu-only build: the mod gets into the game and puts a CO-OP entry into the game's own main menu and pause menu,
each opening a CO-OP page drawn by the game itself (HOST CO-OP GAME, FREE RIDE, JOIN FRIEND, JOIN BY IP, LOBBY,
SETTINGS).
FREE RIDE - on the main menu's CO-OP page and, since 2026-10-20, also as an entry of the main menu itself right
after CO-OP - starts a single-player Free Ride in Empire Bay (the game's own free ride
debug mission with our own script; code written, not tried in game yet - see "Free Ride" below); the other entries
do nothing yet; there is no networking. Plan: [docs/PLAN.md](docs/PLAN.md). What we know about the game:
[docs/re-notes.md](docs/re-notes.md). First in-game test: [docs/test-2026-10-18-menu-launch.md](docs/test-2026-10-18-menu-launch.md)
(main menu CO-OP entry and page work and look like the game's own; pause menu not reached yet).

## Which path the menu takes, and why

The user wants the co-op menu to look exactly like the game's own menus. Mafia II's menus are not Flash: they are
data - binary XML "screens" (events and actions per widget) and "screen layouts" (widgets, skins, the CREAMPUF font,
colours, positions) inside `pc\sds\gui\gui-main.sds` and `gui.sds` (re-notes.md 3). So the CO-OP menu is put
**into the game's own menu system**: the CO-OP entry in the main menu and the pause menu opens a CO-OP page built
from the game's own EXTRAS page (main menu) and pause OPTIONS page (pause menu) - the same widgets, skins, fonts,
highlight, footer prompts, sounds and transitions, only the title and the list entries are ours. The game draws and
drives the page (keyboard, mouse, pad) itself.

The game rejects a whole archive (black screen, no menus) if any of its XML resources changes size (seen in the
test launch, re-notes.md 4). So nothing is added: the CO-OP entries are the menus' hidden debug entries ("DEBUG
Load Chapter" right after STORY, the cheats entry right after OPTIONS) made visible with the condition the game uses
for EXIT and labelled CO-OP; their own debug screens (LoadMission, Cheats) become the CO-OP pages; every patched
resource is padded back to its exact size (trailing zeros on numbers and invisible filler group boxes in layouts,
no-op events in screens).

How it gets there without touching the game folder: when the game opens `...\sds\gui\gui-main.sds` or
`...\sds\gui\gui.sds` (the game tries `Documents\My Games\Mafia II Definitive Edition\Data\sds`, `EDIT\sds`,
`pc\sds_<lang>` and `pc\sds` in that order and takes the first that exists; only an existing one is patched), our
CreateFile hook builds a patched copy from the game's own file into
`<dll dir>\cache\<pid>-<name>.sds` and hands the game that one (`src/game/sds_redirect.cpp`,
`src/game/menu_patch.cpp`). No game assets are in this repo; the copies are made on the player's PC from their own
files and deleted at the next start.

The core-style overlay panels (copied from JustAddMP core, our generic look) are still in the dll behind **F8**, as
a fallback for this test only; they are not the target look.

**Free Ride** (docs/PLAN.md D-FR, re-notes.md 5.1): the main CO-OP page's FREE RIDE runs the game's own mission
loader for the base game's FREE RIDE debug mission (`game.mission.load`, mission 0, part 1 = Summer Day). That
mission's script archive `pc\sds\missionscript\freeraid_main.sds` is redirected like the menus, with our own
script (`src/game/freeride.cpp`, compiled for the game's Lua 5.1 with 4-byte floats by `src/game/lua51_compile.cpp`)
in place of the game's: no save, no test characters or cars, police on, our start position. A season picker does
not fit the menu's size budget yet (Summer Day only); to fit the FREE RIDE action, the main CO-OP page's footer A
glyph is hidden for good (keyboard and mouse see no change; a pad sees SELECT without the A glyph).
The main menu itself also has FREE RIDE, right after CO-OP, with exactly the same action. The main menu has no other
hidden entry to reuse, so it takes the place of the list's hidden promotion button (`btnPromoAction`, never shown in
the DE; re-notes.md 3); both main menu resources keep their exact size.

## Build and test

Needs Visual Studio 2022+ with the x64 C++ toolset and the Windows SDK (fxc). No CMake, no submodules.

```
build.cmd          builds out\Mafia2Coop.exe (launcher), out\mafia2coop.dll, out\menu_nav.exe, out\pad_nav.exe,
                   out\ui_prompts.exe, out\menu_patch_test.exe, out\freeride_test.exe (+ PDBs); objects in out\obj\
build.cmd test     the same, then runs every test (output in out\<test>.txt); exit code 0 = all passed
```

- `menu_patch_test`: zlib inflate (fixed / dynamic / stored), binary XML and SDS round trips on made-up data, and -
  when the game is installed (`M2_GAME_DIR` = the folder holding `pc\`, else the default Steam paths; read-only) -
  the real patch: both archives read, patched, written, read back; the CO-OP entries and pages are there, every
  changed resource keeps its exact size and prefix word, every other resource (daemons too) is byte-identical. It leaves `out\menu_patch\gui-main.sds`, `gui.sds` and
  `*.patch.txt` (the patched trees as XML text) for inspection.
  It also checks the main page's FREE RIDE action (`game.mission.load` mission 0 part 1, FirstCity, gameLine 1, hover
  kept, no other entry doing anything), the main menu's own FREE RIDE entry (order THE STORY, CO-OP, FREE RIDE,
  ADDITIONAL CONTENT, EXTRAS, OPTIONS, EXIT; a game entry's skin, font, colours and size; one row apart; the same
  action as the page's FREE RIDE; the promotion button's events gone; both main menu resources at their exact
  size) and the footer (BACK and SELECT kept, the A glyph check either kept or the
  glyph hidden for good).
- `freeride_test`: Lua 5.1 chunks with 4-byte floats (made-up chunk round trip, the loader's code checks), our
  compiler's exact output for small snippets and its refusals, the Script resource layout and FNV-1 64 hashes, our
  Free Ride script (compiles, passes the checks, no SaveGame / test characters / garage fill names, the start
  position constants, padded to exact sizes) run in a tiny test VM (`tests/mini_lua_vm.h`) against a recording mock
  of the game's API: the exact calls in order for parts 0-7. With the game installed: the game's own FreerideScript
  reads / writes back byte-identically and runs in the same VM as its listing says (checks the VM), and
  freeraid_main.sds is patched (Script resource same size and prefix word, our chunk, new data hash, every other
  resource byte-identical). Leaves `out\freeride\` (patched archive, our chunk `FreerideScript.ours.luac`, its
  source and listing, the game script's listing).
- `menu_nav`: core's overlay panels (the F8 fallback) driven by injected keys.
- `pad_nav`, `ui_prompts`: core's pad navigation and footer prompt logic (copied tests).

Last result (2026-10-20, FREE RIDE in the main menu): build warning-free, all five tests pass (menu_patch_test 53
checks, 39 of them against the installed game; freeride_test 63 checks, 12 of them against the installed game). Our chunk was also
listed with the independent Python lister used for the research: same functions and instruction sequences (267
instructions; the game's script: 1380).

## Launch (the coordinator's test)

The game must not be running. One instance:

```
G:\JustAddMP\mafia2-de\out\Mafia2Coop.exe --log test
```

The launcher finds the game through Steam (app 1030830), starts `pc\Mafia II Definitive Edition.exe` with
`SteamAppId=1030830`, loads `out\mafia2coop.dll` and exits. Logs: `out\logs\launcher-test.log` and
`out\logs\mafia2coop-test.log`. Patched archives and their reports: `out\cache\`.

Optional `out\Mafia2Coop.ini`: `inject_method = thread` (if the early APC injection fails), `game_path = <install
folder>` (if Steam lookup fails).

### What to look for in the log

`out\logs\launcher-test.log`:
- `steam: manifest ...appmanifest_1030830.acf -> ...\Mafia II Definitive Edition\pc\Mafia II Definitive Edition.exe`
- `version: exe CRC32 F35F0DD0 -> Steam build 5172789`
- `inject: dll loaded (via apc)`, then `game running (pid N); launcher done`

`out\logs\mafia2coop-test.log`, in this order:
- `Mafia2Coop 0.0.1, built ...`, `env SteamAppId=1030830`, `loaded by Mafia2Coop.exe`
- `module dxgi.dll: ...\pc\dxgi.dll  (not the system dll: ...)` - the user's ReShade
- `menu: archive redirect installed (CreateFileW 1, CreateFileA 1, attribute queries 1), cache ...\out\cache`
- `sds: game opens ...` lines: each archive path the game tries, once (up to 200); for every archive it tries
  `Documents\My Games\...\Data\sds\...`, `EDIT\sds\...`, `pc\sds_en\...`, then `pc\sds\...`. **Key lines** (gui.sds
  is opened first, then gui_definitive_edition.sds, then gui-main.sds):
  `menu: gui.sds: entry CO-OP: btnCheats in listboxGameMenu (1 visibility condition(s) -> always shown)`,
  `menu: gui.sds: /config/gui/ScreenLayouts/Cheats: 52 invisible filler(s) to keep its size`,
  `menu: gui.sds: page built in /config/gui/Screens/GameMenu/Cheats / /config/gui/ScreenLayouts/Cheats (6 entries, list listboxOptions)`,
  `menu: gui.sds patched in N ms: 2697688 -> 9628670 bytes, 88 resources -> ...cache\<pid>-gui.sds`,
  `sds: ...\pc\sds\gui\gui.sds -> ...cache\<pid>-gui.sds`; then for gui-main.sds
  `entry CO-OP: btnLoadMission in listboxMainMenu (2 visibility condition(s) -> always shown)`,
  `.../Screens/MainMenu/LoadMission: no remember-focus (size)`, `... plain fades (size)`, `... no fades (size)`,
  `.../ScreenLayouts/LoadMission: 12 invisible filler(s) to keep its size`,
  `page built in /config/gui/Screens/MainMenu/LoadMission / ... (6 entries, list listboxExtras)`,
  `.../Screens/MainMenu/LoadMission: the footer's A glyph hidden for good (its controller check dropped to keep the
  size; a pad sees SELECT without the glyph)`, `.../Screens/MainMenu/LoadMission: entry names 25 character(s) longer
  to keep its size`, `FREE RIDE -> game.mission.load mission 0 part 1 (SUMMER DAY), game FirstCity, gameLine 1`,
  `entry FREE RIDE: btnFreeRide in listboxMainMenu after btnLoadMission (in place of the hidden btnPromoAction)`,
  `gui-main.sds patched in N ms: 5812140 -> 14428432 bytes, 446 resources`, `sds: ...gui-main.sds -> ...`.
  A `FAILED: ... cannot keep its size` line means that menu stays unchanged (the game's own archive is used).
- `init: game window ... class 'Mafia II: Definitive Edition' title 'Mafia II: Definitive Edition'`
- `ui_input: installed (DirectInput ok, XInput ok via xinput1_4.dll, GetAsyncKeyState ok)`
- `render: swapchain vtable Present=... Present1=...`, `init: done; ...`
- `mod: first Tick`, `render: first frame via Present1: swapchain ... 1280x720 fmt=28 buffers=2 effect=3 windowed=1`
  (the resolution is the game's setting), then a `heartbeat:` line every 10 s.
- F8: `game: F8 pressed: open the CO-OP panel`, `ui: co-op panel opened ...`, `mod: panel state: ...`.
- Free Ride: see the next section.

### What to check on screen

1. **Main menu** (after the title screen's "Press any key"): a **CO-OP** entry right under THE STORY, same font, size, colour and highlight as the other entries.
   Mouse hover, arrow keys / d-pad and the pad highlight it like the others; the menu's own sounds play.
   Right under CO-OP: **FREE RIDE** (new 2026-10-20), the same look and highlight, then ADDITIONAL CONTENT, EXTRAS,
   OPTIONS, EXIT one row lower than before, no gap. Selecting it starts Free Ride exactly like the CO-OP page's FREE
   RIDE (see "Free Ride" below: back up the saves first). Esc / BACK on the main menu still go to the title screen.
2. Select it (Enter / click / A): the game's page transition, then a page that looks like **EXTRAS** but titled
   **CO-OP**, listing HOST CO-OP GAME, **FREE RIDE**, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS (in that order), with
   the usual footer (BACK / SELECT with the pad glyphs; with keyboard and mouse only BACK shows, as on EXTRAS;
   with a pad the main menu's CO-OP page now shows SELECT without its A glyph - the price of the FREE RIDE action).
   FREE RIDE (new since the 2026-10-18 test) must look exactly like its neighbours: same font, size, colour, left
   edge, row spacing (one row under HOST CO-OP GAME, the rest one row lower than before), highlight on hover / keys /
   pad, the same move sound. Navigate the page with keys, mouse and pad; the highlight goes through all six entries
   in order and wraps as on EXTRAS. Selecting an entry does nothing (expected), except FREE RIDE on the main menu's
   page (next section; in the pause menu FREE RIDE does nothing yet).
   The page now opens without the 0 s fade elements (they were dropped to keep the screen's size; EXTRAS' own fades
   are 0 s too): the transition should look the same as before.
   Esc / B / the footer's BACK return to the main menu.
3. **Compare**: screenshot the CO-OP page and the game's own EXTRAS page side by side (same resolution): header bar,
   title font, list font and spacing, selection highlight, footer and glyphs should match; only the texts differ.
   F9 (game window in the foreground) saves the frame as the game drew it, before ReShade or any other injector
   chained after our Present hook, to `out\logs\shots\raw-<n>.bmp`; the docs screenshots are taken that way.
4. **Pause menu** (load a save, Esc; not done in the 2026-10-18 test): a **CO-OP** entry under OPTIONS; it opens the CO-OP page in the look of the
   pause OPTIONS page, with the same six entries (FREE RIDE second); BACK returns to the pause menu. Screenshot it
   next to the pause OPTIONS page.
5. **Fallback** (only if the native entries are missing): F8 opens core's overlay CO-OP panel over the game
   (keyboard / mouse / pad, Esc closes); F7 toggles a small status text. Not the target look.
6. Quit the game normally; nothing else to clean up (`out\cache\` is cleared at the next start).

### Free Ride (in-game check, not done yet)

FREE RIDE on the main menu's CO-OP page loads the game's free ride mission with our script. Our script never saves,
but a mission load is a new-game style load inside the engine, and if our script were not used the game's own
free ride script would save into slot 1 - **back up the saves first** and check them afterwards.

1. **Back up the saves** (with the game closed): copy the whole folder
   `Documents\My Games\Mafia II Definitive Edition` (on this PC: `C:\Users\harry\OneDrive\Documents\My Games\Mafia
   II Definitive Edition`, holding `Saves\<steam id>\` and `Data\`) to a folder outside Documents, e.g.
   `G:\M2-save-backup-<date>`. Note the file names, sizes and times in `Saves\<steam id>\`. Steam Cloud keeps the
   saves too; if one ever changes, restore the copy before Steam syncs it (or turn Steam Cloud off for the game during
   these tests).
2. Launch as usual (`out\Mafia2Coop.exe --log test`), main menu -> CO-OP -> **FREE RIDE** (Enter / click / A), or
   main menu -> **FREE RIDE** (the same action).
3. Expected: the game's loading screen (its own, probably the mission loading picture), then Empire Bay in summer,
   daytime, clear weather; the player (Vito, in whatever clothes the free ride mission gives him) on a street in
   Little Italy, between parked / passing cars, control unlocked after a 1 s fade in; traffic driving, pedestrians,
   the radio in cars (Empire / Classic / Delta), police patrols; no Joe or Henry next to the player, no car chase, no
   "saving" indicator. Walk, take a car, drive a bit; look for shops (the gun shop, clothes) and a phone box.
4. Pause menu (Esc): note what it offers in this debug mission (RESUME, RETRY, EXIT, ...) and use EXIT / quit to the
   main menu; note what happens. Then quit the game normally.
5. **Check the saves**: same files, sizes and times as noted in step 1 (`Saves\<steam id>\`). If anything changed,
   close the game and restore the backup.
6. Screenshots (F9) of the loaded city and the start position; keep `out\logs\mafia2coop-test.log` and
   `out\cache\<pid>-freeraid_main.sds.patch.txt`.

What the log lines mean (`out\logs\mafia2coop-test.log`, after FREE RIDE is chosen):
- `sds: game opens ...\sds\missionscript\freeraid_main.sds` (four lines, one per place of the search order): the
  game is loading the FREE RIDE mission's script archive - the menu action worked and the mission loader runs
  mission 0. (Mission script and sky archives are always logged, also after the 200-path cap.)
- `freeride: the game loads the FREE RIDE mission's script archive ...\pc\sds\missionscript\freeraid_main.sds
  (route: the FREE RIDE entry -> game.mission.load mission 0 -> the stream map's "Free Raid" game)`: **the route
  that ran** (the only route: the unused `game.start operation="FreeRide"` is not one, re-notes.md 5.1).
- `freeride: /scripts/missions/FreerideMain/FreerideScript.lua: our script (9 functions, 267 instructions, padded to
  13417 bytes) replaces the game's (30 functions, 1380 instructions); data hash B92A93DCD8A59989 -> B3B5F23ADA653532;
  resource 13529 bytes and prefix word kept`: our script went into the archive at the game's exact size.
- `freeride: our Free Ride script ready in N ms -> ...\out\cache\<pid>-freeraid_main.sds` and
  `sds: ...\pc\sds\missionscript\freeraid_main.sds -> ...\out\cache\<pid>-freeraid_main.sds`: the game was handed
  our copy. Its report with our script's source and listing: `out\cache\<pid>-freeraid_main.sds.patch.txt`.
- `sds: game opens ...\sds\skies\freeride.sds`: the free ride sky (its day presets) loads - the "Free Raid" stream
  map game is running.
- **Stop and restore the saves** if instead you see `freeride: ... not patched; the game's own free ride script runs
  (it saves into slot 1!)` or `the game's own free ride script runs` - then the game's original script ran, which
  saves.
- No `freeride:` line at all after choosing FREE RIDE: the action did not start the mission (note what the screen
  did), or the game found the archive elsewhere (look for `freeraid_main` in the `sds: game opens` lines).

What would mean what:
- Black screen / endless loading after the lines above: the mission or its city does not load in the DE (debug
  mission assets), or our script stopped (a Lua error is not logged by us yet). Note how long it stayed.
- City loads but the player cannot move / no fade in: our script stopped before its last step.
- Player falls / stands inside something: the start position is wrong (`freeride::kStart`).
- A save changed: tell us which file - the engine itself saved during the mission load.

## Verified in the test launch of 2026-10-18 (docs/test-2026-10-18-menu-launch.md)

- Injection by early APC, the game reaching its menus; the Present hook through the user's ReShade dxgi.dll
  (`first frame via Present1`; F7 status text drawn).
- The game opens its gui archives through CreateFileA by full path, in a search order (see the log section); the
  redirect to our rebuilt copies with raw blocks (14 MB instead of 5.8 MB) is accepted.
- Main menu: CO-OP under THE STORY in the game's font, colour and highlight; arrow keys and mouse hover select it;
  Enter / click open the CO-OP page (EXTRAS look, title CO-OP, the five entries, BACK footer); keys and mouse move
  the highlight there; Enter on an entry does nothing; Esc and a click on the footer's BACK return to the main menu
  with CO-OP still selected. EXTRAS still opens and looks as before.

## UNVERIFIED

- Free Ride entirely (re-notes.md 5.1): that FREE RIDE's `game.mission.load` starts mission 0 in the DE, that the
  debug mission and its city load, that the game accepts our script resource (same size and prefix word, new data
  hash), that our script runs to the end (no Lua error in the game's API use), the start position, traffic / police /
  radio, that no save is written by the engine itself, the pause menu in the mission. The main menu's own FREE RIDE
  entry (2026-10-20): that the archive still loads without the promotion button, its look, the padding events on
  the main menu's BACK. The main CO-OP page's footer
  without the A glyph check (pad only).

- The pause menu entry and page (needs a loaded save; not done in the test so as not to touch the saves).
- Pad navigation and the pad's footer glyphs (no controller in the test), and the menu sounds (the game was muted;
  the page uses the same list, skin and footer widgets as EXTRAS, which carry the sounds).
- That the padding is harmless everywhere: invisible filler group boxes in the page layouts, no-op
  `menu.removeVariable` events on the page's last widget (the main menu page loads and works with them).
- Input blocking for the F8 fallback panels: xinput1_4 hook, DirectInput, GetAsyncKeyState (the game polls keys with
  it), window messages. (F8 itself was not needed: the native menus work.)
- Window focus: WM_ACTIVATEAPP(FALSE) is swallowed so the game keeps running unfocused. Started without the mod and
  never focused, the game stayed black (30 s); in the test the game was always given the focus once at the start.
