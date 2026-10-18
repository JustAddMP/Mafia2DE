# Mafia II: Definitive Edition co-op - developer notes

Menu-only build: the mod gets into the game and puts a CO-OP entry into the game's own main menu and pause menu,
each opening a CO-OP page drawn by the game itself (HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS).
The entries do nothing yet; there is no networking. Plan: [docs/PLAN.md](docs/PLAN.md). What we know about the game:
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

## Build and test

Needs Visual Studio 2022+ with the x64 C++ toolset and the Windows SDK (fxc). No CMake, no submodules.

```
build.cmd          builds out\Mafia2Coop.exe (launcher), out\mafia2coop.dll, out\menu_nav.exe, out\pad_nav.exe,
                   out\ui_prompts.exe, out\menu_patch_test.exe (+ PDBs); objects in out\obj\
build.cmd test     the same, then runs every test (output in out\<test>.txt); exit code 0 = all passed
```

- `menu_patch_test`: zlib inflate (fixed / dynamic / stored), binary XML and SDS round trips on made-up data, and -
  when the game is installed (`M2_GAME_DIR` = the folder holding `pc\`, else the default Steam paths; read-only) -
  the real patch: both archives read, patched, written, read back; the CO-OP entries and pages are there, every
  changed resource keeps its exact size and prefix word, every other resource (daemons too) is byte-identical. It leaves `out\menu_patch\gui-main.sds`, `gui.sds` and
  `*.patch.txt` (the patched trees as XML text) for inspection.
- `menu_nav`: core's overlay panels (the F8 fallback) driven by injected keys.
- `pad_nav`, `ui_prompts`: core's pad navigation and footer prompt logic (copied tests).

Last result (2026-10-18, after the first test launch): build warning-free, all four tests pass (menu_patch_test 36
checks against the installed game).

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
  `menu: gui.sds: /config/gui/ScreenLayouts/Cheats: 55 invisible filler(s) to keep its size`,
  `menu: gui.sds: page built in /config/gui/Screens/GameMenu/Cheats / /config/gui/ScreenLayouts/Cheats (5 entries, list listboxOptions)`,
  `menu: gui.sds patched in N ms: 2697688 -> 9628670 bytes, 88 resources -> ...cache\<pid>-gui.sds`,
  `sds: ...\pc\sds\gui\gui.sds -> ...cache\<pid>-gui.sds`; then for gui-main.sds
  `entry CO-OP: btnLoadMission in listboxMainMenu (2 visibility condition(s) -> always shown)`,
  `.../Screens/MainMenu/LoadMission: no remember-focus (size)`, `... plain fades (size)`,
  `.../ScreenLayouts/LoadMission: 15 invisible filler(s) to keep its size`,
  `page built in /config/gui/Screens/MainMenu/LoadMission / ... (5 entries, list listboxExtras)`,
  `gui-main.sds patched in N ms: 5812140 -> 14428432 bytes, 446 resources`, `sds: ...gui-main.sds -> ...`.
  A `FAILED: ... cannot keep its size` line means that menu stays unchanged (the game's own archive is used).
- `init: game window ... class 'Mafia II: Definitive Edition' title 'Mafia II: Definitive Edition'`
- `ui_input: installed (DirectInput ok, XInput ok via xinput1_4.dll, GetAsyncKeyState ok)`
- `render: swapchain vtable Present=... Present1=...`, `init: done; ...`
- `mod: first Tick`, `render: first frame via Present1: swapchain ... 1280x720 fmt=28 buffers=2 effect=3 windowed=1`
  (the resolution is the game's setting), then a `heartbeat:` line every 10 s.
- F8: `game: F8 pressed: open the CO-OP panel`, `ui: co-op panel opened ...`, `mod: panel state: ...`.

### What to check on screen

1. **Main menu** (after the title screen's "Press any key"): a **CO-OP** entry right under THE STORY, same font, size, colour and highlight as the other entries.
   Mouse hover, arrow keys / d-pad and the pad highlight it like the others; the menu's own sounds play.
2. Select it (Enter / click / A): the game's page transition, then a page that looks like **EXTRAS** but titled
   **CO-OP**, listing HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS, with the usual footer (BACK / SELECT
   with the pad glyphs; with keyboard and mouse only BACK shows, as on EXTRAS). Navigate it with keys, mouse and pad. Selecting an entry does nothing (expected).
   Esc / B / the footer's BACK return to the main menu.
3. **Compare**: screenshot the CO-OP page and the game's own EXTRAS page side by side (same resolution): header bar,
   title font, list font and spacing, selection highlight, footer and glyphs should match; only the texts differ.
   F9 (game window in the foreground) saves the frame as the game drew it, before ReShade or any other injector
   chained after our Present hook, to `out\logs\shotsaw-<n>.bmp`; the docs screenshots are taken that way.
4. **Pause menu** (load a save, Esc; not done in the 2026-10-18 test): a **CO-OP** entry under OPTIONS; it opens the CO-OP page in the look of the
   pause OPTIONS page; BACK returns to the pause menu. Screenshot it next to the pause OPTIONS page.
5. **Fallback** (only if the native entries are missing): F8 opens core's overlay CO-OP panel over the game
   (keyboard / mouse / pad, Esc closes); F7 toggles a small status text. Not the target look.
6. Quit the game normally; nothing else to clean up (`out\cache\` is cleared at the next start).

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

- The pause menu entry and page (needs a loaded save; not done in the test so as not to touch the saves).
- Pad navigation and the pad's footer glyphs (no controller in the test), and the menu sounds (the game was muted;
  the page uses the same list, skin and footer widgets as EXTRAS, which carry the sounds).
- That the padding is harmless everywhere: invisible filler group boxes in the page layouts, no-op
  `menu.removeVariable` events on the page's last widget (the main menu page loads and works with them).
- Input blocking for the F8 fallback panels: xinput1_4 hook, DirectInput, GetAsyncKeyState (the game polls keys with
  it), window messages. (F8 itself was not needed: the native menus work.)
- Window focus: WM_ACTIVATEAPP(FALSE) is swallowed so the game keeps running unfocused. Started without the mod and
  never focused, the game stayed black (30 s); in the test the game was always given the focus once at the start.
