#pragma once
#include <d3d9.h>

namespace wot
{
// 13f5310 uploads complete source mip(skip+i) into destination mip(i).
// Publication is transactional: a failed copy leaves the caller's output
// unchanged and releases every temporary surface/texture reference.
inline HRESULT copyStockSpaceDecalMipTexture(IDirect3DDevice9* device, IDirect3DTexture9* source,
    unsigned skip, unsigned levels, IDirect3DTexture9** output)
{
    if (!device || !source || !output || !levels) return D3DERR_INVALIDCALL;
    const unsigned available = source->GetLevelCount();
    if (skip > available || levels > available - skip) return D3DERR_INVALIDCALL;
    D3DSURFACE_DESC description;
    HRESULT result = source->GetLevelDesc(skip, &description);
    if (FAILED(result)) return result;
    if (description.Pool != D3DPOOL_SYSTEMMEM) return D3DERR_INVALIDCALL;
    IDirect3DTexture9* candidate = NULL;
    result = device->CreateTexture(description.Width, description.Height, levels, 0,
        description.Format, D3DPOOL_DEFAULT, &candidate, NULL);
    if (FAILED(result)) return result;
    for (unsigned level = 0; level < levels; ++level)
    {
        IDirect3DSurface9* input = NULL;
        IDirect3DSurface9* destination = NULL;
        result = source->GetSurfaceLevel(skip + level, &input);
        if (SUCCEEDED(result)) result = candidate->GetSurfaceLevel(level, &destination);
        if (SUCCEEDED(result)) result = device->UpdateSurface(input, NULL, destination, NULL);
        if (destination) destination->Release();
        if (input) input->Release();
        if (FAILED(result)) { candidate->Release(); return result; }
    }
    *output = candidate;
    return S_OK;
}
}
