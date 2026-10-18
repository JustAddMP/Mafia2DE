// Adapted from JustAddMP core 8c9c0fd src/dll/mod.cpp: the per-frame tick (heartbeat, game layer, co-op panels,
// overlay). Menu-only build: no session, no crash handler, no exit hooks, no settings file.
#include "dll/mod.h"

#include "core/log.h"
#include "dll/coop.h"
#include "dll/coop_ui.h"
#include "dll/ui_input.h"
#include "game/game_module.h"
#include "render/overlay.h"
#include "render/present_hook.h"
#include "render/raw_shot.h"

#include <windows.h>
#include <cstdio>
#include <vector>

namespace mc {
namespace {

constexpr double kHeartbeatSeconds = 10.0;

struct State {
    double uptime = 0.0;
    double sinceHeartbeat = 0.0;
    unsigned long long frames = 0;
    unsigned framesSinceHeartbeat = 0;
    bool overlayVisible = false;  // the debug status text (top-left, F7); banners and panels are drawn regardless
    bool toggleKeyDown = false;
    bool shotKeyDown = false;
    std::vector<render::OverlayLine> status;
    float minDt = 1e9f;
    float maxDt = 0.0f;
    bool panelWasOpen = false;
};

State _state;

bool GameInForeground() {
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

// True once per press of vk.
bool KeyPressed(int vk, bool& wasDown) {
    const bool down = (ui_input::RealAsyncKeyState(vk) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

// F7 toggles the debug status text, F9 saves the frame as the game drew it (render/raw_shot.h); only while this
// game's window is in the foreground.
void PollKeys() {
    const bool f7 = KeyPressed(VK_F7, _state.toggleKeyDown);
    const bool f9 = KeyPressed(VK_F9, _state.shotKeyDown);
    if (!(f7 || f9) || !GameInForeground()) return;
    if (f7) Mod::SetDebugOverlay(!_state.overlayVisible, false);
    if (f9) render::RequestRawShot();
}

void StatusLines(std::vector<render::OverlayLine>& out) {
    const GameInfo& gi = GetGameInfo();
    unsigned w = 0, h = 0;
    render::Overlay::BackbufferSize(&w, &h);
    char line[160];
    std::snprintf(line, sizeof line, "%s %s - co-op menu preview (no networking)", gi.modName, gi.modVersion);
    out.push_back({line, 0xFFD1AC62});
    std::snprintf(line, sizeof line, "backbuffer %ux%u, frame %llu, up %.0f s", w, h, _state.frames, _state.uptime);
    out.push_back({line, 0xFFE9E5DB});
    out.push_back({"F8: CO-OP panel   F7: this text   F9: raw screenshot", 0xFFA8A398});
}

} // namespace

void Mod::Init() {
    MC_LOG_INFO("mod: debug status text hidden at start (F7 toggles)");
    Coop::Init();
    CoopUi::Init();
    MC_LOG_INFO("mod: initialized");
}

void Mod::Tick(float dt) {
    if (_state.frames == 0) MC_LOG_INFO("mod: first Tick");
    ++_state.frames;
    ++_state.framesSinceHeartbeat;
    _state.uptime += dt;
    _state.sinceHeartbeat += dt;
    if (_state.frames > 1) {
        if (dt < _state.minDt) _state.minDt = dt;
        if (dt > _state.maxDt) _state.maxDt = dt;
    }
    if (_state.sinceHeartbeat >= kHeartbeatSeconds) {
        unsigned w = 0, h = 0;
        render::Overlay::BackbufferSize(&w, &h);
        const double fps = _state.framesSinceHeartbeat / _state.sinceHeartbeat;
        MC_LOG_INFO("heartbeat: uptime %.0f s, frames %llu, fps %.1f, frame time min %.1f ms max %.1f ms, "
                    "backbuffer %ux%u, panel %s",
                    _state.uptime, _state.frames, fps, _state.minDt * 1000.0f, _state.maxDt * 1000.0f, w, h,
                    CoopUi::PanelOpen() ? "open" : "closed");
        _state.sinceHeartbeat = 0.0;
        _state.framesSinceHeartbeat = 0;
        _state.minDt = 1e9f;
        _state.maxDt = 0.0f;
    }

    GetGameModule().RenderTick(dt);
    Coop::Tick(dt);
    render::Overlay::Track(render::GameSwapChain());
    CoopUi::Tick(dt);
    const bool open = CoopUi::PanelOpen();
    if (open != _state.panelWasOpen) {
        _state.panelWasOpen = open;
        if (open) MC_LOG_INFO("mod: panel state: %s", CoopUi::Describe().c_str());
    }

    PollKeys();
    _state.status.clear();
    if (_state.overlayVisible) StatusLines(_state.status);
    render::Overlay::Draw(render::GameSwapChain(), _state.status, CoopUi::Draw());
}

void Mod::OnBeforeResize() { render::Overlay::ReleaseTargets(); }

void Mod::OnDeviceLost() { render::Overlay::ReleaseAll(); }

bool Mod::DebugOverlay() { return _state.overlayVisible; }

void Mod::SetDebugOverlay(bool on, bool) {
    _state.overlayVisible = on;
    MC_LOG_INFO("mod: debug status text %s", on ? "shown" : "hidden");
}

void Mod::OnResize(unsigned width, unsigned height) { MC_LOG_INFO("mod: resized to %ux%u", width, height); }

} // namespace mc
