// menu_nav: the co-op panels of the menu-only build without the game: the hotkey opens and closes the CO-OP panel,
// its entries (HOST, JOIN FRIEND, JOIN BY IP, SET PASSWORD, LOBBY, SETTINGS, BACK), keyboard navigation into each
// sub-panel and back, the JOIN BY IP text box, the settings rows, and that HOST / CONNECT only report "not available
// yet" (no session). Keys are injected as the window's messages would be (dll/ui_input.h InjectEvent).
// Exits non-zero on any failure.
#include "dll/coop.h"
#include "dll/coop_ui.h"
#include "dll/mod.h"
#include "dll/ui_input.h"
#include "game/game_module.h"

#include <windows.h>
#include <cstdio>
#include <string>

namespace {

int g_failures = 0;

void Check(bool ok, const char* name, const std::string& detail = {}) {
    std::printf("%s  %-60s %s\n", ok ? "PASS" : "FAIL", name, ok ? "" : detail.c_str());
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

// The game module: the test decides when the hotkey "was pressed".
class TestModule final : public mc::GameModule {
public:
    void Init(const std::wstring&, const std::string&) override {}
    void RenderTick(float) override {}
    bool PopMenuCommand(mc::MenuCommand* cmd) override {
        if (!hotkey) return false;
        hotkey = false;
        *cmd = mc::MenuCommand::PausePanel;
        return true;
    }
    void LobbyModes(std::vector<mc::LobbyMode>& modes) override { modes.push_back({"FREE RIDE", false, "NOT AVAILABLE YET"}); }
    bool hotkey = false;
};
TestModule g_module;

void Tick() { mc::CoopUi::Tick(1.0f / 60.0f); }

void Key(uint32_t vk) {
    mc::ui_input::InjectEvent({mc::ui_input::Event::Key, vk, 0, 0});
    Tick();
}

void Type(const std::string& s) {
    for (char c : s) mc::ui_input::InjectEvent({mc::ui_input::Event::Char, static_cast<uint32_t>(c), 0, 0});
    Tick();
}

std::string D() { return mc::CoopUi::Describe(); }
bool Has(const std::string& s) { return D().find(s) != std::string::npos; }

void Hotkey() {
    g_module.hotkey = true;
    Tick();
}

}  // namespace

namespace mc {
GameModule& GetGameModule() { return g_module; }
}  // namespace mc

int main() {
    mc::Coop::Init();
    mc::CoopUi::Init();
    Tick();
    Check(!mc::CoopUi::PanelOpen(), "nothing open at start");

    Hotkey();
    Check(mc::CoopUi::PanelOpen() && Has("panel pause CO-OP"), "hotkey opens the CO-OP panel", D());
    for (const char* label : {"HOST CO-OP GAME", "JOIN FRIEND", "JOIN BY IP", "SET PASSWORD", "LOBBY", "SETTINGS", "BACK"})
        Check(Has(std::string(" ") + label), (std::string("CO-OP panel lists ") + label).c_str(), D());
    Check(Has("[0]* HOST CO-OP GAME"), "selection starts on HOST CO-OP GAME", D());
    Key(VK_DOWN);
    Check(Has("[1]* JOIN FRIEND"), "Down moves to JOIN FRIEND", D());
    Key(VK_UP);
    Key(VK_UP);
    Check(Has("[6]* BACK"), "Up from the top wraps to BACK", D());
    Hotkey();
    Check(!mc::CoopUi::PanelOpen(), "hotkey again closes it", D());

    // HOST: closes the panel, no session
    Hotkey();
    Key(VK_RETURN);
    Check(!mc::CoopUi::PanelOpen(), "HOST closes the panel", D());
    Check(mc::Coop::GetInfo().phase == mc::Coop::Phase::Idle, "HOST starts no session");

    // JOIN FRIEND: its panel, "not available yet", BACK returns to the CO-OP panel
    Hotkey();
    Key(VK_DOWN);
    Key(VK_RETURN);
    Check(Has("panel join friend"), "JOIN FRIEND opens its panel", D());
    Check(Has(" REFRESH") && Has(" BACK"), "JOIN FRIEND lists REFRESH and BACK", D());
    Key(VK_ESCAPE);
    Check(Has("panel pause CO-OP"), "Esc on JOIN FRIEND returns to the CO-OP panel", D());

    // JOIN BY IP: text box, typing, Backspace, Enter = CONNECT (not available, closes)
    Key(VK_DOWN);  // selection is back on the first entry after reopening: HOST -> JOIN FRIEND
    Key(VK_DOWN);
    Key(VK_RETURN);
    Check(Has("panel join by IP"), "JOIN BY IP opens the text box", D());
    Type("10.0.0.79");
    Key(VK_BACK);
    Type("8");
    Key(VK_RETURN);
    Check(!mc::CoopUi::PanelOpen(), "Enter in the text box connects (closes the box)", D());
    Check(mc::Coop::GetInfo().lastAddress == "10.0.0.78", "typed address reached JoinAddress",
          mc::Coop::GetInfo().lastAddress);
    Check(mc::Coop::GetInfo().phase == mc::Coop::Phase::Idle, "CONNECT starts no session");

    // LOBBY: its panel with SETTINGS and BACK
    Hotkey();
    for (int i = 0; i < 4; ++i) Key(VK_DOWN);
    Check(Has("[4]* LOBBY"), "LOBBY is the fifth entry", D());
    Key(VK_RETURN);
    Check(Has("panel lobby") && Has(" SETTINGS") && Has(" BACK"), "LOBBY opens the lobby panel", D());
    Key(VK_ESCAPE);
    Check(!mc::CoopUi::PanelOpen(), "Esc closes the lobby", D());

    // SETTINGS: NAME and DEBUG OVERLAY rows; Enter on DEBUG OVERLAY toggles it; BACK to the CO-OP panel
    Hotkey();
    Key(VK_UP);
    Key(VK_UP);
    Check(Has("[5]* SETTINGS"), "SETTINGS is the sixth entry", D());
    Key(VK_RETURN);
    Check(Has("panel settings") && Has(" NAME = ") && Has(" DEBUG OVERLAY = OFF"), "SETTINGS lists NAME and DEBUG OVERLAY", D());
    Key(VK_DOWN);
    Key(VK_RETURN);
    // (the row's value text is rebuilt when the panel is drawn; without a backbuffer only the state changes)
    Check(mc::Mod::DebugOverlay(), "Enter on DEBUG OVERLAY turns it on", D());
    Key(VK_LEFT);
    Check(!mc::Mod::DebugOverlay(), "Left on DEBUG OVERLAY turns it off again", D());
    Key(VK_UP);
    Key(VK_RETURN);
    Check(Has("panel name"), "NAME opens the name box", D());
    Type("Vito");
    Key(VK_RETURN);
    Check(Has("panel settings") && Has("NAME = Vito"), "the name box saves into the NAME row", D());
    Key(VK_ESCAPE);
    Check(Has("panel pause CO-OP"), "Esc on SETTINGS returns to the CO-OP panel", D());

    // SET PASSWORD: box, then CHANGE / CLEAR PASSWORD
    Key(VK_DOWN);
    Key(VK_DOWN);
    Key(VK_DOWN);
    Check(Has("[3]* SET PASSWORD"), "SET PASSWORD is the fourth entry", D());
    Key(VK_RETURN);
    Check(Has("panel password"), "SET PASSWORD opens the password box", D());
    Type("pw");
    Key(VK_RETURN);
    Hotkey();
    Check(Has(" CHANGE PASSWORD") && Has(" CLEAR PASSWORD"), "after setting one: CHANGE / CLEAR PASSWORD", D());
    Key(VK_ESCAPE);
    Check(!mc::CoopUi::PanelOpen(), "Esc closes the CO-OP panel");

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASSED", g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
