# Test launch 2026-10-18: CO-OP in the game's own menus

One instance, `out\Mafia2Coop.exe --log test`, game window 1280x720 windowed, muted, the user's ReShade
(`pc\dxgi.dll`, ReShade 6.8 with add-ons) in place. Main menu only: no New Game, no save loaded, so the pause menu
was not reached. The profile/saves folder (`Documents\My Games\Mafia II Definitive Edition`) was backed up before
and restored afterwards; sha256 of all 22 files identical to before. Screenshots (game window only) are in
`out\test-shots\2026-10-18\` (not in the repo: they show the game's art).

## Result

| check | result |
|---|---|
| launcher finds the game, injects by APC | OK: `inject: dll loaded (via apc)`, `game running (pid N); launcher done` |
| dll init, Present hook through ReShade | OK: `init: done`, `render: first frame via Present1: swapchain ... 1280x720 fmt=28 buffers=2 effect=3 windowed=1`; F7 status text drawn over the menu (`11-f7-overlay-through-reshade.png`) |
| `sds: game opens ...\sds\gui\gui-main.sds` | OK, after a search order (see below) |
| CO-OP entry added / archives accepted | first build: **no** (two bugs, fixed in `c81b32b`); fixed build: OK |
| CO-OP under THE STORY, same font / colour / highlight | OK (`03-main-menu-coop.png`, `04-main-menu-coop-selected.png`) |
| arrow keys and mouse hover select it | OK (`08-main-menu-mouse-hover.png`) |
| Enter / click open the CO-OP page | OK: EXTRAS look, title CO-OP, HOST CO-OP GAME, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS, BACK footer (`05-coop-page.png`, side by side with EXTRAS: `12-coop-page-next-to-extras.png`) |
| navigation on the page | OK: keys and mouse move the highlight (`07-coop-page-keyboard-nav.png`, `09-coop-page-mouse-hover.png`); Enter on an entry does nothing (expected) |
| back | OK: Esc and a click on the footer's BACK return to the main menu, CO-OP still selected (`10-back-click-returns.png`) |
| EXTRAS still works | OK (`06-extras-same-build.png`, same as without the mod: `02-vanilla-extras.png`) |
| pad navigation, pad glyphs, menu sounds | not checked (no controller; the game was muted) |
| pause menu entry and page | not checked (needs a loaded save) |
| F8 overlay fallback | not needed |

## What went wrong first, and the fixes

1. **The wrong path was patched.** The game looks for every archive in
   `Documents\My Games\Mafia II Definitive Edition\Data\sds\...`, `<install>\EDIT\sds\...`, `<install>\pc\sds_en\...`
   and `<install>\pc\sds\...` and takes the first that exists. The redirect tried to patch the first (missing) path,
   logged `menu: cannot read ...Documents\...\gui.sds (3); menus unchanged` and never tried again. Fix: only an
   existing file is patched and redirected. Log after the fix:
   `sds: G:\SteamLibrary\...\pc\sds\gui\gui-main.sds -> G:\JustAddMP\mafia2-de\out\cache\<pid>-gui-main.sds`.
2. **The game dropped the patched archive** (black screen after the logos, no title screen, no crash). Bisected
   with temporary switches in the dll (removed again) and a ReadFile trace (the game read the bad archive only up
   to the end of its block stream, a good one to the end of the file): a rewritten archive with unchanged
   resources loads, same-size edits load, any change of a resource's size - or of its first prefix word - makes
   the game drop the archive (table in re-notes.md 4). Fix (`menu_patch.cpp`): nothing is added any more; the
   CO-OP entries are the menus' hidden debug entries, the pages are built into the debug screens they open, and
   every patched resource is padded back to its exact size.
3. **Label literals are drawn.** The first padding used the empty `text=""` of labels that take their text from a
   `textid`; the main menu's title then showed blanks instead of MAIN MENU (`00b-label-padding-hid-title.png`). A
   non-empty `text` wins over `textid`. Fix: layouts are padded with trailing zeros on numbers and invisible filler
   group boxes instead.

## Log (fixed build, last run)

`out\logs\launcher-test.log`:
```
steam: manifest G:\SteamLibrary\steamapps\appmanifest_1030830.acf -> G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\pc\Mafia II Definitive Edition.exe
version: exe CRC32 F35F0DD0 -> Steam build 5172789
started pid 15852 (suspended)
inject: method apc, dll G:\JustAddMP\mafia2-de\out\mafia2coop.dll
inject: dll loaded (via apc)
game running (pid 15852); launcher done
```

`out\logs\mafia2coop-test.log` (excerpt):
```
module dxgi.dll: G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\pc\dxgi.dll  (not the system dll: a wrapper / mod, or the game's own copy)
menu: archive redirect installed (CreateFileW 1, CreateFileA 1, attribute queries 1), cache G:\JustAddMP\mafia2-de\out\cache
init: game window ... class 'Mafia II: Definitive Edition' title 'Mafia II: Definitive Edition'
ui_input: installed (DirectInput ok, XInput ok via xinput1_4.dll, GetAsyncKeyState ok)
init: done; press F8 in the game for the CO-OP panel
render: first frame via Present1: swapchain 000000004F150A80 1280x720 fmt=28 buffers=2 effect=3 windowed=1 hwnd=... 'Mafia II: Definitive Edition'
sds: game opens C:\Users\...\Documents\My Games\Mafia II Definitive Edition\Data\sds\gui\gui.sds
sds: game opens G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\EDIT\sds\gui\gui.sds
sds: game opens G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\pc\sds_en\gui\gui.sds
sds: game opens G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\pc\sds\gui\gui.sds
menu: gui.sds: entry CO-OP: btnCheats in listboxGameMenu (1 visibility condition(s) -> always shown)
menu: gui.sds: /config/gui/ScreenLayouts/Cheats: 55 invisible filler(s) to keep its size
menu: gui.sds: page built in /config/gui/Screens/GameMenu/Cheats / /config/gui/ScreenLayouts/Cheats (5 entries, list listboxOptions)
menu: gui.sds patched in 235 ms: 2697688 -> 9628670 bytes, 88 resources -> G:\JustAddMP\mafia2-de\out\cache\15852-gui.sds
sds: G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\pc\sds\gui\gui.sds -> G:\JustAddMP\mafia2-de\out\cache\15852-gui.sds
sds: game opens ...\pc\sds\gui\gui_definitive_edition.sds
sds: game opens ...\pc\sds\gui\gui-main.sds
menu: gui-main.sds: entry CO-OP: btnLoadMission in listboxMainMenu (2 visibility condition(s) -> always shown)
menu: gui-main.sds: /config/gui/Screens/MainMenu/LoadMission: no remember-focus (size)
menu: gui-main.sds: /config/gui/Screens/MainMenu/LoadMission: plain fades (size)
menu: gui-main.sds: /config/gui/ScreenLayouts/LoadMission: 15 invisible filler(s) to keep its size
menu: gui-main.sds: page built in /config/gui/Screens/MainMenu/LoadMission / /config/gui/ScreenLayouts/LoadMission (5 entries, list listboxExtras)
menu: gui-main.sds patched in 125 ms: 5812140 -> 14428432 bytes, 446 resources -> G:\JustAddMP\mafia2-de\out\cache\15852-gui-main.sds
sds: G:\SteamLibrary\steamapps\common\Mafia II Definitive Edition\pc\sds\gui\gui-main.sds -> G:\JustAddMP\mafia2-de\out\cache\15852-gui-main.sds
heartbeat: uptime 60 s, frames ..., fps ~293, backbuffer 1280x720, panel closed
```

## Notes for the next test

- The pause menu: load a save (with the saves backed up), Esc, look for CO-OP under OPTIONS, open it, compare with the
  pause OPTIONS page, BACK.
- A pad: d-pad / stick on both pages, A / B, the footer glyphs.
- Sound: unmuted, the menu's move / select sounds on the CO-OP entry and page.
- The game needs the window focus once at start (it stays on a black screen until then, also without the mod).
- The game draws its own cursor and moves it from relative mouse input: absolute cursor jumps (SetCursorPos) do not
  place it; tests must send relative moves.
