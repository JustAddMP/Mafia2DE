// No-network stand-in for core's Coop (see coop.h): the menus render and react, nothing connects.
#include "dll/coop.h"

#include "core/log.h"

#include <windows.h>

#include <deque>

namespace mc {
namespace {

constexpr char kNotYet[] = "Not available yet: this build only shows the co-op menus in Mafia II.";

struct State {
    std::deque<Coop::Notice> notices;
    std::string name;         // CO-OP SETTINGS > NAME (memory only)
    std::string lastAddress;  // JOIN BY IP prefill
    bool password = false;
    int lobbyMode = 0;
};

State& S() {
    static State* s = new State;
    return *s;
}

void NotAvailable(const char* what) {
    MC_LOG_INFO("coop: %s pressed: not available yet (menu-only build)", what);
    if (S().notices.size() >= 4) S().notices.pop_front();
    S().notices.push_back({"CO-OP", kNotYet, false});
}

std::string WindowsUser() {
    wchar_t buf[128] = {};
    DWORD n = 128;
    if (!GetUserNameW(buf, &n)) return "Player";
    char out[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, sizeof(out), nullptr, nullptr);
    return out[0] ? out : "Player";
}

}  // namespace

void Coop::Init() { MC_LOG_INFO("coop: menu-only build: no networking, no Steam (HOST / JOIN show a notice)"); }
void Coop::Tick(float) {}

void Coop::Host() { NotAvailable("HOST CO-OP GAME"); }
void Coop::JoinAddress(const std::string& address) {
    S().lastAddress = address;
    MC_LOG_INFO("coop: JOIN BY IP '%s'", address.c_str());
    NotAvailable("CONNECT");
}
void Coop::Leave() { NotAvailable("LEAVE"); }
bool Coop::InviteFriends() {
    NotAvailable("INVITE FRIENDS");
    return false;
}
bool Coop::OpenSteamFriends() {
    NotAvailable("STEAM FRIENDS LIST");
    return false;
}

Coop::FriendList Coop::GetFriends() { return {}; }
void Coop::RefreshFriends() { MC_LOG_INFO("coop: JOIN FRIEND refresh (nothing to scan in this build)"); }
bool Coop::JoinFriend(uint64_t) {
    NotAvailable("JOIN FRIEND");
    return false;
}
void Coop::SetPassword(const std::string& password) {
    // Shown in the panel (lock marker, CHANGE / CLEAR PASSWORD) so those rows can be tried; never used.
    S().password = !password.empty();
    MC_LOG_INFO("coop: password %s (menu-only build: not used)", S().password ? "set" : "cleared");
}
bool Coop::PopPasswordPrompt() { return false; }
void Coop::RetryWithPassword(const std::string&) { NotAvailable("PASSWORD CONNECT"); }
bool Coop::Kick(uint8_t, bool) { return false; }

void Coop::SetReady(bool) { NotAvailable("READY"); }
void Coop::SetLobbyMode(int mode) { S().lobbyMode = mode; }
bool Coop::StartGame(bool) {
    NotAvailable("START");
    return false;
}
bool Coop::JoinRunningGame() {
    NotAvailable("JOIN GAME");
    return false;
}
bool Coop::PopOpenLobby() { return false; }

std::string Coop::Name() { return S().name; }
std::string Coop::EffectiveName() { return S().name.empty() ? WindowsUser() : S().name; }
void Coop::SetName(const std::string& name) {
    S().name = name;
    MC_LOG_INFO("coop: name set to '%s' (memory only)", name.c_str());
}

Coop::Info Coop::GetInfo() {
    Info i;
    i.lastAddress = S().lastAddress;
    i.passwordSet = S().password;
    i.lobbyMode = S().lobbyMode;
    return i;
}

bool Coop::PopNotice(Notice* out) {
    if (S().notices.empty()) return false;
    *out = S().notices.front();
    S().notices.pop_front();
    return true;
}

}  // namespace mc
