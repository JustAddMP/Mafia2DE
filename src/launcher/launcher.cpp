// The launcher (Mafia2Coop.exe), adapted from JustAddMP core 8c9c0fd src/launcher/launcher.cpp: finds Mafia II:
// Definitive Edition through Steam, starts it suspended with SteamAppId set, injects mafia2coop.dll (from this
// exe's directory) and resumes it. Menu-only build: no version.dll proxy, no install, no version refusal (the exe
// CRC32 is only logged against the known build).
//
// Usage: Mafia2Coop.exe [--log <tag>] [--<key> [value]] [--game <exe path>]
// Every --<key> [value] (except --game) is passed to the dll as environment variable MAFIA2COOP_<KEY>=<value>
// ("1" for a bare flag). Ini next to the launcher (optional): game_path = <folder or exe>, inject_method = apc|thread.
#include "game/game_info.h"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace mc::launcher {
namespace {

const GameInfo& Gi() { return GetGameInfo(); }

std::wstring Ascii(const char* s) {
    std::wstring w;
    for (; *s; ++s) w += static_cast<wchar_t>(*s);
    return w;
}
std::wstring ModName() { return Ascii(Gi().modName); }
std::wstring AppId() { return std::to_wstring(Gi().steamAppId); }

FILE* _log = nullptr;

void Log(const wchar_t* fmt, ...) {
    if (!_log) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(_log, L"%02u:%02u:%02u.%03u ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfwprintf(_log, fmt, args);
    va_end(args);
    fputwc(L'\n', _log);
    fflush(_log);
}

int Fail(const std::wstring& msg) {
    Log(L"ERROR: %s", msg.c_str());
    MessageBoxW(nullptr, msg.c_str(), ModName().c_str(), MB_ICONERROR | MB_OK);
    return 1;
}

std::wstring LastErrorText(DWORD err = GetLastError()) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring s = buf ? buf : L"";
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ')) s.pop_back();
    return L"error " + std::to_wstring(err) + (s.empty() ? L"" : L": " + s);
}

bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return {};
    return s.substr(b, s.find_last_not_of(L" \t\r\n") - b + 1);
}

std::wstring Widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::wstring ReadTextFile(const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::stringstream ss;
    ss << in.rdbuf();
    return Widen(ss.str());
}

std::wstring NormalizePath(std::wstring p) {
    std::replace(p.begin(), p.end(), L'/', L'\\');
    while (!p.empty() && p.back() == L'\\') p.pop_back();
    return p;
}

std::wstring IniValue(const std::wstring& iniPath, const std::wstring& key) {
    std::wistringstream in(ReadTextFile(iniPath));
    std::wstring line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] == 0xFEFF) line.erase(0, 1);
        std::wstring t = Trim(line);
        if (t.empty() || t[0] == L'#' || t[0] == L';') continue;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) continue;
        if (Lower(Trim(t.substr(0, eq))) != key) continue;
        std::wstring v = Trim(t.substr(eq + 1));
        if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"') v = v.substr(1, v.size() - 2);
        return v;
    }
    return {};
}

// --- Steam library discovery -------------------------------------------------------------------

std::wstring RegString(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    wchar_t buf[MAX_PATH * 2];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, subkey, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

// All "key" "value" pairs with the given key in a Valve KeyValues text file (values unescaped).
std::vector<std::wstring> VdfValues(const std::wstring& text, const std::wstring& key) {
    std::vector<std::wstring> out;
    const std::wstring needle = Lower(L"\"" + key + L"\"");
    const std::wstring lowerText = Lower(text);
    size_t pos = 0;
    while ((pos = lowerText.find(needle, pos)) != std::wstring::npos) {
        pos += needle.size();
        size_t open = text.find(L'"', pos);
        size_t nl = text.find(L'\n', pos);
        if (open == std::wstring::npos || (nl != std::wstring::npos && open > nl)) continue;  // a block key
        std::wstring value;
        size_t i = open + 1;
        for (; i < text.size() && text[i] != L'"'; ++i) {
            if (text[i] == L'\\' && i + 1 < text.size()) ++i;
            value += text[i];
        }
        out.push_back(value);
        pos = i;
    }
    return out;
}

std::wstring FindGameViaSteam() {
    std::vector<std::wstring> steamRoots;
    for (auto [root, sub, val] : {std::tuple{HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"},
                                  std::tuple{HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"},
                                  std::tuple{HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"}}) {
        std::wstring p = RegString(root, sub, val);
        if (!p.empty()) steamRoots.push_back(NormalizePath(p));
    }
    if (steamRoots.empty()) {
        Log(L"steam: no Steam install found in the registry");
        return {};
    }
    std::vector<std::wstring> libraries;
    for (const auto& root : steamRoots) {
        Log(L"steam: root %s", root.c_str());
        libraries.push_back(root);
        for (auto& lib : VdfValues(ReadTextFile(root + L"\\steamapps\\libraryfolders.vdf"), L"path"))
            libraries.push_back(NormalizePath(lib));
    }
    for (const auto& lib : libraries) {
        const std::wstring manifest = lib + L"\\steamapps\\appmanifest_" + AppId() + L".acf";
        const std::wstring acf = ReadTextFile(manifest);
        if (acf.empty()) continue;
        auto dirs = VdfValues(acf, L"installdir");
        const std::wstring installDir = dirs.empty() ? std::wstring(Gi().steamInstallDir) : dirs.front();
        const std::wstring exe = lib + L"\\steamapps\\common\\" + installDir + L"\\" + Gi().gameExe;
        Log(L"steam: manifest %s -> %s", manifest.c_str(), exe.c_str());
        if (FileExists(exe)) return exe;
    }
    Log(L"steam: app %s not found in %zu libraries", AppId().c_str(), libraries.size());
    return {};
}

// CRC-32 (zlib's), bytewise; the launcher reads the 37 MB exe once per start (logged only).
bool FileCrc32(const std::wstring& path, uint32_t& crc) {
    static uint32_t table[256];
    if (!table[1])
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
            table[i] = c;
        }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::vector<uint8_t> buf(4u << 20);
    uint32_t c = 0xFFFFFFFFu;
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) {
            ok = false;
            break;
        }
        if (got == 0) break;
        for (DWORD i = 0; i < got; ++i) c = (c >> 8) ^ table[(c ^ buf[i]) & 0xFF];
    }
    CloseHandle(f);
    crc = ~c;
    return ok;
}

std::vector<DWORD> FindProcesses(const wchar_t* exeName) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
        if (_wcsicmp(pe.szExeFile, exeName) == 0) pids.push_back(pe.th32ProcessID);
    CloseHandle(snap);
    return pids;
}

// Current environment minus Steam/our (<envPrefix>*) vars, plus ours; sorted, double-NUL terminated.
std::vector<wchar_t> BuildEnvironment(const std::vector<std::pair<std::wstring, std::wstring>>& extra) {
    std::vector<std::wstring> entries;
    const std::wstring ourPrefix = Lower(Ascii(Gi().envPrefix));
    wchar_t* env = GetEnvironmentStringsW();
    for (const wchar_t* p = env; p && *p; p += wcslen(p) + 1) {
        std::wstring e = p;
        std::wstring name = Lower(e.substr(0, e.find(L'=', 1)));
        if (name == L"steamappid" || name == L"steamgameid" || name.rfind(ourPrefix, 0) == 0) continue;
        entries.push_back(e);
    }
    if (env) FreeEnvironmentStringsW(env);
    for (size_t i = 0; i < extra.size(); ++i) {  // a name given twice: the last one wins
        bool later = false;
        for (size_t j = i + 1; j < extra.size() && !later; ++j)
            later = _wcsicmp(extra[i].first.c_str(), extra[j].first.c_str()) == 0;
        if (!later) entries.push_back(extra[i].first + L"=" + extra[i].second);
    }
    std::sort(entries.begin(), entries.end(), [](const std::wstring& a, const std::wstring& b) {
        return _wcsicmp(a.substr(0, a.find(L'=', 1)).c_str(), b.substr(0, b.find(L'=', 1)).c_str()) < 0;
    });
    std::vector<wchar_t> block;
    for (auto& e : entries) {
        block.insert(block.end(), e.begin(), e.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

bool InjectRemoteThread(HANDLE process, void* remotePath, DWORD& err) {
    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
    HANDLE t = CreateRemoteThread(process, nullptr, 0, loadLibrary, remotePath, 0, nullptr);
    if (!t) {
        err = GetLastError();
        return false;
    }
    DWORD wait = WaitForSingleObject(t, 15000);
    DWORD code = 0;
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    err = wait == WAIT_OBJECT_0 ? (code ? 0 : ERROR_MOD_NOT_FOUND) : WAIT_TIMEOUT;
    return wait == WAIT_OBJECT_0 && code != 0;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

} // namespace

int Run() {
    std::wstring selfDir(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, selfDir.data(), static_cast<DWORD>(selfDir.size()));
        if (n == 0) return Fail(L"GetModuleFileName failed: " + LastErrorText());
        if (n < selfDir.size()) {
            selfDir.resize(n);
            break;
        }
        selfDir.resize(selfDir.size() * 2);
    }
    selfDir = selfDir.substr(0, selfDir.find_last_of(L'\\'));
    const std::wstring iniPath = selfDir + L"\\" + Gi().iniName;
    const std::wstring dllPath = selfDir + L"\\" + Gi().dllName;
    const std::wstring envPrefix = Ascii(Gi().envPrefix);

    // ---- arguments: --key [value] -> MAFIA2COOP_KEY env var for the dll; --game <exe> overrides the search ----
    std::vector<std::pair<std::wstring, std::wstring>> passEnv;
    std::wstring gameOverride, logTag;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (a.rfind(L"--", 0) != 0 || a.size() < 3) continue;
        std::wstring key = Lower(a.substr(2)), value = L"1";
        if (i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) != 0) value = argv[++i];
        if (key == L"game") {
            gameOverride = value;
            continue;
        }
        if (key == L"log") logTag = value;
        std::replace(key.begin(), key.end(), L'-', L'_');
        std::wstring upper = key;
        for (auto& c : upper) c = static_cast<wchar_t>(towupper(c));
        passEnv.emplace_back(envPrefix + upper, value);
    }
    if (argv) LocalFree(argv);

    const std::wstring exeName = FileNameOf(Gi().gameExe);
    const std::vector<DWORD> running = FindProcesses(exeName.c_str());
    std::wstring logName = L"launcher.log";
    if (!logTag.empty())
        logName = L"launcher-" + logTag + L".log";
    else if (!running.empty())
        logName = L"launcher-" + std::to_wstring(GetCurrentProcessId()) + L".log";
    CreateDirectoryW((selfDir + L"\\logs").c_str(), nullptr);
    _wfopen_s(&_log, (selfDir + L"\\logs\\" + logName).c_str(), L"w, ccs=UTF-8");
    Log(L"%s %s launcher, built " __DATE__ " " __TIME__, ModName().c_str(), Ascii(Gi().modVersion).c_str());
    for (auto& [k, v] : passEnv) Log(L"arg: %s=%s", k.c_str(), v.c_str());

    // ---- find the game ----
    std::wstring gameExe;
    if (!gameOverride.empty()) {
        gameExe = NormalizePath(gameOverride);
        Log(L"game: from --game: %s", gameExe.c_str());
    } else {
        gameExe = FindGameViaSteam();
        if (gameExe.empty()) {
            std::wstring p = IniValue(iniPath, L"game_path");
            if (!p.empty()) {
                p = NormalizePath(p);
                gameExe = Lower(p).ends_with(L".exe") ? p : p + L"\\" + Gi().gameExe;
                Log(L"game: from %s game_path: %s", Gi().iniName, gameExe.c_str());
            }
        }
    }
    const std::wstring gameName = Ascii(Gi().gameName);
    if (gameExe.empty())
        return Fail(L"Could not find " + gameName + L" (Steam app " + AppId() + L").\n\nAdd a line\n"
                    L"    game_path = <the game's install folder>\nto " + iniPath);
    if (!FileExists(gameExe)) return Fail(L"Game executable not found:\n" + gameExe);
    if (!FileExists(dllPath)) return Fail(std::wstring(Gi().dllName) + L" not found next to the launcher:\n" + dllPath);

    // ---- game version: logged only ----
    uint32_t crc = 0;
    if (FileCrc32(gameExe, crc)) {
        const char* label = nullptr;
        for (size_t i = 0; i < Gi().buildCount; ++i)
            if (Gi().builds[i].crc32 == crc) label = Gi().builds[i].label;
        Log(L"version: exe CRC32 %08X -> %s", crc, label ? Widen(label).c_str() : L"unknown build (menu may still work)");
    } else {
        Log(L"version: cannot read the exe (%s)", LastErrorText().c_str());
    }
    const std::wstring gameDir = gameExe.substr(0, gameExe.find_last_of(L'\\'));
    Log(L"game: %s", gameExe.c_str());
    if (!running.empty()) Log(L"note: %zu game instance(s) already running; starting another", running.size());

    // ---- start suspended ----
    // SteamAppId: the exe calls SteamAPI_RestartAppIfNecessary; with the app id set (and steam_appid.txt next to
    // the exe) it runs in place instead of being restarted through Steam without our dll.
    passEnv.emplace_back(L"SteamAppId", AppId());
    passEnv.emplace_back(L"SteamGameId", AppId());
    passEnv.emplace_back(envPrefix + L"LAUNCHER", L"1");
    std::vector<wchar_t> env = BuildEnvironment(passEnv);
    std::wstring cmdLine = L"\"" + gameExe + L"\"";
    Log(L"command line: %s", cmdLine.c_str());
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(gameExe.c_str(), cmdLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT, env.data(), gameDir.c_str(), &si, &pi))
        return Fail(L"Could not start the game:\n" + gameExe + L"\n\n" + LastErrorText());
    Log(L"started pid %lu (suspended)", pi.dwProcessId);

    auto abortGame = [&](const std::wstring& why) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return Fail(why);
    };

    const std::wstring eventName = L"Local\\" + ModName() + L"Injected-" + std::to_wstring(pi.dwProcessId);
    HANDLE loaded = CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
    if (!loaded) return abortGame(L"CreateEvent failed: " + LastErrorText());

    const size_t pathBytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(pi.hProcess, nullptr, pathBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath || !WriteProcessMemory(pi.hProcess, remotePath, dllPath.c_str(), pathBytes, nullptr))
        return abortGame(L"Could not write into the game process: " + LastErrorText());

    // ---- inject: an APC to LoadLibraryW on the suspended main thread (runs after the loader has mapped the exe's
    // imports, before its entry point); fallback CreateRemoteThread(LoadLibraryW) after resuming ----
    std::wstring method = Lower(IniValue(iniPath, L"inject_method"));
    if (method.empty()) method = L"apc";
    Log(L"inject: method %s, dll %s", method.c_str(), dllPath.c_str());
    auto loadLibrary = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    bool apcQueued = false;
    if (method == L"apc") {
        if (!QueueUserAPC(reinterpret_cast<PAPCFUNC>(reinterpret_cast<void*>(loadLibrary)), pi.hThread,
                          reinterpret_cast<ULONG_PTR>(remotePath))) {
            Log(L"inject: QueueUserAPC failed (%s), will use remote thread", LastErrorText().c_str());
            method = L"thread";
        } else {
            apcQueued = true;
        }
    }
    if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) return abortGame(L"ResumeThread failed: " + LastErrorText());
    Log(L"resumed");

    HANDLE waits[2] = {loaded, pi.hProcess};
    DWORD w = WaitForMultipleObjects(2, waits, FALSE, method == L"apc" ? 15000 : 0);
    if (w == WAIT_OBJECT_0 + 1) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        return Fail(L"The game exited during startup (exit code " + std::to_wstring(code) + L") before " +
                    Gi().dllName + L" was loaded.");
    }
    if (w != WAIT_OBJECT_0) {
        if (method == L"apc") Log(L"inject: APC did not load the dll within 15 s, falling back to remote thread");
        DWORD err = 0;
        if (!InjectRemoteThread(pi.hProcess, remotePath, err) || WaitForSingleObject(loaded, 5000) != WAIT_OBJECT_0) {
            CloseHandle(loaded);
            return abortGame(L"Could not load " + std::wstring(Gi().dllName) + L" into the game: " + LastErrorText(err));
        }
        method = L"thread";
    } else {
        apcQueued = false;
    }
    Log(L"inject: dll loaded (via %s)", method.c_str());
    CloseHandle(loaded);

    // ---- a quick exit usually means Steam restarted the game without us ----
    if (WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        Sleep(2000);
        bool relaunched = false;
        for (DWORD p : FindProcesses(exeName.c_str()))
            if (std::find(running.begin(), running.end(), p) == running.end()) relaunched = true;
        Log(L"game exited early with code %lu (relaunched by Steam: %s)", code, relaunched ? L"yes" : L"no");
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return Fail(relaunched ? L"The game was restarted through Steam, so the mod is not loaded. "
                                 L"Make sure Steam is running and try again."
                               : L"The game exited right after starting (exit code " + std::to_wstring(code) +
                                     L"). See logs\\" + Gi().logPrefix + L".log.");
    }
    Log(L"game running (pid %lu); launcher done", pi.dwProcessId);
    if (!apcQueued) VirtualFreeEx(pi.hProcess, remotePath, 0, MEM_RELEASE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

} // namespace mc::launcher

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int rc = mc::launcher::Run();
    if (mc::launcher::_log) fclose(mc::launcher::_log);
    return rc;
}
