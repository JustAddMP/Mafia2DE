#pragma once
// The game layer as the co-op panels see it: a trimmed copy of JustAddMP core's include/mc/game_module.h
// (8c9c0fd) with only what dll/coop_ui.cpp calls (no session, no HUD sync). Threads: Init on the dll's init thread,
// the rest on the game's render thread (the Present hook).
#include "game/game_info.h"
#include "render/overlay.h"

#include <string>
#include <vector>

namespace mc {

// Commands for the co-op panels (core: the game's native menu entries; here: the hotkey).
enum class MenuCommand {
    Host,
    Leave,
    JoinFriend,
    JoinByIp,
    Invite,
    PausePanel,  // opens the CO-OP panel
    SetPassword,
    ClearPassword,
    Lobby,
    Settings,
};

enum class MenuMode { Idle, Hosting, Joining, Connected };

struct MenuState {
    MenuMode mode = MenuMode::Idle;
    int players = 0;
    bool password = false;
};

struct PanelOption {
    std::string label;
    std::string value;
};

struct LobbyMode {
    std::string name;
    bool available = true;
    std::string note;
};

class GameModule {
public:
    virtual ~GameModule() = default;
    virtual void Init(const std::wstring& dllDir, const std::string& logTag) = 0;
    virtual void RenderTick(float dt) = 0;

    virtual void SetMenuState(const MenuState&) {}
    virtual bool PopMenuCommand(MenuCommand*) { return false; }
    virtual void PanelOptions(std::vector<PanelOption>&) {}
    virtual void PanelOptionStep(size_t /*index*/, int /*delta*/) {}
    virtual void SettingsOptions(std::vector<PanelOption>&) {}
    virtual void SettingsOptionStep(size_t /*index*/, int /*delta*/) {}
    virtual void LobbyModes(std::vector<LobbyMode>&) {}
    virtual void DrawHud(render::DrawList&, float /*w*/, float /*h*/) {}
};

GameModule& GetGameModule();  // src/game/module.cpp

}  // namespace mc
