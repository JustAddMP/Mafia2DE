#include "render/raw_shot.h"

#include "core/log.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace mc::render {
namespace {

std::atomic<bool> _pending{false};
int _counter = 0;

template <class T>
void SafeRelease(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

bool WriteBmp(const std::wstring& path, const std::vector<unsigned char>& bgr, unsigned w, unsigned h) {
    const unsigned stride = (w * 3 + 3) & ~3u;
    BITMAPFILEHEADER fh{};
    BITMAPINFOHEADER ih{};
    ih.biSize = sizeof(ih);
    ih.biWidth = static_cast<LONG>(w);
    ih.biHeight = -static_cast<LONG>(h);  // top-down
    ih.biPlanes = 1;
    ih.biBitCount = 24;
    ih.biCompression = BI_RGB;
    ih.biSizeImage = stride * h;
    fh.bfType = 0x4D42;
    fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    bool ok = fwrite(&fh, sizeof(fh), 1, f) == 1 && fwrite(&ih, sizeof(ih), 1, f) == 1 &&
              fwrite(bgr.data(), 1, bgr.size(), f) == bgr.size();
    fclose(f);
    return ok;
}

void Capture(IDXGISwapChain* sc) {
    ID3D11Texture2D* back = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) {
        MC_LOG_WARN("raw shot: GetBuffer failed");
        return;
    }
    D3D11_TEXTURE2D_DESC d{};
    back->GetDesc(&d);
    back->GetDevice(&device);
    device->GetImmediateContext(&ctx);
    const bool rgba = d.Format == DXGI_FORMAT_R8G8B8A8_UNORM || d.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    const bool bgra = d.Format == DXGI_FORMAT_B8G8R8A8_UNORM || d.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    if (!rgba && !bgra) {
        MC_LOG_WARN("raw shot: backbuffer format %d not supported", static_cast<int>(d.Format));
    } else if (d.SampleDesc.Count != 1) {
        MC_LOG_WARN("raw shot: multisampled backbuffer not supported");
    } else {
        D3D11_TEXTURE2D_DESC sd = d;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        if (SUCCEEDED(device->CreateTexture2D(&sd, nullptr, &staging))) {
            ctx->CopyResource(staging, back);
            D3D11_MAPPED_SUBRESOURCE m{};
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
                const unsigned w = d.Width, h = d.Height, stride = (w * 3 + 3) & ~3u;
                std::vector<unsigned char> bgr(static_cast<size_t>(stride) * h, 0);
                for (unsigned y = 0; y < h; ++y) {
                    const unsigned char* src = static_cast<const unsigned char*>(m.pData) + static_cast<size_t>(y) * m.RowPitch;
                    unsigned char* dst = bgr.data() + static_cast<size_t>(y) * stride;
                    for (unsigned x = 0; x < w; ++x, src += 4, dst += 3) {
                        dst[0] = rgba ? src[2] : src[0];
                        dst[1] = src[1];
                        dst[2] = rgba ? src[0] : src[2];
                    }
                }
                ctx->Unmap(staging, 0);
                const std::wstring dir = core::log::Dir() + L"\\shots";
                CreateDirectoryW(dir.c_str(), nullptr);
                wchar_t name[32];
                swprintf_s(name, L"\\raw-%d.bmp", _counter++);
                const std::wstring path = dir + name;
                if (WriteBmp(path, bgr, w, h)) {
                    MC_LOG_INFO("raw shot: %ux%u fmt=%d -> %ls", w, h, static_cast<int>(d.Format), path.c_str());
                } else {
                    MC_LOG_WARN("raw shot: writing %ls failed", path.c_str());
                }
            } else {
                MC_LOG_WARN("raw shot: Map failed");
            }
        } else {
            MC_LOG_WARN("raw shot: staging texture creation failed");
        }
    }
    SafeRelease(staging);
    SafeRelease(ctx);
    SafeRelease(device);
    SafeRelease(back);
}

} // namespace

void RequestRawShot() { _pending = true; }

void CaptureRawShotIfRequested(IDXGISwapChain* sc) {
    if (!sc || !_pending.exchange(false)) return;
    Capture(sc);
}

} // namespace mc::render
