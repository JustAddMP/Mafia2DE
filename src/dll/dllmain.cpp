// Mod dll entry, adapted from JustAddMP core 8c9c0fd src/dll/dllmain.cpp. DllMain only signals the launcher and
// spawns the init thread. The init thread opens the log, installs MinHook and the game layer, waits for the game
// window, subclasses it (panel input; the game keeps running when unfocused), hooks the game's input
// (dll/ui_input.h) and Present (render/present_hook.h). Menu-only build: no ini, no crash handler, no Steam.
#include "core/hook.h"
#include "core/log.h"
#include "dll/mod.h"
#include "dll/ui_input.h"
#include "game/game_module.h"
#include "render/present_hook.h"

#include <windows.h>
#include <cstdio>
#include <string>

namespace mc {
namespace {

HMODULE _module = nullptr;
WNDPROC _gameWndProc = nullptr;

// WM_ACTIVATEAPP(FALSE) is swallowed so an unfocused game keeps rendering (two instances side by side; the panel
// stays drawn). While a co-op panel is open, ui_input swallows the keyboard and mouse messages so the game's menus
// underneath do not react.
LRESULT CALLBACK ModWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ACTIVATEAPP && wp == FALSE) return 0;
    if (ui_input::OnWindowMessage(msg, static_cast<uint64_t>(wp), static_cast<int64_t>(lp))) return 0;
    return CallWindowProcW(_gameWndProc, hwnd, msg, wp, lp);
}

void SubclassWindow(HWND hwnd) {
    ui_input::SetWindow(hwnd);
    _gameWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    SetLastError(0);
    auto prev = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ModWndProc)));
    if (!prev && GetLastError() != 0) {
        MC_LOG_WARN("init: SetWindowLongPtr failed (%lu): co-op panels get no keyboard/mouse", GetLastError());
        return;
    }
    _gameWndProc = prev;
    MC_LOG_INFO("init: window subclassed (panel input; window deactivation ignored)");
}

std::wstring ModuleDir(HMODULE mod) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring s(path, n);
    size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Ascii(const char* s) {
    std::wstring w;
    for (; *s; ++s) w += static_cast<wchar_t>(*s);
    return w;
}

std::wstring EnvName(const wchar_t* key) { return Ascii(GetGameInfo().envPrefix) + key; }

struct WindowSearch {
    DWORD pid;
    HWND found;
};

BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM lp) {
    auto* search = reinterpret_cast<WindowSearch*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid || !IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    if (rc.right - rc.left < 320 || rc.bottom - rc.top < 200) return TRUE;  // skip splash/tool windows
    search->found = hwnd;
    return FALSE;
}

HWND WaitForGameWindow(DWORD timeoutMs) {
    const ULONGLONG start = GetTickCount64();
    WindowSearch search{GetCurrentProcessId(), nullptr};
    while (GetTickCount64() - start < timeoutMs) {
        EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&search));
        if (search.found) return search.found;
        Sleep(250);
    }
    return nullptr;
}

// Where a dll the overlay or the input hooks depend on was loaded from. A dxgi.dll / d3d11.dll outside System32 is a
// wrapper (ReShade, an upscaler mod, ...): the Present hook then hooks the wrapper's swapchain (logged so a test
// knows what it ran against).
void LogModule(const wchar_t* name) {
    HMODULE m = GetModuleHandleW(name);
    if (!m) {
        MC_LOG_INFO("module %s: not loaded", Narrow(name).c_str());
        return;
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(m, path, MAX_PATH);
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const bool system = _wcsnicmp(path, sys, wcslen(sys)) == 0;
    MC_LOG_INFO("module %s: %s%s", Narrow(name).c_str(), Narrow(path).c_str(),
                system ? "" : "  (not the system dll: a wrapper / mod, or the game's own copy)");
}

void LogEnvironment(const std::wstring& dir) {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    MC_LOG_INFO("%s %s, built " __DATE__ " " __TIME__, GetGameInfo().modName, GetGameInfo().modVersion);
    MC_LOG_INFO("pid %lu, exe %s", GetCurrentProcessId(), Narrow(exe).c_str());
    MC_LOG_INFO("main module base %p size 0x%lx timestamp 0x%08lx", static_cast<const void*>(base),
                static_cast<unsigned long>(nt->OptionalHeader.SizeOfImage),
                static_cast<unsigned long>(nt->FileHeader.TimeDateStamp));
    MC_LOG_INFO("dll dir %s", Narrow(dir).c_str());
    char appId[32] = {};
    GetEnvironmentVariableA("SteamAppId", appId, sizeof(appId));
    MC_LOG_INFO("env SteamAppId=%s", appId[0] ? appId : "(unset)");
    if (GetEnvironmentVariableW(EnvName(L"LAUNCHER").c_str(), nullptr, 0) > 0)
        MC_LOG_INFO("loaded by %s", Narrow(GetGameInfo().launcherName).c_str());
    else
        MC_LOG_INFO("loaded by an unknown injector (no %sLAUNCHER)", GetGameInfo().envPrefix);
}

// One log per game instance: --log <tag> -> <logPrefix>-<tag>.log; without it the first instance writes
// <logPrefix>.log and any further concurrent instance <logPrefix>-<pid>.log.
std::wstring LogFileName(std::wstring& tagOut) {
    const std::wstring prefix = GetGameInfo().logPrefix;
    wchar_t tag[64] = {};
    DWORD n = GetEnvironmentVariableW(EnvName(L"LOG").c_str(), tag, 64);
    if (n > 0 && n < 64) {
        tagOut = tag;
        return prefix + L"-" + tag + L".log";
    }
    const std::wstring mutexName = L"Local\\" + Ascii(GetGameInfo().modName) + L"PrimaryLog";
    HANDLE m = CreateMutexW(nullptr, FALSE, mutexName.c_str());  // intentionally never closed
    if (m && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(m);
        return prefix + L"-" + std::to_wstring(GetCurrentProcessId()) + L".log";
    }
    return prefix + L".log";
}

DWORD WINAPI InitThread(LPVOID) {
    const std::wstring dir = ModuleDir(_module);
    std::wstring tag;
    const std::wstring logName = LogFileName(tag);
    core::log::Init(dir + L"\\logs", logName.c_str());
    LogEnvironment(dir);
    for (const wchar_t* m : {L"dxgi.dll", L"d3d11.dll", L"xinput1_4.dll", L"dinput8.dll", L"steam_api64.dll"})
        LogModule(m);

    if (!core::hook::Init()) return 1;
    GetGameModule().Init(dir, Narrow(tag));

    constexpr DWORD kTimeoutMs = 300 * 1000;
    MC_LOG_INFO("init: waiting for the game window");
    HWND hwnd = WaitForGameWindow(kTimeoutMs);
    if (!hwnd) {
        MC_LOG_ERROR("init: game window did not appear within %lu s; not hooking", kTimeoutMs / 1000);
        return 1;
    }
    char title[128] = {}, cls[128] = {};
    GetWindowTextA(hwnd, title, sizeof(title));
    GetClassNameA(hwnd, cls, sizeof(cls));
    MC_LOG_INFO("init: game window %p class '%s' title '%s'", static_cast<void*>(hwnd), cls, title);
    // what is loaded by now (the game creates its D3D11 device around the time its window appears)
    for (const wchar_t* m : {L"dxgi.dll", L"d3d11.dll", L"xinput1_4.dll", L"dinput8.dll"}) LogModule(m);

    SubclassWindow(hwnd);
    ui_input::Install();
    Mod::Init();
    if (!render::InstallPresentHook(&Mod::Tick, &Mod::OnResize, &Mod::OnBeforeResize, &Mod::OnDeviceLost)) {
        MC_LOG_ERROR("init: Present hook failed; mod inactive");
        return 1;
    }
    MC_LOG_INFO("init: done; press F8 in the game for the CO-OP panel");
    return 0;
}

} // namespace
} // namespace mc

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        mc::_module = module;
        DisableThreadLibraryCalls(module);
        // Tell the launcher the dll is in (event created by the launcher; absent when injected otherwise).
        std::wstring name = L"Local\\";
        for (const char* p = mc::GetGameInfo().modName; *p; ++p) name += static_cast<wchar_t>(*p);
        name += L"Injected-" + std::to_wstring(GetCurrentProcessId());
        if (HANDLE ev = OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str())) {
            SetEvent(ev);
            CloseHandle(ev);
        }
        if (HANDLE t = CreateThread(nullptr, 0, mc::InitThread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
