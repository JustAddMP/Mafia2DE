# Mafia II: Definitive Edition - research notes

Everything here comes from the game's own files on this PC (read-only), looked at with our own scripts: the exe's
PE headers, imports and strings, the Steam app manifest, the game's logs of a wrapper dll the user has installed
(ReShade) and the game's SDS archives, plus what our own dll logged and what we saw in test launches (section 4).
Nothing comes from other mods. What is still unchecked in a running game is marked **UNVERIFIED**.

## 1. The exe and the install

| what | value | how found |
|---|---|---|
| Steam app id | **1030830** | `steamapps\appmanifest_1030830.acf`, `pc\steam_appid.txt` |
| Steam build | 5172789 (`buildid`), depot 1030831 | app manifest |
| install folder | `steamapps\common\Mafia II Definitive Edition` (`installdir`) | app manifest |
| exe | `pc\Mafia II Definitive Edition.exe` (in the `pc` subfolder, with spaces) | folder |
| exe size / CRC32 | 37 324 288 bytes, CRC32 `F35F0DD0` | our script (zlib crc32) |
| PE | x64 (machine 0x8664), GUI subsystem, image base 0x140000000, timestamp 0x5EE8BA60 (2020-06-16), DllCharacteristics 0x8120 (ASLR high-entropy, NX, terminal-server aware) | PE header |
| sections | `.text` 0x18263A3, `BINK`, `.rdata`, `.data`, `.pdata`, `BINKCONS`, `BINKDATA`, `_RDATA`, `.rsrc` - no packer / protector section | PE header |
| 2K launcher | removed (`2kLauncherRemoved.txt` in the install root); the old `pc\Mafia2Launcher\Launcher.exe` (.NET, Steamworks.NET) is still there but not needed | folder |

**Imports that matter** (PE import table):
- Graphics: `d3d11.dll` **D3D11CreateDevice** only, `dxgi.dll` **CreateDXGIFactory1** only: the game makes its device
  first and its swapchain from a DXGI factory. The ReShade log in the game folder (written by the user's ReShade,
  `pc\ReShade.log`) shows the calls: `D3D11CreateDevice(... FeatureLevels = 1, SDKVersion = 7)`, then
  `IDXGIFactory2::CreateSwapChainForHwnd`, swapchain `R8G8B8A8_UNORM`, `SwapEffect 3` (FLIP_SEQUENTIAL), windowed,
  `ResizeBuffers(..., 1920x1080, ..., flags 0x802)`. So: **D3D11, flip-model swapchain** (an `IDXGISwapChain1`; the
  game may present with `Present` or `Present1` - our hook covers both).
- `D3DCOMPILER_47.dll` (D3DCompile / Reflect / Disassemble): shaders compiled at run time.
- Input: `DINPUT8.dll` **DirectInput8Create**, `XINPUT1_4.dll` by ordinal **#2 / #3 / #4** (XInputGetState /
  XInputSetState / XInputGetCapabilities), user32 **GetAsyncKeyState**, GetKeyboardState, window messages
  (PeekMessage/DispatchMessage). No raw input (`RegisterRawInputDevices` / `GetRawInputData` are not imported).
  Mafia DE used xinput9_1_0; here the hook must be on **xinput1_4.dll** (done in `src/dll/ui_input.cpp`).
- Steam: `steam_api64.dll` imported directly (12 functions: SteamAPI_Init, RestartAppIfNecessary, RunCallbacks,
  RegisterCallback / CallResult, SteamInternal_CreateInterface / ContextInit, GetHSteamPipe / User, Shutdown) -
  unlike Mafia DE, which memory-loads an old steam_api. `RestartAppIfNecessary` means a start outside Steam needs
  `steam_appid.txt` next to the exe (it is there) or `SteamAppId` set (our launcher sets it).
- Files: kernel32 **CreateFileW and CreateFileA**, ReadFile, GetFileSize, GetFileAttributes(Ex)A/W,
  CreateFileMappingA / MapViewOfFile, Find*File*. No ntdll imports. `src/game/sds_redirect.cpp` hooks the CreateFile
  and attribute calls (in kernelbase).
- Also: fmod / fmodex64 (audio), bink2w64 (video), WSOCK32 + WS2_32 + IPHLPAPI (the game has its own socket code),
  dbghelp, WTSAPI32, USERENV, Secur32, D3DCOMPILER_47.

**Window**: class and title **`Mafia II: Definitive Edition`** (`RegisterClassExW` in the ReShade log; the same
UTF-16 string is in the exe). Style 0x3 (CS_HREDRAW | CS_VREDRAW).

**Single instance**: exe strings `Mafia2 Launcher Super Mutex`, `Mafia2 Installer Super Mutex`,
`... - Startup Mutex` / `... - Data Mutex` (CreateMutexA / OpenMutexA imported). Not analysed further yet (a second
instance on one PC is a later item, docs/PLAN.md); `/multiinst` (Mafia DE's switch) is not in the exe as a string.

**The user's PC has a wrapper dxgi.dll**: `pc\dxgi.dll` is ReShade 6.8 with add-ons (`*.addon64`, a "DLSS 5"
add-on that also creates a D3D12 device). Our Present hook takes the swapchain vtable from a throwaway D3D11
swapchain, which ReShade wraps too, so we hook ReShade's swapchain `Present` - the one the game calls - and draw before
ReShade's effects. Verified 2026-10-18 (`first frame via Present1`, F7 text drawn). F9 copies the backbuffer at that point (render/raw_shot.h), so screenshots show the game without ReShade's effects. The dll logs where dxgi.dll / d3d11.dll were loaded from.

**Lua**: Lua 5.1 appears to be linked into the exe (string `Lua 5.1` at file offset 0x1993B60; not looked at further).
The `pc\Lua_*DLL.dll` files next to it are **32-bit** (machine 0x14C) leftovers with `lua_pcall` etc. exported; the
x64 exe does not import them. Not used by this build.

## 2. Injection

Same as Mafia DE (core's launcher): `CreateProcess` suspended with `SteamAppId` / `SteamGameId` in the environment,
`QueueUserAPC(LoadLibraryW)` on the main thread, resume; the APC runs after the loader has mapped the exe's imports
(ReShade's dxgi.dll included) and before the exe's entry point. Fallback `CreateRemoteThread(LoadLibraryW)`
(`inject_method = thread` in Mafia2Coop.ini). Nothing Mafia-II-specific found that would stop it (no protector
section, no anti-debug imports beyond the usual). Verified 2026-10-18: `inject: dll loaded (via apc)`.

## 3. The menus: data in SDS archives, not Flash

Unlike Mafia DE (Scaleform AS3 movies), Mafia II's menus are the Illusion engine's own GUI, described by **binary
XML resources** in two SDS archives:

| archive | contents (menu part) |
|---|---|
| `pc\sds\gui\gui-main.sds` (5 812 140 bytes) | 327 textures, 118 XML: `/config/gui/Daemons/*` (MainMenu, GameMenu, Dialog, Game), `/config/gui/Screens/MainMenu/*`, `/config/gui/Screens/Dialog/*`, `/config/gui/ScreenLayouts/*` (main menu pages, also `OptionsGameMenu`) |
| `pc\sds\gui\gui.sds` (2 697 688 bytes) | `/config/gui/Screens/GameMenu/*` (the pause menu), `/config/gui/Screens/Game/*`, their layouts, `Skins/Default`, `SysGui`, `Sys2dRender`, the GUI sounds (`Sound_table/GUI_sounds/e_st_click1` ...) |

The exe builds the names from `/config/gui/Daemons/`, `/config/gui/Screens/`, `/config/gui/ScreenLayouts/` and loads
`/sds/gui/gui-main.sds` (strings at RVA 0x18520f0 / 0x1853148, `CharUniversSDS` / `IngameGuiSDS` style loaders around
RVA 0x1331f0).

**SDS v19 (PC)** (our reader/writer `src/game/sds.cpp`; checked against both archives: every resource parses, sizes
add up exactly):
- header 0x48 bytes: `SDS\0`, u32 19, `PC\0\0`, u32 FNV-1 32 of bytes 0..11; @0x10 u32 type-table offset (0x48),
  block-stream offset, XML-index offset; four u32 memory sums (slot RAM, slot VRAM, other RAM, other VRAM - the sums of
  the resources' own fields); u32 1; 16 zero bytes; @0x40 u32 resource count; @0x44 u32 **FNV-1 32 of bytes
  0x10..0x43** (found by trying ranges; both archives match).
- type table: u32 count, { u32 id, u32 length, name, u32 parent }.
- block stream: `UEzl`, u32 0x4000, u8 4, then blocks { u32 size, u8 compressed, data }, end u32 0 + u8 0.
  Compressed blocks: 32-byte header (u32 uncompressed size, u32 0x20, ...) + a zlib stream. **Raw blocks of 0x4000
  bytes also occur in the game's own gui.sds** (9 of 586), which is why our writer uses raw blocks only.
- the concatenated stream: per resource u32 type, u32 size (incl. this 26-byte header), u16 version, 4 x u32 memory,
  data. XML resources: version 3, memory 0.
- XML index (plain text) at the end: one `<ResourceInfo>` per resource (TypeName, SourceDataDescription, memory) in
  stream order. Unchanged by us.
- `pc\sds\sdsconfig.bin` (`psds` + NOT-obfuscated names) lists archive names only, no sizes or hashes.

**Binary XML** (our reader/writer `src/game/bxml.cpp`; all 167 binary XML resources of both archives read back to the
same tree, and re-written they are byte-identical except alignment padding, where the game leaves junk and we write
zeros):
- prefix: u32 (unknown: not FNV / CRC of the name or the data we tried; kept as is - **we never create a resource**),
  u32 length + `UnknownTargetSystem`, u8 1, u32 length + resource name, u8 0 (binary).
- u8 version 4, u32 node count, u32 string-table size, string table, node table.
- string entries { u32 type, u32 0, NUL-terminated text }: type 4 for element / attribute names and attribute values,
  type 1 for element text (4-byte aligned in the table). Strings are not de-duplicated; order = per node: name,
  attributes (name, value), text, then the children.
- nodes { name, text (0 = none), id, child count, child ids, attribute count, (name, value) pairs } in id order;
  ids are depth-first pre-order; node 0 is an unnamed root.

**How a menu works** (from the decoded data):
- A *daemon* (`/config/gui/Daemons/MainMenu`, `.../GameMenu`) lists its screens: `<screen name="Extras"
  source="MainMenu/Extras"/>` -> `/config/gui/Screens/MainMenu/Extras`.
- A *screen* has `<settings>` (`<definition>\sExtras</definition>` = its layout `/config/gui/ScreenLayouts/Extras`,
  fades, `<focus>`, `<remember-focus>`), `<events>` (`cancel`, `show`, ...) and per-widget events: `use`,
  `cursor-over`, `activate`, `select` with an `action`: `screen.activate` (operation `replace` / `set`, text = the
  screen name), `screen.back`, `screen.deactivate`, `container.selectItem`, `container.notifySelectedItem`,
  `menu.visibility` (`\u<id>`: hide by a game condition), `play.sound`, `game.*`, `options.*`, ... about 130 action
  names, all handled in C++ (static initialisers hash the names with FNV-1 64, e.g. RVA 0x1c230 for `screen.activate`,
  hash function RVA 0xa76940, which also parses decimal numbers). Values carry type prefixes: `\s` string, `\u`
  unsigned, `\f` float.
- A *layout* is a widget tree (`layout`, `groupbox`, `image`, `listbox` with `<items>` of `button`s, `statictext`,
  `dockpanel` footers with the A/B glyph images and BACK / SELECT buttons), each with skin (`button.buton`), font
  (`CREAMPUF`, size 0.05, `capital="true"`), colours, position / size in screen fractions (rows of 0.062), and
  `<text text="..." textid="59010003"/>`: a string id from the game's text tables, or literal text with `textid="0"`
  (220 widgets in the game's own layouts do that, e.g. "Accept All", "Warning").

**The CO-OP entries** (`src/game/menu_patch.cpp`; reworked after the first test launch, see section 4 for why):
- main menu: the hidden debug entry `btnLoadMission` ("DEBUG Load Chapter", right after `btnStory` in
  `listboxMainMenu`, hidden by `menu.visibility \u145` on `activate` and `refresh`) gets the literal label CO-OP
  (`text="CO-OP" textid="0"`) and the visibility condition `\u100` (the one EXIT uses; shown on PC). Its `use`
  stays `screen.activate replace LoadMission`; the screen `LoadMission` (only reachable from that entry) is the
  CO-OP page. The list re-flows visible entries, so CO-OP sits right under THE STORY.
- pause menu: the hidden cheats entry `btnCheats` (right after `btnOptions` in `listboxGameMenu`, hidden by
  `\u130`) likewise; its screen `Cheats` is the CO-OP page.
- the pages: built into those debug screens and their layouts (`Screens/MainMenu/LoadMission` +
  `ScreenLayouts/LoadMission`, `Screens/GameMenu/Cheats` + `ScreenLayouts/Cheats`) from the EXTRAS page
  (`Screens/MainMenu/Extras` + `ScreenLayouts/Extras`) and the pause OPTIONS page (`Screens/GameMenu/Options` +
  `ScreenLayouts/OptionsGameMenu`): same frame, header, list style and footer; the title says CO-OP, the list holds
  HOST CO-OP GAME, FREE RIDE, JOIN FRIEND, JOIN BY IP, LOBBY, SETTINGS (literal text; each button a copy of the
  template list's first button, one row apart), `cancel` and the footer's BACK do
  `screen.back`. The host layout keeps its root widget name. Daemons are not touched.
- sizes: every patched resource is padded back to its exact original size (section 4).
- delivery: the dll hooks CreateFileW/A and GetFileAttributes(Ex)A/W; when the game opens an existing
  `...\sds\gui\gui-main.sds` or `...\sds\gui\gui.sds` it gets `<dll dir>\cache\<pid>-gui-main.sds` / `<pid>-gui.sds`,
  built at that moment from the game's file (read, inflate, patch, write raw blocks, read back as a check). The game
  folder is never written. Reports with the patched trees: `cache\<pid>-gui-main.sds.patch.txt`.

## 4. What the test launches showed (2026-10-18)

Test log: [test-2026-10-18-menu-launch.md](test-2026-10-18-menu-launch.md).

**Archive search order.** For every archive the game tries, in this order, and takes the first that exists
(CreateFileA, full paths, each path a few times):
`Documents\My Games\Mafia II Definitive Edition\Data\sds\<dir>\<name>.sds`, `<install>\EDIT\sds\...`,
`<install>\pc\sds_en\...` (language folder), `<install>\pc\sds\...`. So the game has override folders (we do not
use them: the Documents one holds the player's profile and saves, the install folder is read-only for us). The
first build tried to patch the first (missing) path and gave up; now only an existing file is redirected. Startup
order of the gui archives: `gui.sds`, `gui_definitive_edition.sds`, `gui-main.sds`.

**The archive check.** The game reads the whole archive and then drops it (no menus at all, black screen after the
logos, no crash, no log) if a binary XML resource is not as expected. Found by bisecting in test launches (each
change alone; "loads" = the title screen appears and the game reads the archive to its end):

| change | result |
|---|---|
| copy: archive rewritten with raw blocks, every resource unchanged | loads |
| every patched XML re-written by our bxml writer, trees unchanged | loads |
| one attribute value changed, same length (`0.0620` -> `0.0621`) | loads |
| two attributes of a node swapped (same bytes, other order) | loads |
| a label made longer, another made shorter by the same bytes (resource size unchanged) | loads |
| a value made longer (`0.0620` -> `0.06200000`), a label made longer, an attribute added (any size change) | **dropped** |
| an unused resource at the very end of the archive made longer | **dropped** |
| only the resource's first prefix word changed (`u32` before `UnknownTargetSystem`) | **dropped** |

So the prefix word is checked against something that depends on the resource's size (content changes of the same
size pass). It is not a hash of the size alone: identical bodies under different names in gui.sds and gui-main.sds
have different words, while `Screens/Dialog/2KSimpleDialog` and `TutorialDialog` (same size, 8 bytes differ twice)
share one. Not worked out; we keep every resource's size and prefix word instead. Node and attribute counts may
change (the CO-OP pages have other trees than the screens they replace, and load).

**Padding that is safe** (all seen working in the main menu and its CO-OP page):
- numbers written with more or fewer trailing zeros (`0.0000` <-> `0.000000`), one byte per zero; layouts have no
  4-aligned element texts, so every size can be reached;
- invisible empty group boxes (`commondata visible="false"`, position / size 0) in a layout's children, for large
  gaps (the main CO-OP page needed 15 for 10 KB);
- in screens: `<hide action="menu.removeVariable" name="CoopPadN..."/>` on the last widget's events (nothing follows
  in the string table, so the name's length takes up the last bytes exactly);
- shrinking a screen: dropping `remember-focus` and the fades' `type="blend"` (the main CO-OP page, hosted in the
  3142-byte LoadMission screen, needed both with five entries); with six entries also dropping the fade elements
  (they are 0 s long, `\f0`, as on EXTRAS; the game's own Garage, PhoneMenu and Question screens have none -
  UNVERIFIED in game);
- when a screen ends up a few bytes short and too tight for one more padding event: longer (invisible) entry
  widget names in the screen and the layout (`btnCoop0xx`, ...; the main CO-OP page needs 7 characters).

**Not padding:** a label literal. A non-empty `text="..."` is drawn **instead of** the `textid` string (padding
MAIN MENU's literal with spaces made the title blank), so `text` only stays empty where `textid` is used.

**Rendering:** literal labels (`textid="0"`) are drawn in the game's font with its capitals; the main menu list
re-flows its visible entries (the debug entry, once visible, takes the row after THE STORY and the rest move down);
the game draws its own software cursor and moves it from relative mouse input.

**Still UNVERIFIED:** the pause menu entry and page (needs a loaded save), pad navigation and glyphs, the menu
sounds (the game was muted; the page uses the same widgets as EXTRAS).

## 5. Free Ride: what the game has for an open city without the story (static, 2026-10-19)

Mafia II has no free-roam mode in its menus (Free Ride exists in Mafia DE and, as a debug mission, in Joe's
Adventures). Looked at statically, read-only: the exe's strings, the SDS archives' resource indexes (all 4530
archives under `pc\sds` and `pc\dlcs`, our scratch scripts), the GUI binary XML, the stream-map and game files
in `EDIT\`, the DLC `content` descriptors, and the Lua scripts (disassembled with our own Lua 5.1 bytecode lister,
written from the public Lua 5.1 format). Nothing here was tried in game: all of it is **UNVERIFIED** in a running
game.

**Lua in the game.** Mission scripts are `Script` resources in `pc\sds\missionscript\*.sds` (and `script\`):
a name prefix (`/scripts/missions/<Name>/`) then one or more Lua 5.1 chunks (`\x1bLuaQ`), each with its file name
(`/scripts/missions/FreerideMain/FreerideScript.lua`). The chunks carry constants and function line ranges but
no per-instruction lines. Header: little endian, int 4, size_t 4, instruction 4, **lua_Number 4 bytes, float**
(not the stock double): our own chunks for the game's VM must be compiled for that format. Each mission archive
also has a `MemFile /missions/<Name>/scriptname.txt`, a `FrameResource`, `FrameNameTable` and `Actors` (the
mission's own entities). Script entry points are globals `onGameInit` / `onEvent` (exe strings `onGameDone`,
`onEvent`, `ScriptMachine`).

**1. A free-ride mission is still in the base game.**
- `pc\sds\missionscript\freeraid_main.sds` (6929 bytes): script `/missions/FreerideMain`
  (`FreerideScript.lua`). Its start function `FreeRideStart2` (a `CommandBuffer` sequence):
  - `game.sds:ActivateStreamMapLine("free_summer_load")` if `game.datastore:GetNumber("missionPart") < 4`, else
    `"free_winter_load"`;
  - `game.gfx:SetWeatherTemplate(...)` by part: 1 `DTFreeRideDay`, 2 `DTFreeRideDayRain`, 3 `DTFreerideNight`
    (summer), 4 `DTFreeRideDayWinter`, 5 `DTFreeRideDaySnow`, 6 `DTFreeRideNightSnow` (winter);
  - `game.game:SaveGame(1)` and waits for it (**it saves - may overwrite a story save; back up saves before any
    test**);
  - `game.game:GetActivePlayer()`, `player:SetPos` to (69.63, 909.08, -19.63); about 70 `game.garage:CheatAddCar(
    "<model>", true)` calls (every car model, Ascot_BaileyS200_pha ... Moscardo_AS37_pha) - fills the garage;
    `ActivateStreamMapLine("free_joe_load")`, waits for `game.sds:GetSyncObjectForLoadSDS("italy")`;
  - `game.traffic:OpenSeason(140)` (summer) / `(50)` (winter), `game.speech:LoadStage(...)` the same numbers,
    `game.radio:SetContent("Empire" / "Classic" / "Delta", "all", "<station>_07010" or "_02004")`;
  - test content: entities `Joe`, `Henry` (wingmen to recruit by an action, with `ChangePlayerModel` suit
    changes VITSUIT / VITTUX / VITPRA), `Hunter` and `Escaper` (two AI cars, `CarAIProfile.PIRATE`, a chase);
    `ai.relationships:SetRelationship` player <-> Clemente gang;
  - `player:SetControlStyle(enums.ControlStyle.FREE)`, `game.game:SoundFadeIn`, `game.hud:FaderFadeIn` - the
    player gets control.
- It is listed as a real mission in the debug mission list `/config/gui/Missions` (gui-main.sds):
  `<mission name="freeride" number="0" title="FREE RIDE">` with parts `freeride1`..`freeride6` "Summer Day",
  "Summer Day Rain", "Summer Night", "Winter Day", "Winter Day Snow", "Winter Night" (no `gameLine`: the story's).
  That list is shown by the hidden "DEBUG Load Chapter" entry's screen `LoadMission`
  (`menu.mission.filllist saveId 1..4`) and its `MissionParts` screen (`menu.mission.fillpartlist`) - the entry and
  screen our main menu CO-OP entry and page now use, so the debug loader is gone from our patched menu.
- The base stream map `EDIT\tables\StreamMapa.bin` (`StrM`, version 6; named file groups such as `Director_Lua-1`,
  `City_Ground-1`, then games with named lines) has a game **"Free Raid"** with the lines `free_area_state`,
  `freeraid_main`, `free_summer_load` / `summer_release`, `free_winter_load` / `winter_release`, `free_joe_load`,
  `generic_quest1`..`4`, `free_ride_joe_end`; it lists `/sds/missionscript/freeraid_main.sds` and
  `/sds/skies/freeride.sds`. The format is not decoded further (which archives each line loads is open).
- Its sky archive `pc\sds\skies\freeride.sds` has the day presets `DTFreeRideDay`, `...DayRain`, `...DaySnow`,
  `...DayWinter`, `DTFreerideNight`, `DTFreeRideNightSnow`, the time preset `TTFreeRide` and the weather presets
  `WTFreeRide*`.
- The exe knows a start operation **`FreeRide`**: `game.start` operations are `NewGame`, `Continue`, `QuickFight`,
  `QuickRace`, `FreeRide`, `Retry`, `Load` (strings at 0x18d6668..). No menu uses `FreeRide`, `QuickFight` or
  `QuickRace`; what `FreeRide` does is open.

**2. Joe's Adventures.** The DE ships every DLC with the base game: `pc\dlcs\cnt_joes_adventures` (and
`cnt_jimmys_vendetta`, `cnt_betrayal_of_jimmy`, the car / suit packs) are in the base depot 1030831 (the only
installed depot in the app manifest); the base `pc\sds` has its menus (`gui\gui-main_dlc_ja.sds`,
`ingame_gui_ja.sds`), its skies (`skies\ja_sky.sds`) and 156-byte empty placeholder archives for its files
(`missionscript\dlc_*.sds`, `city\dlc*.sds`).
- `pc\dlcs\cnt_joes_adventures\content` (XML): package "Joe's Adventures" (Id 96); mounts `<dlc>\sds` (and
  `sds_<lang>`) over `/sds` for 3095 listed files on trigger `GameLine_6` (so the DLC's real archives replace the
  placeholders while game line 6 runs); content `MISSION_PACK_V2` with `MissionPackId 6`, `StreamMapFile
  /tables/M2DLC_Joes_Adventures_sm.bin` (in `game_edit\tables`), `StreamMapLine game_main_load`,
  `FirstMission 61`, `FirstPart 1`, `MissionCount 4`, `OpenString CITY_TRICK`, `MenuScreen
  DLC_JoesAdventure_Loading`, `GuiSDS /sds/gui/gui-main_dlc_ja`, `IngameGuiSDS /sds/gui/ingame_gui_ja.sds`,
  `CharUniversSDS /sds/player/joeh.sds`, `MapScreen MapJA`; player content `JOECIV2`.
- Start: the DLC screen `DLC_JoesAdventure` -> difficulty -> `game.start operation="NewGame" saveId="\u6"`
  (Continue: `operation="Continue" saveId="\u6"`). The open city between missions is run by a "game director"
  script in the stream map's `Director_Lua-1` slot (`/sds/missionscript/dlc_game_director_2.sds`; the base game's
  `game_director.sds` holds `GameDirector.lua`, `CityQuestManager.lua`, `ListOfQuests.lua`, `RoadblockManager.lua`,
  `TempQuestStarter.lua`: city quests, roadblocks, `LoadQuestSds`, `ActivateStreamMapLine`).
- Its mission list `/config/gui/dlcs/JoesAdventure/MissionsJA` (gui-main_dlc_ja.sds in `sds_<lang>`, loaded by
  `menu.AddMissionList` on `DLC_JoesAdventure_Loading`): `freeride` number 60 "JA - FREE RIDE" (parts
  `freeridea`..`f`, the same six weathers), `m01` 61 WITNESS (10 parts), `m02` 62 CONNECTION, `m03` 63
  SUPERMARKET, `m04` 64 CATHOUSE, `m05` 65 CONSTRUCTION, `m06` 66 CITY QUESTS, `testscene` 80; all `gameLine 6`.
  Shown only by JA's own hidden debug entry (`btnLoadMission`, `\u145`, screen `DLC_JA_LoadMission`,
  `menu.mission.filllist saveId 6`).
- JA's free ride script: `/missions/DLC_FreerideMain` in `dlcs\cnt_joes_adventures\sds\missionscript\
  dlc_freeraid_main.sds` (mounted by `GameLine_6`), the same as the base one without the Hunter / Escaper chase,
  with `ChangePlayerModel("JOEL1")`; the JA stream map has a `freeride_group` (`DLC_free_ride_start`,
  `free_summer_load` / `_end`, `free_winter_load` / `_end`, `DLC_free_ride_end`).
- The base gui-main.sds still has an older, unreferenced `Screens/MainMenu/JoeAdventure` (layout `MafiaArcade`,
  not in the daemon): `game.start NewGame \u3`, and `game.mission.load part="\u1" game="\sFirstCity"
  gameLine="\u3"` with the value `\u2` - the only use of `game.mission.load` in the menus, and the base mission
  list's old JA block (`gameLine 3`, missions 61..70, its `freeride` number 60). Save ids seen: 1 story, 2 Jimmy's
  Vendetta, 3 old JA, 4 Betrayal of Jimmy, 6 Joe's Adventures, 98 benchmark.

**3. A chapter's city without its story scripts.** Each chapter's script is its own archive
(`missionscript\m02_main.sds`, `m03_agnelo.sds`, ...) that the stream map loads by line (`02_main_load`,
`02_part_N_load`, ...), and the engine accepts empty 156-byte archives in place of real ones (the DLC
placeholders). So our archive redirect (re-notes 3, `sds_redirect.cpp`) could hand the game an empty archive
for a chapter's mission script. But the script is what unlocks the player: the free ride script shows what a
mission script must do itself - stream lines for the season, weather template, traffic season, speech stage,
radio, `SetControlStyle(FREE)`, sound and fader fade-in. Without it the game would most likely stay black /
locked, and chapter state (city parts, shops, the `CityParts - Open` style lists in the exe, police) belongs to
the chapter's own lines. Possible, but every chapter is a different case; the free ride mission is the clean base.

**4. World, time of day, weather** (exe strings and the free ride script):
- The game: `/games/FirstCity.bin` (`EDIT\games`, `rpmg` v6: `/sds/City/`, `/sds/Shops/`, `/sds/Traffic/`,
  `/sds/Cars/`, fog `fog_sky_den`, open string `CITY_trick`, `/missions/CITY/actors_player.bin`); the exe's
  `FirstCity`, `/tables/StreamMapa.bin`, `StreamMapFile` / `StreamMapLine`, the lines `game_main_load`,
  `carcyclopedia_load`, `fr_director_load` (Jimmy's Vendetta), `ja_director_load`, triggers `GameLine_%d` /
  `GameLine_%d_%s`, datastore keys `missionNumber`, `missionPart`.
- Weather: a *day* preset (`/weatherPresets/day/DT...`) names a weather preset (`DayWeatherTemplate`, `WT...`),
  a time preset (`DayTimeTemplate`, `TT...`: sun / moon curves), a saved time and a winter flag; scripts pick one
  with `game.gfx:SetWeatherTemplate("DT...")`. Per-chapter sky archives `pc\sds\skies\01_01_sicily.sds` ..
  `14_02_construction.sds`, `freeride.sds`, `ja_sky.sds`, `rtr_sky.sds`. Exe script functions nearby:
  `SetWeatherDateTime`, `GetDayTime`, `IsTheWeatherLike`, `SetTime`.
- Seasons: summer / winter are separate city data (`free_summer_load` / `free_winter_load`,
  `ground_leto.sds` / `ground_zima.sds`, `/tables/WinterCity.txt`), chosen when the city loads, not switched live.
- City life: `game.traffic` (`OpenSeason`, `CloseSeason`, `SetPolice`, `DespawnPolice`, `SwitchRoad`,
  `SetNumCars`, `SetNumStaticCars`, `Populate`, `PoliceReinforcements`, `SwitchGenerators`), `game.sds`
  (`ActivateStreamMapLine`, `LoadCityPart` / `ReleaseCityPart`, `LoadCityShop` / `ReleaseCityShop`), `game.radio`,
  `game.speech`.

**Most promising approach** (docs/PLAN.md item D-FR): start the base game's FREE RIDE mission (mission 0, parts
1-6) the way the game's own debug loader does, from our FREE RIDE entry with the game's `game.mission.load`
menu action (data only), then replace its script with our own (no test NPCs and cars, no save, our start
position) and run co-op on it. Open: the value `game.mission.load` takes, the right `gameLine` for the base
game, whether the debug mission still loads in the DE, what its `SaveGame(1)` overwrites, what
`game.start operation="FreeRide"` does, and how the stream map's line format maps lines to archives.
