#!/usr/bin/env python3
"""Connect #1513 Movie/BitmapData commands to the actual Scaleform render owner."""
import argparse
from pathlib import Path

HEADER = 'lib/scaleform/manager.hpp'
SOURCE = 'lib/scaleform/manager.cpp'
MOVIE = 'lib/scaleform/py_movie_def.cpp'
HELPER = 'stock_flash_drawable_images.hpp'

EXECUTOR = r'''// DrawableImage may flush synchronously while AS3 advances outside a frame.
// HALConfig_NoSceneCalls leaves the actual D3D scene to Moo's nested owner.
static void executeStockFlashDrawableImageCommand(Render::ThreadCommand* command)
{
    IDirect3DDevice9* device = Moo::rc().device();
    IDirect3DStateBlock9* state = NULL;
    IDirect3DSurface9* targets[4] = {};
    bool targetSaved[4] = {};
    IDirect3DSurface9* depth = NULL;
    D3DVIEWPORT9 viewport = {};
    D3DCAPS9 caps = {};
    const bool capsSaved = device && SUCCEEDED(device->GetDeviceCaps(&caps));
    const unsigned targetCount = capsSaved ? (caps.NumSimultaneousRTs < 4 ? caps.NumSimultaneousRTs : 4) : 0;
    if (device && !capsSaved) ERROR_MSG("Scaleform: cannot query DrawableImage device caps\n");
    const bool stateSaved = device && SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL, &state)) &&
        state && SUCCEEDED(state->Capture());
    if (device && !stateSaved) ERROR_MSG("Scaleform: cannot capture DrawableImage device state\n");
    const bool viewportSaved = device && SUCCEEDED(device->GetViewport(&viewport));
    if (device && !viewportSaved) ERROR_MSG("Scaleform: cannot capture DrawableImage viewport\n");
    const HRESULT depthResult = device ? device->GetDepthStencilSurface(&depth) : E_FAIL;
    const bool depthSaved = SUCCEEDED(depthResult) || depthResult == D3DERR_NOTFOUND;
    if (device && !depthSaved) ERROR_MSG("Scaleform: cannot capture DrawableImage depth (%08lx)\n", depthResult);
    for (unsigned i = 0; i < targetCount; ++i)
    {
        const HRESULT result = device->GetRenderTarget(i, &targets[i]);
        targetSaved[i] = SUCCEEDED(result) || result == D3DERR_NOTFOUND;
        if (!targetSaved[i]) ERROR_MSG("Scaleform: cannot capture DrawableImage target %u (%08lx)\n", i, result);
    }
    // beginScene increments its nesting counter even when BeginScene fails.
    const HRESULT scene = device ? Moo::rc().beginScene() : S_OK;
    if (FAILED(scene)) ERROR_MSG("Scaleform: DrawableImage BeginScene failed (%08lx)\n", scene);
    // Completion events and the SDK's extra references belong to Execute itself.
    // In particular, do not discard a command when the device is lost.
    command->Execute();
    if (device && FAILED(Moo::rc().endScene()))
        ERROR_MSG("Scaleform: cannot end DrawableImage scene\n");
    if (stateSaved && FAILED(state->Apply()))
        ERROR_MSG("Scaleform: cannot restore DrawableImage device state\n");
    if (device)
    {
        // MRTs are restored before depth, which can depend on their dimensions.
        if (FAILED(device->SetDepthStencilSurface(NULL)))
            ERROR_MSG("Scaleform: cannot detach DrawableImage depth\n");
        for (unsigned i = 0; i < 4; ++i)
            if (targetSaved[i] && FAILED(device->SetRenderTarget(i, targets[i])))
                ERROR_MSG("Scaleform: cannot restore DrawableImage target %u\n", i);
        if (depthSaved && FAILED(device->SetDepthStencilSurface(depth)))
            ERROR_MSG("Scaleform: cannot restore DrawableImage depth\n");
        if (viewportSaved && FAILED(device->SetViewport(&viewport)))
            ERROR_MSG("Scaleform: cannot restore DrawableImage viewport\n");
    }
    for (unsigned i = 0; i < 4; ++i) if (targets[i]) targets[i]->Release();
    if (depth) depth->Release();
    if (state) state->Release();
    if (device) Moo::rc().invalidateStateCache();
}

'''

SEAMS = (
    (HEADER, '#include "py_movie_view.hpp"', '#include "py_movie_view.hpp"\n#include "stock_flash_drawable_images.hpp"'),
    (HEADER, '\t\tLoader* pLoader()', '\t\tRender::ThreadCommandQueue* drawableImageQueue() { return &drawableImageQueue_; }\n\t\tLoader* pLoader()'),
    (HEADER, '\t\tGFx::System system_;\n', '\t\tGFx::System system_;\n\t\tStockFlashDrawableImageQueue drawableImageQueue_;\n'),
    (SOURCE, '\tManager::Manager() :\n', EXECUTOR + '\tManager::Manager() :\n\t\tdrawableImageQueue_(&executeStockFlashDrawableImageCommand),\n'),
    (SOURCE, 'pRenderHAL_ = *new Render::D3D9::HAL();', 'pRenderHAL_ = *new Render::D3D9::HAL(&drawableImageQueue_);'),
    (SOURCE, '\t\tpLoader_ = new Loader();', '\t\tdrawableImageQueue_.configure(Render::Interfaces(pRenderHAL_->GetTextureManager(),\n\t\t\tpRenderHAL_.GetPtr(), pRenderer_.GetPtr(), pRenderHAL_->GetRenderThreadId()));\n\n\t\tpLoader_ = new Loader();'),
    (SOURCE, '\t\tpRenderHAL_ = NULL;\n', '\t\tpRenderHAL_ = NULL;\n\t\tdrawableImageQueue_.configure(Render::Interfaces());\n'),
    (MOVIE, 'GFx::Movie *movieView = pMovieDef_->CreateInstance( initFirstFrame );',
     'GFx::Movie *movieView = pMovieDef_->CreateInstance( initFirstFrame, 0, NULL,\n\t\t\tManager::instance().drawableImageQueue() );\n\t\tif (!movieView)\n\t\t{\n\t\t\tPyErr_SetString(PyExc_Exception, "GFx internally failed.");\n\t\t\treturn NULL;\n\t\t}'),
)
FILES = (HEADER, SOURCE, MOVIE)


def patch_sources(sources):
    final = all(sources[name].count(after) == 1 and sources[name].count(before) == after.count(before)
                for name, before, after in SEAMS)
    if final: return dict(sources)
    if not all(sources[name].count(before) == 1 and sources[name].count(after) == 0
               for name, before, after in SEAMS):
        raise ValueError('Pinned Scaleform DrawableImage queue seams are partial, duplicated or changed')
    result = dict(sources)
    for name, before, after in SEAMS:
        result[name] = result[name].replace(before, after, 1)
    return result


def patch(root):
    root = Path(root)
    sources = {name:(root/name).read_text(encoding='utf-8-sig') for name in FILES}
    result = patch_sources(sources)
    helper = Path(__file__).with_name(HELPER).read_text()
    for name in FILES:
        if result[name] != sources[name]: (root/name).write_text(result[name], encoding='utf-8')
    (root/'lib/scaleform'/HELPER).write_text(helper, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
