#pragma once
// Copied from JustAddMP core 8c9c0fd src/dll/mod.h.
// The mod's per-frame entry point. Everything game-facing runs inside Tick, on the game's render thread.

namespace mc {

class Mod {
public:
    static void Init();          // called once from the init thread, before the Present hook goes live
    static void Tick(float dt);  // called once per frame from the Present hook
    static void OnResize(unsigned width, unsigned height);
    static void OnBeforeResize();  // drop backbuffer references (overlay)
    static void OnDeviceLost();    // the game's D3D device is gone: drop every overlay resource

    // The debug status text (F7). save is ignored in this build (no settings file).
    static bool DebugOverlay();
    static void SetDebugOverlay(bool on, bool save);
};

} // namespace mc
