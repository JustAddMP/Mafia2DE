# Mafia II: Definitive Edition co-op - developer notes

Menu-only build: the mod gets into the game and puts a CO-OP entry into the game's own main menu and pause menu,
each opening a CO-OP page drawn by the game itself (HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS).
The entries do nothing yet; there is no networking. Plan: [docs/PLAN.md](docs/PLAN.md). What we know about the game
(all static so far): [docs/re-notes.md](docs/re-notes.md).

## Which path the menu takes, and why

The user wants the co-op menu to look exactly like the game's own menus. Mafia II's menus are not Flash: they are
data - binary XML "screens" (events and actions per widget) and "screen layouts" (widgets, skins, the CREAMPUF font,
colours, positions) inside `pc\sds\gui\gui-main.sds` and `gui.sds` (re-notes.md 3). So the CO-OP menu is put
**into the game's own menu system**: we add an entry to the main menu and the pause menu and build the CO-OP page
from the game's own EXTRAS page (main menu) and pause OPTIONS page (pause menu) - the same widgets, skins, fonts,
highlight, footer prompts, sounds and transitions, only the title and the list entries are ours. The game draws and
drives the page (keyboard, mouse, pad) itself.

How it gets there without touching the game folder: when the game opens `...\sds\gui\gui-main.sds` or
`...\sds\gui\gui.sds`, our CreateFile hook builds a patched copy from the game's own file into
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
  the real patch: both archives read, patched, written, read back; the CO-OP entries, daemons and pages are there,
  every other resource is byte-identical. It leaves `out\menu_patch\gui-main.sds`, `gui.sds` and
  `*.patch.txt` (the patched trees as XML text) for inspection.
- `menu_nav`: core's overlay panels (the F8 fallback) driven by injected keys.
- `pad_nav`, `ui_prompts`: core's pad navigation and footer prompt logic (copied tests).

Last result (2026-10-18): build warning-free, all four tests pass (menu_patch_test 37 checks against the installed
game).

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
- `sds: game opens ...` lines: every archive the game opens (the first 40). **Key line**:
  `sds: game opens ...\sds\gui\gui-main.sds`, then
  `menu: gui-main.sds: entry CO-OP added after btnStory in listboxMainMenu (row 0.0620)`,
  `menu: gui-main.sds: daemon /config/gui/Daemons/MainMenu: screen Coop -> MainMenu/MafiaLogo`,
  `menu: gui-main.sds: page built in /config/gui/Screens/MainMenu/MafiaLogo / ...`,
  `menu: gui-main.sds patched in N ms: 5812140 -> 14xxxxxx bytes, 446 resources -> ...cache\<pid>-gui-main.sds`,
  `sds: ...gui-main.sds -> ...cache\<pid>-gui-main.sds`; the same for `gui.sds` (pause menu, `after btnOptions`).
  If the `sds: game opens` lines never show the gui archives, the game opens them some other way (UNVERIFIED path).
- `init: game window ... class 'Mafia II: Definitive Edition' title 'Mafia II: Definitive Edition'`
- `ui_input: installed (DirectInput ok, XInput ok via xinput1_4.dll, GetAsyncKeyState ok)`
- `render: swapchain vtable Present=... Present1=...`, `init: done; ...`
- `mod: first Tick`, `render: first frame via Present...: swapchain ... 1920x1080 fmt=28 ... effect=3 ...`
  (or `via Present1`), then a `heartbeat:` line every 10 s.
- F8: `game: F8 pressed: open the CO-OP panel`, `ui: co-op panel opened ...`, `mod: panel state: ...`.

### What to check on screen

1. **Main menu**: a **CO-OP** entry right under STORY, same font, size, colour and highlight as the other entries.
   Mouse hover, arrow keys / d-pad and the pad highlight it like the others; the menu's own sounds play.
2. Select it (Enter / click / A): the game's page transition, then a page that looks like **EXTRAS** but titled
   **CO-OP**, listing HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS, with the usual footer (BACK / SELECT
   with the pad glyphs). Navigate it with keys, mouse and pad. Selecting an entry does nothing (expected).
   Esc / B / the footer's BACK return to the main menu.
3. **Compare**: screenshot the CO-OP page and the game's own EXTRAS page side by side (same resolution): header bar,
   title font, list font and spacing, selection highlight, footer and glyphs should match; only the texts differ.
4. **Pause menu** (load a save, Esc): a **CO-OP** entry under OPTIONS; it opens the CO-OP page in the look of the
   pause OPTIONS page; BACK returns to the pause menu. Screenshot it next to the pause OPTIONS page.
5. **Fallback** (only if the native entries are missing): F8 opens core's overlay CO-OP panel over the game
   (keyboard / mouse / pad, Esc closes); F7 toggles a small status text. Not the target look.
6. Quit the game normally; nothing else to clean up (`out\cache\` is cleared at the next start).

## UNVERIFIED (nothing here has run in the game yet)

- Injection by early APC into this exe, and the Present hook through the user's ReShade dxgi.dll (we hook
  ReShade's swapchain Present, so our overlay is drawn before ReShade's effects).
- That the game opens its gui archives through kernel32 CreateFileW/A (or asks GetFileAttributes) by a path ending
  in `sds\gui\gui-main.sds` / `gui.sds`.
- That it accepts our rebuilt archives: raw (uncompressed) blocks everywhere (it uses raw blocks itself in
  gui.sds), a bigger file (14 MB instead of 5.8 MB), the rewritten binary XML (zero padding instead of the
  original junk bytes).
- That a new `<screen name="Coop" source="MainMenu/MafiaLogo"/>` in the daemon is picked up and
  `screen.activate replace Coop` opens it (same for `GameMenu/Cheats` in the pause menu).
- That literal labels (`textid="0"`) render in the game's font and capitals; that the list re-flows or keeps our
  explicit row positions sensibly; that the cloned pages look exactly like EXTRAS / OPTIONS.
- That gui-main.sds (it holds the pause menu's daemon) is the one used in game too.
- Input blocking for the F8 fallback panels: xinput1_4 hook, DirectInput, GetAsyncKeyState (the game polls keys with
  it), window messages.
- Window focus: WM_ACTIVATEAPP(FALSE) is swallowed so the game keeps running unfocused (as in Mafia DE).
