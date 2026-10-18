#pragma once
// Stand-in for JustAddMP core's Coop (src/dll/coop.h): the same interface the co-op panels (dll/coop_ui.cpp) call,
// but with no networking at all. This build only renders the co-op menus over Mafia II: Definitive Edition; there
// is never a session (phase stays Idle), and every action that would host, join or invite tells the player that it
// is not available yet (a banner) and logs it. The player name lives in memory only.
// When the game moves onto core, this file goes away and coop_ui.cpp is core's again.
#include <cstdint>
#include <string>
#include <vector>

namespace mc {

class Coop {
public:
    static void Init();
    static void Tick(float dt);

    // ---- actions (each one only shows "not available yet") ----
    static void Host();
    static void JoinAddress(const std::string& address);
    static void Leave();
    static bool InviteFriends();
    static bool OpenSteamFriends();

    // ---- JOIN FRIEND ----
    struct FriendRow {
        uint64_t steamId = 0;
        std::string name;
        std::string detail;
        bool joinable = false;
        bool warn = false;
        bool playing = false;
        int mode = -1;
    };
    struct FriendList {
        bool steamReady = false;  // always false here: the panel says co-op is not available yet
        bool available = false;
        bool scanned = false;
        int friends = 0, online = 0;
        std::vector<FriendRow> rows;
    };
    static FriendList GetFriends();
    static void RefreshFriends();
    static bool JoinFriend(uint64_t steamId);
    static void SetPassword(const std::string& password);
    static bool PopPasswordPrompt();
    static void RetryWithPassword(const std::string& password);
    static bool Kick(uint8_t playerId, bool ban);

    // ---- pre-game lobby ----
    static void SetReady(bool ready);
    static void SetLobbyMode(int mode);
    static bool StartGame(bool force = false);
    static bool JoinRunningGame();
    static bool PopOpenLobby();

    // ---- settings ----
    static std::string Name();           // the name typed in CO-OP SETTINGS > NAME ("" = Windows user name)
    static std::string EffectiveName();  // that name, else the Windows user name
    static void SetName(const std::string& name);

    enum class Phase { Idle, Starting, Hosting, Joining, Connected };
    struct PlayerRow {
        uint8_t id = 0;
        std::string name;
        int pingMs = -1;
        int health = -1;
        bool host = false, you = false;
        bool ready = false;
        std::string character;
    };
    struct Info {
        Phase phase = Phase::Idle;
        std::string target;
        std::vector<PlayerRow> players;
        bool steamReady = false;
        bool lobbyOpen = false;
        uint16_t port = 0;
        std::string localAddress;
        std::string lastAddress;
        bool passwordSet = false;
        int lobbyMode = 0;
        bool hostInGame = false;
        int hostGameMode = -1;
        bool allReady = true;
        bool ready = false;
        uint32_t starts = 0;
        bool reconnecting = false;
        int reconnectLeftS = 0;
    };
    static Info GetInfo();

    struct Notice {
        std::string title, text;
        bool error = false;
    };
    static bool PopNotice(Notice* out);
};

} // namespace mc
