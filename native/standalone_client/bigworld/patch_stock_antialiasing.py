#!/usr/bin/env python3
"""Connect the original AA device configuration to its public renderer owner."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    pending = {}

    def edit(relative, before, after, present=None):
        text = pending.get(relative)
        if text is None:
            text = (root / relative).read_text()
        if (present or after) in text:
            return
        if text.count(before) != 1:
            raise ValueError('Antialiasing owner anchor changed: ' + relative + ': ' + repr(before))
        pending[relative] = text.replace(before, after, 1)

    header = 'lib/moo/render_context.hpp'
    edit(header, '#define RENDER_CONTEXT_HPP',
         '#define RENDER_CONTEXT_HPP\n#include "wot/stock_antialiasing.hpp"',
         '#include "wot/stock_antialiasing.hpp"')
    edit(header, '\t\tbool\t\t\t\t\t\tisMultisamplingEnabled() const;',
         '\t\tbool\t\t\t\t\t\tisMultisamplingEnabled() const;\n'
         '\t\tuint32 stockMultisamplingType() const { return stockMultisampling_.type; }\n'
         '\t\tuint32 stockMultisamplingQuality() const { return stockMultisampling_.quality; }\n'
         '\t\tvoid restoreStockMultisampling(uint32 type, uint32 quality) { stockMultisampling_.type=type; stockMultisampling_.quality=quality; stockMultisampling_.pending=false; }\n'
         '\t\tvoid requestStockMultisampling(uint32 type) { stockMultisampling_.request(type); }\n'
         '\t\tbool applyStockMultisampling();', 'bool applyStockMultisampling();')
    edit(header, '\t\tD3DPRESENT_PARAMETERS\t\tpresentParameters_;',
         '\t\tD3DPRESENT_PARAMETERS\t\tpresentParameters_;\n'
         '\t\twot::StockMultisamplingSelection stockMultisampling_;',
         'wot::StockMultisamplingSelection stockMultisampling_;')
    cpp = 'lib/moo/render_context.cpp'
    edit(cpp, '#include "pch.hpp"',
         '#include "pch.hpp"\n#include "stock_antialiasing.hpp"',
         '#include "stock_antialiasing.hpp"')
    edit(cpp, '        customAA_.reset(new CustomAA());',
         '        // The #1513 deferred pipeline owns the native FXAA/TAA algorithms.',
         '        // The #1513 deferred pipeline owns the native FXAA/TAA algorithms.')
    edit(cpp, '        effectVisualContext_->init();',
         '        effectVisualContext_->init();\n'
         '        initialiseStockAntialiasing();',
         '        initialiseStockAntialiasing();')
    edit(cpp, 'void RenderContext::fini()\n{\n    BW_GUARD;',
         'void RenderContext::fini()\n{\n    BW_GUARD;\n'
         '    finaliseStockAntialiasing();',
         '    finaliseStockAntialiasing();')
    edit(cpp, '    presentParameters_.MultiSampleType = D3DMULTISAMPLE_NONE;',
         '    // #1513 b2e2b0: decode the desired enum only at the actual device boundary.\n'
         '    const auto stockSamples = wot::stockD3D9Multisampling(\n'
         '        stockMultisampling_.type, stockMultisampling_.quality,\n'
         '        devices_[deviceIndex_].identifier_.VendorId == 0x10de);\n'
         '    presentParameters_.MultiSampleType = static_cast<D3DMULTISAMPLE_TYPE>(stockSamples.samples);\n'
         '    presentParameters_.MultiSampleQuality = stockSamples.quality;',
         '    presentParameters_.MultiSampleQuality = stockSamples.quality;')
    edit(cpp, '        deviceEx_->ResetEx( &presentParameters_, pFullScreenDesc );',
         '        const HRESULT stockResetResult = deviceEx_->ResetEx( &presentParameters_, pFullScreenDesc );\n'
         '        if (FAILED(stockResetResult))\n'
         '        {\n'
         '            printErrorMsg_(stockResetResult, "Moo::RenderContext::changeMode: Unable to reset device");\n'
         '            return false;\n'
         '        }', '        const HRESULT stockResetResult = deviceEx_->ResetEx(')
    edit(cpp, '''bool RenderContext::isMultisamplingEnabled() const
{
    return false;//(multisamplingType_ != D3DMULTISAMPLE_NONE && multisamplingQuality_ >= 0);
}''', '''bool RenderContext::applyStockMultisampling()
{
    if (!stockMultisampling_.pending) return true;
    if (!device_) return false;
    // Resource callbacks may synchronously request another mode. Consume this
    // request before reset and preserve any new request admitted during it.
    stockMultisampling_.pending = false;
    const bool result = resetDevice();
    if (!result) stockMultisampling_.pending = true;
    return result;
}

bool RenderContext::isMultisamplingEnabled() const
{
    return presentParameters_.MultiSampleType != D3DMULTISAMPLE_NONE;
}''', 'bool RenderContext::applyStockMultisampling()')
    cpp = 'client/device_app.cpp'
    edit(cpp, '#include "device_app.hpp"',
         '#include "device_app.hpp"\n#include "stock_deferred_buffers.hpp"',
         '#include "stock_deferred_buffers.hpp"')
    edit(cpp, '    // load graphics settings',
         '    uint32 stockCustomAAMode = 0;\n'
         '    uint32 stockMultisamplingType = 0;\n'
         '    uint32 stockMultisamplingQuality = 0;\n\n'
         '    // load graphics settings', '    uint32 stockCustomAAMode = 0;')
    edit(cpp, '        Moo::GraphicsSetting::init(graphicsPreferences);',
         '        Moo::GraphicsSetting::init(graphicsPreferences);\n'
         '        // 61feb0 reads custom AA from graphics, MSAA from device preferences.\n'
         '        if (graphicsPreferences)\n'
         '            stockCustomAAMode = graphicsPreferences->readUInt("customAAMode", 0);',
         '            stockCustomAAMode = graphicsPreferences->readUInt(')
    edit(cpp, '            // limit width and height',
         '            stockMultisamplingType = static_cast<uint32>(devicePreferences->readInt("multisamplingType", 0));\n'
         '            stockMultisamplingQuality = static_cast<uint32>(devicePreferences->readInt("multisamplingQuality", 0));\n\n'
         '            // limit width and height',
         '            stockMultisamplingType = static_cast<uint32>(devicePreferences->readInt(')
    edit(cpp, '    Moo::rc().tripleBuffering(tripleBuffering);',
         '    Moo::rc().tripleBuffering(tripleBuffering);\n'
         '    // Seed desired settings before the first device, without queuing a reset.\n'
         '    Moo::rc().restoreStockMultisampling(stockMultisamplingType, stockMultisamplingQuality);',
         '    Moo::rc().restoreStockMultisampling(')
    edit(cpp, '#if SCALEFORM_SUPPORT\n    pScaleFormBWManager_ = ScaleFormBWManagerPtr(',
         '    // 6218db applies the saved custom mode only after device creation.\n'
         '    if (auto* stockAAOwner = stockDeferredBuffers())\n'
         '        stockAAOwner->customAAMode(stockCustomAAMode);\n\n'
         '#if SCALEFORM_SUPPORT\n    pScaleFormBWManager_ = ScaleFormBWManagerPtr(',
         '        stockAAOwner->customAAMode(stockCustomAAMode);')
    edit(cpp, '        Moo::GraphicsSetting::write(grPref);',
         '        Moo::GraphicsSetting::write(grPref);\n'
         '        // 65a970: Forward has no CustomAA owner and writes no field.\n'
         '        if (const auto* stockAAOwner = stockDeferredBuffers())\n'
         '        {\n'
         '            grPref->writeUInt("customAAMode", stockAAOwner->customAAMode());\n'
         '        }',
         '            grPref->writeUInt("customAAMode",')
    edit(cpp, '        devPref->writeBool( "tripleBuffering",   Moo::rc().tripleBuffering());',
         '        devPref->writeBool( "tripleBuffering",   Moo::rc().tripleBuffering());\n'
         '        devPref->writeUInt("multisamplingType", Moo::rc().stockMultisamplingType());\n'
         '        devPref->writeUInt("multisamplingQuality", Moo::rc().stockMultisamplingQuality());',
         '        devPref->writeUInt("multisamplingQuality",')
    edit(cpp, '    Moo::rc().nextFrame();',
         '    if (!Moo::rc().applyStockMultisampling())\n'
         '        ERROR_MSG("Antialiasing: native multisampling device reset failed\\n");\n'
         '    Moo::rc().nextFrame();',
         '    if (!Moo::rc().applyStockMultisampling())')
    cpp = 'lib/moo/deferred_pipeline.cpp'
    edit(cpp, '#include "deferred_pipeline.hpp"',
         '#include "deferred_pipeline.hpp"\n#include "stock_deferred_buffers.hpp"',
         '#include "stock_deferred_buffers.hpp"')
    edit(cpp, '    if (g_clearGBuffer)',
         '    if (!m_stockDeferredBuffers->beginOpaqueVelocity())\n'
         '        ERROR_MSG("Antialiasing: native velocity target could not be bound\\n");\n\n'
         '    if (g_clearGBuffer)',
         '    if (!m_stockDeferredBuffers->beginOpaqueVelocity())')
    cpp = 'client/lens_app.cpp'
    edit(cpp, '#include "moo/renderer.hpp"',
         '#include "moo/renderer.hpp"\n#include "stock_deferred_buffers.hpp"',
         '#include "stock_deferred_buffers.hpp"')
    edit(cpp, '    Moo::rc().customAA().ppaa().apply();',
         '    if (!finishStockAntialiasingScene())\n'
         '        ERROR_MSG("Antialiasing: native scene resolve failed\\n");',
         '    if (!finishStockAntialiasingScene())')
    cpp = 'lib/romp/hdr_support.cpp'
    edit(cpp, '#include "pch.hpp"',
         '#include "pch.hpp"\n#include "stock_deferred_buffers.hpp"',
         '#include "stock_deferred_buffers.hpp"')
    edit(cpp, '        calcAvgLumOnGPU();',
         '        calcAvgLumOnGPU();\n'
         '        // 99f5b0 resolves 16F colour history into this actual HDR target.\n'
         '        if (!resolveStockTemporalHDR(m_hdrRT, m_finalAvgLumRT->pTexture(),\n'
         '                g_cfg.m_tonemapping.m_middleGray, g_cfg.m_tonemapping.m_whitePoint))\n'
         '            ERROR_MSG("Antialiasing: native HDR temporal resolve failed\\n");',
         '        if (!resolveStockTemporalHDR(m_hdrRT, m_finalAvgLumRT->pTexture(),')
    # Public RenderContext/EnviroMinder consumers do not all link the renderer
    # targets. Publish their adapter headers with the public engine headers.
    application = Path(__file__).resolve().parent
    headers = ((application / 'stock_antialiasing.hpp', root / 'lib/stock_antialiasing.hpp'),
               (application / 'stock_temporal_antialiasing.hpp', root / 'lib/stock_temporal_antialiasing.hpp'),
               (application.parent / 'include/wot/stock_antialiasing.hpp',
                root / 'lib/wot/stock_antialiasing.hpp'))
    sources = [(destination, source.read_text()) for source, destination in headers]
    # Validate every anchor and source before publishing any translation unit.
    for relative, text in pending.items():
        path = root / relative
        if path.read_text() != text:
            path.write_text(text)
    for destination, text in sources:
        if not destination.exists() or destination.read_text() != text:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
