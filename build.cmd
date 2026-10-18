@echo off
rem Mafia2Coop build (menu-only build, self-contained: no JustAddMP core submodule yet).
rem Usage: build.cmd   (runnable from any directory; builds only, runs nothing)
rem Output in out\: Mafia2Coop.exe (launcher), mafia2coop.dll (the mod), the unit tests (tests\*.cpp ->
rem out\<name>.exe), PDBs next to them; objects and libraries in out\obj\.
rem Tests: build.cmd test   (builds, then runs every test exe; exit code 0 = all passed)
setlocal EnableExtensions EnableDelayedExpansion
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "OUT=%ROOT%\out"
set "OBJ=%OUT%\obj"
set "GEN=%OBJ%\gen"

call "%ROOT%\tools\msvc.cmd" || goto :fail

for %%d in (gen minhook core render dll game launcher tests) do if not exist "%OBJ%\%%d" mkdir "%OBJ%\%%d"
set "INCS=/I"%ROOT%\src" /I"%GEN%" /I"%ROOT%\third_party\minhook\include""
set "CXXFLAGS=%MC_CXXFLAGS% %INCS%"

rem ---- minhook (C, vendored, BSD-2) -------------------------------------------
echo [build] minhook
del /q "%OBJ%\minhook\*.obj" 2>nul
cl %MC_CFLAGS% /Fd"%OBJ%\minhook\vc.pdb" /Fo"%OBJ%\minhook\\" ^
  "%ROOT%\third_party\minhook\src\buffer.c" "%ROOT%\third_party\minhook\src\hook.c" ^
  "%ROOT%\third_party\minhook\src\trampoline.c" "%ROOT%\third_party\minhook\src\hde\hde64.c" >"%OBJ%\minhook\cl.log"
if errorlevel 1 ( type "%OBJ%\minhook\cl.log" & echo [build] ERROR: minhook failed & goto :fail )

rem ---- shaders: src\render\overlay.hlsl -> gen\overlay_vs.h / overlay_ps.h (fxc from the Windows SDK) ----------
where fxc.exe >nul 2>nul || ( echo [build] ERROR: fxc.exe not on PATH ^(Windows SDK^) & goto :fail )
echo [build] shader overlay.hlsl
fxc /nologo /T vs_4_0 /E VSMain /O3 /Vn kOverlayVS /Fh "%GEN%\overlay_vs.h" "%ROOT%\src\render\overlay.hlsl" >"%GEN%\overlay_vs.log" 2>&1
if errorlevel 1 ( type "%GEN%\overlay_vs.log" & echo [build] ERROR: shader VS failed & goto :fail )
fxc /nologo /T ps_4_0 /E PSMain /O3 /Vn kOverlayPS /Fh "%GEN%\overlay_ps.h" "%ROOT%\src\render\overlay.hlsl" >"%GEN%\overlay_ps.log" 2>&1
if errorlevel 1 ( type "%GEN%\overlay_ps.log" & echo [build] ERROR: shader PS failed & goto :fail )

rem ---- sources (every .cpp in each dir) ----------------------------------------
for %%d in (core render dll game launcher) do (
  del /q "%OBJ%\%%d\*.obj" 2>nul
  echo [build] src\%%d
  cl %CXXFLAGS% /Fd"%OBJ%\%%d\vc.pdb" /Fo"%OBJ%\%%d\\" "%ROOT%\src\%%d\*.cpp"
  if errorlevel 1 ( echo [build] ERROR: compiling src\%%d failed & goto :fail )
)

rem m2_ui.lib: everything of the dll except DllMain and the game module (the tests link it with their own module)
echo [build] lib m2_ui.lib
lib /NOLOGO /OUT:"%OBJ%\m2_ui.lib" "%OBJ%\minhook\*.obj" "%OBJ%\core\*.obj" "%OBJ%\render\*.obj" ^
  "%OBJ%\dll\coop.obj" "%OBJ%\dll\coop_ui.obj" "%OBJ%\dll\mod.obj" "%OBJ%\dll\ui_input.obj"
if errorlevel 1 ( echo [build] ERROR: lib m2_ui.lib failed & goto :fail )
rem m2_game.lib: the game-free parts of src\game the tests use (archives, binary XML, inflate, the menu patch)
lib /NOLOGO /OUT:"%OBJ%\m2_game.lib" "%OBJ%\game\sds.obj" "%OBJ%\game\bxml.obj" "%OBJ%\game\inflate.obj" ^
  "%OBJ%\game\menu_patch.obj"
if errorlevel 1 ( echo [build] ERROR: lib m2_game.lib failed & goto :fail )

echo [build] link mafia2coop.dll
link %MC_LDFLAGS% /DLL /OUT:"%OUT%\mafia2coop.dll" /PDB:"%OUT%\mafia2coop.pdb" /IMPLIB:"%OBJ%\mafia2coop.lib" ^
  "%OBJ%\dll\dllmain.obj" "%OBJ%\game\*.obj" "%OBJ%\m2_ui.lib" %MC_SYSLIBS%
if errorlevel 1 ( echo [build] ERROR: linking mafia2coop.dll failed & goto :fail )

echo [build] link Mafia2Coop.exe
link %MC_LDFLAGS% /SUBSYSTEM:WINDOWS /OUT:"%OUT%\Mafia2Coop.exe" /PDB:"%OUT%\Mafia2Coop-launcher.pdb" ^
  "%OBJ%\launcher\*.obj" "%OBJ%\game\game_info.obj" kernel32.lib user32.lib advapi32.lib shell32.lib
if errorlevel 1 ( echo [build] ERROR: linking Mafia2Coop.exe failed & goto :fail )

rem ---- unit tests: every tests\*.cpp is one console exe -------------------------------------------------------
for %%f in ("%ROOT%\tests\*.cpp") do (
  echo [build] %%~nf.exe
  cl %CXXFLAGS% /Fd"%OBJ%\tests\vc.pdb" /Fo"%OBJ%\tests\%%~nf.obj" "%%f"
  if errorlevel 1 ( echo [build] ERROR: compiling %%~nxf failed & goto :fail )
  link %MC_LDFLAGS% /SUBSYSTEM:CONSOLE /OUT:"%OUT%\%%~nf.exe" /PDB:"%OUT%\%%~nf.pdb" "%OBJ%\tests\%%~nf.obj" ^
    "%OBJ%\game\game_info.obj" "%OBJ%\m2_game.lib" "%OBJ%\m2_ui.lib" %MC_SYSLIBS%
  if errorlevel 1 ( echo [build] ERROR: linking %%~nf.exe failed & goto :fail )
)

if /i "%~1"=="test" (
  for %%f in ("%ROOT%\tests\*.cpp") do (
    echo [test] %%~nf
    "%OUT%\%%~nf.exe" >"%OUT%\%%~nf.txt" 2>&1
    if errorlevel 1 ( type "%OUT%\%%~nf.txt" & echo [test] FAILED: %%~nf & goto :fail )
  )
  echo [test] all passed
)

echo [build] OK -^> %OUT%
endlocal
exit /b 0

:fail
echo [build] FAILED
endlocal
exit /b 1
