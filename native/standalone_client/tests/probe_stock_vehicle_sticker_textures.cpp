// Real D3D9Ex probe. Run in the logged-in Windows session; no visible window.
#include "stock_vehicle_stickers_data.hpp"
#include "stock_space_decals_texture.hpp"
#include <d3dx9.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
unsigned readWord(std::istream& source)
{
    unsigned char data[4];
    assert(source.read(reinterpret_cast<char*>(data), 4));
    return unsigned(data[0]) | (unsigned(data[1]) << 8) | (unsigned(data[2]) << 16) | (unsigned(data[3]) << 24);
}
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(NULL);
    windowClass.lpszClassName = L"Original Vehicle Sticker Texture Probe";
    assert(RegisterClassW(&windowClass));
    HWND window = CreateWindowW(windowClass.lpszClassName, L"Original Vehicle Sticker Texture Probe", WS_POPUP,
        0, 0, 64, 64, NULL, NULL, windowClass.hInstance, NULL);
    assert(window);
    IDirect3D9Ex* api = NULL;
    HRESULT result = Direct3DCreate9Ex(D3D_SDK_VERSION, &api);
    if (FAILED(result)) { std::fprintf(stderr, "Direct3DCreate9Ex failed: %08lx; an interactive session is required\n", result); return 2; }
    D3DPRESENT_PARAMETERS parameters = {};
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.BackBufferWidth = parameters.BackBufferHeight = 64;
    parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    IDirect3DDevice9Ex* device = NULL;
    result = api->CreateDeviceEx(0, D3DDEVTYPE_HAL, window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &parameters, NULL, &device);
    if (FAILED(result)) { std::fprintf(stderr, "Actual HAL device creation failed: %08lx\n", result); api->Release(); return 3; }
    const unsigned count = readWord(input);
    unsigned views = 0;
    for (unsigned index = 0; index < count; ++index)
    {
        const unsigned nameSize = readWord(input), byteCount = readWord(input);
        std::string name(nameSize, '\0');
        std::vector<unsigned char> bytes(byteCount);
        assert(input.read(&name[0], nameSize) && input.read(reinterpret_cast<char*>(bytes.data()), byteCount));
        IDirect3DTexture9* source = NULL;
        result = D3DXCreateTextureFromFileInMemoryEx(device, bytes.data(), byteCount, D3DX_DEFAULT, D3DX_DEFAULT,
            D3DX_DEFAULT, 0, name.find("@memory:") == 0 ? D3DFMT_A8R8G8B8 : D3DFMT_UNKNOWN, D3DPOOL_SYSTEMMEM, D3DX_FILTER_BOX|D3DX_FILTER_MIRROR, D3DX_FILTER_BOX|D3DX_FILTER_MIRROR, 0, NULL, NULL, &source);
        if (FAILED(result)) { std::fprintf(stderr, "Cannot load original DDS %s: %08lx\n", name.c_str(), result); return 4; }
        D3DSURFACE_DESC original;
        assert(SUCCEEDED(source->GetLevelDesc(0, &original)));
        assert(original.Format == (name.find("@memory:") == 0 ? D3DFMT_A8R8G8B8 : D3DFMT_DXT5));
        assert(original.Width >= 64 && original.Height >= 64 && original.Width <= 512 && original.Height <= 512);
        const auto uv = wot::stockVehicleStickerTextureUV(original.Width, original.Height);
        assert(uv[0]*original.Width == original.Width-1.5f && uv[1]*original.Height == original.Height-1.5f);
        assert(uv[2]*original.Width == .5f && uv[3]*original.Height == .5f);
        for (unsigned skip : {0u})
        {
            IDirect3DTexture9* view = NULL;
            const unsigned levels = 5;
            result = wot::copyStockSpaceDecalMipTexture(device, source, skip, levels, &view);
            if (FAILED(result)) { std::fprintf(stderr, "Original DDS mip upload failed %s skip %u: %08lx\n", name.c_str(), skip, result); return 5; }
            assert(view && view->GetLevelCount() == levels);
            for (unsigned level = 0; level < levels; ++level)
            {
                D3DSURFACE_DESC description;
                assert(SUCCEEDED(view->GetLevelDesc(level, &description)));
                assert(description.Width == (original.Width >> (level + skip)) &&
                    description.Height == (original.Height >> (level + skip)) &&
                    description.Format == original.Format && description.Pool == D3DPOOL_DEFAULT);
            }
            IDirect3DTexture9* unchanged = view;
            assert(FAILED(wot::copyStockSpaceDecalMipTexture(device, source, source->GetLevelCount(), 1, &unchanged)) && unchanged == view);
            assert(FAILED(wot::copyStockSpaceDecalMipTexture(device, source, 1, 0xffffffffu, &unchanged)) && unchanged == view);
            assert(FAILED(wot::copyStockSpaceDecalMipTexture(device, view, 0, 1, &unchanged)) && unchanged == view);
            view->Release();
            ++views;
        }
        source->Release();
    }
    assert(input.peek() == std::char_traits<char>::eof());
    // Every default-pool view and surface has retired before resetting the
    // actual private device. No other process or shared renderer is touched.
    assert(SUCCEEDED(device->ResetEx(&parameters, NULL)));
    device->Release(); api->Release();
    DestroyWindow(window); UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    std::printf("Actual D3D9Ex: %u original DDS resources, %u mip views, transactional failures and private device reset PASS\n", count, views);
    return 0;
}
