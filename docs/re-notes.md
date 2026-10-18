# Mafia II: Definitive Edition - static notes

Everything here comes from the game's own files on this PC (read-only), looked at with our own scripts: the exe's
PE headers, imports and strings, the Steam app manifest, the game's logs of a wrapper dll the user has installed
(ReShade) and the game's SDS archives. Nothing comes from other mods. Nothing here has been checked in a running
game by us yet; what the menu work depends on is marked **UNVERIFIED** where it matters.

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
ReShade's effects. **UNVERIFIED** in game. The dll logs where dxgi.dll / d3d11.dll were loaded from.

**Lua**: Lua 5.1 appears to be linked into the exe (string `Lua 5.1` at file offset 0x1993B60; not looked at further).
The `pc\Lua_*DLL.dll` files next to it are **32-bit** (machine 0x14C) leftovers with `lua_pcall` etc. exported; the
x64 exe does not import them. Not used by this build.

## 2. Injection

Same as Mafia DE (core's launcher): `CreateProcess` suspended with `SteamAppId` / `SteamGameId` in the environment,
`QueueUserAPC(LoadLibraryW)` on the main thread, resume; the APC runs after the loader has mapped the exe's imports
(ReShade's dxgi.dll included) and before the exe's entry point. Fallback `CreateRemoteThread(LoadLibraryW)`
(`inject_method = thread` in Mafia2Coop.ini). Nothing Mafia-II-specific found that would stop it (no protector
section, no anti-debug imports beyond the usual). **UNVERIFIED** in game.

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

**The CO-OP entries** (`src/game/menu_patch.cpp`):
- main menu: `btnCoop` cloned from `btnStory` in `listboxMainMenu` (layout `MainMenu`), inserted after it, the rows
  below move down 0.062; its screen events: `use` -> `screen.activate replace Coop`, `cursor-over` ->
  `container.selectItem`. Daemon MainMenu: `<screen name="Coop" source="MainMenu/MafiaLogo"/>`.
- pause menu: `btnCoop` cloned from `btnOptions` in `listboxGameMenu` (layout `GameMenu`), same events. Daemon
  GameMenu (in gui-main.sds): `<screen name="Coop" source="GameMenu/Cheats"/>`.
- the pages: built into screen/layout pairs the retail game never opens, so no resource has to be added:
  `Screens/MainMenu/MafiaLogo` + `ScreenLayouts/MafiaLogo` (not in the MainMenu daemon's list; nothing activates
  it) for the main menu, `Screens/GameMenu/Cheats` + `ScreenLayouts/Cheats` (the pause menu's `btnCheats` is hidden
  by `menu.visibility \u130`) for the pause menu. Templates: the EXTRAS page (`Screens/MainMenu/Extras` +
  `ScreenLayouts/Extras`) and the pause OPTIONS page (`Screens/GameMenu/Options` + `ScreenLayouts/OptionsGameMenu`):
  same frame, header, list style and footer; the title text says CO-OP, the list holds HOST CO-OP GAME, JOIN FRIEND,
  JOIN BY IP, LOBBY, SETTINGS (literal text), `cancel` and the footer's BACK do `screen.back`.
- delivery: the dll hooks CreateFileW/A and GetFileAttributes(Ex)A/W; when the game opens `...\sds\gui\gui-main.sds`
  or `...\sds\gui\gui.sds` it gets `<dll dir>\cache\<pid>-gui-main.sds` / `<pid>-gui.sds`, built at that moment
  from the game's file (read, inflate, patch, write raw blocks, read back as a check). The game folder is never
  written. Reports with the patched trees: `cache\<pid>-gui-main.sds.patch.txt`.

**UNVERIFIED** (all of it needs the test launch):
- that the game opens the two archives through kernel32 CreateFileW/A by path (the log lists every `.sds` open);
- that it accepts raw blocks everywhere in an archive and a different file size (it does use raw blocks itself);
- that the screen loader resolves `source="MainMenu/MafiaLogo"` / `GameMenu/Cheats` under a new screen name, and that
  `screen.activate replace Coop` reaches it;
- that `textid="0"` shows our literal text with the game's font and capitals;
- how the list lays out rows (explicit positions vs. re-flow of visible items) - we keep explicit positions sensible;
- that the cloned pages look exactly like EXTRAS / OPTIONS (they are the same widgets, so they should);
- whether gui-main.sds is loaded during play (the GameMenu daemon is in it, so it should be).
