#include "game/sds_redirect.h"

#include "core/hook.h"
#include "core/log.h"
#include "game/menu_patch.h"
#include "game/sds.h"

#include <windows.h>

#include <atomic>
#include <cwctype>
#include <mutex>
#include <string>
#include <vector>

namespace mc::game::sds_redirect {
namespace {

using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
CreateFileWFn _origW = nullptr;
CreateFileAFn _origA = nullptr;
// The size the game may ask for by name (before or instead of opening) must be our copy's, not the original's.
using GetAttrWFn = DWORD(WINAPI*)(LPCWSTR);
using GetAttrAFn = DWORD(WINAPI*)(LPCSTR);
using GetAttrExWFn = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
using GetAttrExAFn = BOOL(WINAPI*)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
GetAttrWFn _origAttrW = nullptr;
GetAttrAFn _origAttrA = nullptr;
GetAttrExWFn _origAttrExW = nullptr;
GetAttrExAFn _origAttrExA = nullptr;

std::wstring _cacheDir;
thread_local bool _busy = false;  // our own file access while building (never redirected)
std::atomic<int> _sdsOpensLogged{0};

enum class Kind { None, GuiMain, Gui };

struct Built {
    bool tried = false;
    std::wstring path;  // empty: not patched (the game gets its own file)
};
std::mutex _mutex;
Built _built[3];

std::string Narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towlower(c == L'/' ? L'\\' : c));
    return s;
}

bool EndsWith(const std::wstring& s, const wchar_t* tail) {
    const size_t n = wcslen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

Kind Classify(const std::wstring& lowerPath) {
    if (EndsWith(lowerPath, L"\\sds\\gui\\gui-main.sds")) return Kind::GuiMain;
    if (EndsWith(lowerPath, L"\\sds\\gui\\gui.sds")) return Kind::Gui;
    return Kind::None;
}

bool ReadFileAll(const std::wstring& path, std::vector<uint8_t>& out) {
    HANDLE f = _origW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart < (512ll << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(f, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
    }
    CloseHandle(f);
    return ok;
}

bool WriteFileAll(const std::wstring& path, const void* data, size_t n) {
    HANDLE f = _origW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, data, static_cast<DWORD>(n), &put, nullptr) && put == n;
    CloseHandle(f);
    return ok;
}

std::wstring FullPath(const std::wstring& p) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 2, buf, nullptr);
    return n > 0 && n < MAX_PATH * 2 ? std::wstring(buf, n) : p;
}

void SaveReport(const std::wstring& name, const menu_patch::Result& r) {
    std::string text;
    for (const auto& l : r.log) text += "# " + l + "\n";
    for (const auto& [res, xml] : r.dumps) text += "\n<!-- " + res + " -->\n" + xml;
    WriteFileAll(_cacheDir + L"\\" + name, text.data(), text.size());
}

// Builds the patched copy of `requested` (GuiMain or Gui) and returns its path, or "" (logged) on any failure.
std::wstring Build(Kind kind, const std::wstring& requested) {
    const std::wstring original = FullPath(requested);
    const std::wstring dir = original.substr(0, original.find_last_of(L'\\'));
    const wchar_t* fileName = kind == Kind::GuiMain ? L"gui-main.sds" : L"gui.sds";
    const ULONGLONG t0 = GetTickCount64();
    std::vector<uint8_t> raw, rawMain;
    if (!ReadFileAll(original, raw)) {
        MC_LOG_WARN("menu: cannot read %s (%lu); menus unchanged", Narrow(original).c_str(), GetLastError());
        return {};
    }
    sds::Archive a, main;
    std::string err;
    if (!sds::Read(raw, a, &err)) {
        MC_LOG_WARN("menu: %s: %s; menus unchanged", Narrow(original).c_str(), err.c_str());
        return {};
    }
    menu_patch::Result r;
    if (kind == Kind::GuiMain) {
        r = menu_patch::PatchGuiMain(a);
    } else {
        const std::wstring mainPath = dir + L"\\gui-main.sds";
        if (!ReadFileAll(mainPath, rawMain) || !sds::Read(rawMain, main, &err)) {
            MC_LOG_WARN("menu: cannot read %s for the pause page template (%s); pause menu unchanged",
                        Narrow(mainPath).c_str(), err.c_str());
            return {};
        }
        r = menu_patch::PatchGui(a, main);
    }
    for (const auto& l : r.log) MC_LOG_INFO("menu: %ls: %s", fileName, l.c_str());
    const std::wstring pid = std::to_wstring(GetCurrentProcessId());
    SaveReport(pid + L"-" + fileName + L".patch.txt", r);
    if (!r.ok) {
        MC_LOG_WARN("menu: %ls not patched; the game's own menus load unchanged", fileName);
        return {};
    }
    const std::vector<uint8_t> out = sds::Write(a);
    // check what we wrote reads back (same resources)
    sds::Archive check;
    if (!sds::Read(out, check, &err) || check.resources.size() != a.resources.size()) {
        MC_LOG_WARN("menu: the patched %ls does not read back (%s); menus unchanged", fileName, err.c_str());
        return {};
    }
    const std::wstring path = _cacheDir + L"\\" + pid + L"-" + fileName;
    if (!WriteFileAll(path, out.data(), out.size())) {
        MC_LOG_WARN("menu: cannot write %s (%lu); menus unchanged", Narrow(path).c_str(), GetLastError());
        return {};
    }
    MC_LOG_INFO("menu: %ls patched in %llu ms: %zu -> %zu bytes, %zu resources -> %s", fileName, GetTickCount64() - t0,
                raw.size(), out.size(), a.resources.size(), Narrow(path).c_str());
    return path;
}

// The path the game should open instead of `path`, or "" (open it as asked).
std::wstring Redirect(const std::wstring& path) {
    if (_busy || path.empty()) return {};
    const std::wstring lower = Lower(path);
    if (!EndsWith(lower, L".sds")) return {};
    const Kind kind = Classify(L"\\" + lower);  // also a relative "sds/gui/..."
    if (_sdsOpensLogged < 40) {
        ++_sdsOpensLogged;
        MC_LOG_INFO("sds: game opens %s", Narrow(path).c_str());
    }
    if (kind == Kind::None) return {};
    if (lower.find(Lower(_cacheDir)) == 0) return {};  // our own copy
    std::lock_guard lock(_mutex);
    Built& b = _built[static_cast<int>(kind)];
    if (!b.tried) {
        b.tried = true;
        _busy = true;
        b.path = Build(kind, path);
        _busy = false;
    }
    if (!b.path.empty()) MC_LOG_INFO("sds: %s -> %s", Narrow(path).c_str(), Narrow(b.path).c_str());
    return b.path;
}

HANDLE WINAPI CreateFileWDetour(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                DWORD flags, HANDLE tpl) {
    if (name && !_busy) {
        const std::wstring to = Redirect(name);
        if (!to.empty()) return _origW(to.c_str(), access, share, sa, disp, flags, tpl);
    }
    return _origW(name, access, share, sa, disp, flags, tpl);
}

std::wstring RedirectA(LPCSTR name) {
    if (!name || _busy) return {};
    const int n = MultiByteToWideChar(CP_ACP, 0, name, -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_ACP, 0, name, -1, w.data(), n);
    return Redirect(w);
}

HANDLE WINAPI CreateFileADetour(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                DWORD flags, HANDLE tpl) {
    const std::wstring to = RedirectA(name);
    if (!to.empty()) return _origW(to.c_str(), access, share, sa, disp, flags, tpl);
    return _origA(name, access, share, sa, disp, flags, tpl);
}

DWORD WINAPI GetAttrWDetour(LPCWSTR name) {
    const std::wstring to = name && !_busy ? Redirect(name) : std::wstring();
    return _origAttrW(to.empty() ? name : to.c_str());
}
DWORD WINAPI GetAttrADetour(LPCSTR name) {
    const std::wstring to = RedirectA(name);
    return to.empty() ? _origAttrA(name) : _origAttrW(to.c_str());
}
BOOL WINAPI GetAttrExWDetour(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    const std::wstring to = name && !_busy ? Redirect(name) : std::wstring();
    return _origAttrExW(to.empty() ? name : to.c_str(), level, info);
}
BOOL WINAPI GetAttrExADetour(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    const std::wstring to = RedirectA(name);
    return to.empty() ? _origAttrExA(name, level, info) : _origAttrExW(to.c_str(), level, info);
}

void CleanCache() {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((_cacheDir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        DeleteFileW((_cacheDir + L"\\" + fd.cFileName).c_str());  // fails for a running instance's files: fine
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

}  // namespace

bool Install(const std::wstring& dllDir) {
    _cacheDir = dllDir + L"\\cache";
    CreateDirectoryW(_cacheDir.c_str(), nullptr);
    CleanCache();
    HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
    if (!kb) kb = GetModuleHandleW(L"kernel32.dll");
    void* w = kb ? reinterpret_cast<void*>(GetProcAddress(kb, "CreateFileW")) : nullptr;
    void* a = kb ? reinterpret_cast<void*>(GetProcAddress(kb, "CreateFileA")) : nullptr;
    const bool okW = w && core::hook::Install("CreateFileW", w, reinterpret_cast<void*>(&CreateFileWDetour),
                                              reinterpret_cast<void**>(&_origW));
    const bool okA = a && core::hook::Install("CreateFileA", a, reinterpret_cast<void*>(&CreateFileADetour),
                                              reinterpret_cast<void**>(&_origA));
    auto hook = [&](const char* name, void* detour, void** orig) {
        void* t = kb ? reinterpret_cast<void*>(GetProcAddress(kb, name)) : nullptr;
        return t && core::hook::Install(name, t, detour, orig);
    };
    // attribute queries: only matter if the game asks for an archive's size by name; failures are not fatal
    bool attrs = hook("GetFileAttributesW", reinterpret_cast<void*>(&GetAttrWDetour), reinterpret_cast<void**>(&_origAttrW));
    attrs &= hook("GetFileAttributesA", reinterpret_cast<void*>(&GetAttrADetour), reinterpret_cast<void**>(&_origAttrA));
    attrs &= hook("GetFileAttributesExW", reinterpret_cast<void*>(&GetAttrExWDetour), reinterpret_cast<void**>(&_origAttrExW));
    attrs &= hook("GetFileAttributesExA", reinterpret_cast<void*>(&GetAttrExADetour), reinterpret_cast<void**>(&_origAttrExA));
    MC_LOG_INFO("menu: archive redirect %s (CreateFileW %d, CreateFileA %d, attribute queries %d), cache %s",
                okW && okA ? "installed" : "INCOMPLETE", okW, okA, attrs, Narrow(_cacheDir).c_str());
    return okW && okA;
}

}  // namespace mc::game::sds_redirect
