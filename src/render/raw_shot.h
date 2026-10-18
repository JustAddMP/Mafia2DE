#pragma once
// Raw frame capture for screenshots: copies the game's backbuffer from inside the Present hook, before the real
// Present runs. That is the picture the game (plus our overlay) drew, before anything chained after our hook
// (ReShade or other post-processing injectors that wrap the swapchain) changes it. Written as a 24-bit BMP to
// <log dir>\shots\raw-<n>.bmp. F9 requests one (only while the game window is in the foreground).
struct IDXGISwapChain;

namespace mc::render {

void RequestRawShot();
// Render thread, from the Present hook after the frame callback: writes the pending shot, if any.
void CaptureRawShotIfRequested(IDXGISwapChain* sc);

} // namespace mc::render
