// The Mafia II: Definitive Edition game layer for the menu-only build: no hooks into the game itself yet. It opens
// the CO-OP panel on F8 and names the lobby's (not yet available) mode. Menu entries in the game's own menus come
// later (docs/PLAN.md).
#include "game/game_module.h"

#include "core/log.h"
#include "dll/ui_input.h"

#include <windows.h>

namespace mc::game {
namespace {

constexpr int kOpenKey = VK_F8;

class Mafia2Module final : public GameModule {
public:
    void Init(const std::wstring&, const std::string& logTag) override {
        MC_LOG_INFO("game: Mafia II: Definitive Edition menu-only layer (log tag '%s'); F8 opens the CO-OP panel",
                    logTag.c_str());
    }

    void RenderTick(float) override {
        // F8 down edge, only while this game's window is in front (two instances side by side: only the focused
        // one reacts). The real key state: our GetAsyncKeyState hook blocks the game's own reads, not ours.
        const bool down = (ui_input::RealAsyncKeyState(kOpenKey) & 0x8000) != 0;
        const bool pressed = down && !_keyDown;
        _keyDown = down;
        if (!pressed) return;
        HWND fg = GetForegroundWindow();
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (pid != GetCurrentProcessId()) return;
        MC_LOG_INFO("game: F8 pressed: open the CO-OP panel");
        _openPanel = true;
    }

    bool PopMenuCommand(MenuCommand* cmd) override {
        if (!_openPanel) return false;
        _openPanel = false;
        *cmd = MenuCommand::PausePanel;
        return true;
    }

    void LobbyModes(std::vector<LobbyMode>& modes) override {
        modes.push_back({"FREE RIDE", false, "NOT AVAILABLE YET"});
    }

private:
    bool _keyDown = false;
    bool _openPanel = false;
};

}  // namespace
}  // namespace mc::game

namespace mc {
GameModule& GetGameModule() {
    static game::Mafia2Module module;
    return module;
}
}  // namespace mc
