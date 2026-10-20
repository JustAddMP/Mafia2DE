// The Mafia II: Definitive Edition game layer for the menu-only build: the CO-OP entries and pages in the game's own
// main and pause menus (game/menu_patch.h, served by game/sds_redirect.h), and - as a fallback for testing - F8
// opens core's CO-OP panel drawn by our overlay.
#include "game/game_module.h"

#include "core/log.h"
#include "dll/ui_input.h"
#include "game/sds_redirect.h"
#include "game/update_notice.h"

#include <windows.h>

namespace mc::game {
namespace {

constexpr int kOpenKey = VK_F8;

class Mafia2Module final : public GameModule {
public:
    void Init(const std::wstring& dllDir, const std::string& logTag) override {
        MC_LOG_INFO("game: Mafia II: Definitive Edition menu-only layer (log tag '%s')", logTag.c_str());
        // The CO-OP entries in the game's own main and pause menus (game/menu_patch.h): the game opens patched
        // copies of its two menu archives. Must be in place before the game loads its menus.
        sds_redirect::Install(dllDir);
        // The update check (game/update_notice.h): started now so that its answer is in by the time the game opens
        // its menu archives, which is when the notice goes into the CO-OP pages.
        update_notice::Start();
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
