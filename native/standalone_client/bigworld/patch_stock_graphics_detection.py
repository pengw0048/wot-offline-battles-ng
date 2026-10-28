#!/usr/bin/env python3
"""Connect the #1513 graphics detector to device startup and Python."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    path = root / 'lib/moo/graphics_settings_picker.cpp'
    source = path.read_text(encoding='utf-8-sig')
    include = '#include "stock_graphics_detection.hpp"'
    if include not in source:
        anchor = '#include "graphics_settings_picker.hpp"'
        if source.count(anchor) != 1:
            raise ValueError('Original graphics detector include changed')
        source = source.replace(anchor, anchor + '\n' + include, 1)
    publication = ('\tconfigureStockGraphicsDetection( pConfig,\n'
                   '\t\tBWResource::instance().openSection( s_autoConfigSettingsFileName.value() ) );')
    if publication not in source:
        anchor = '\tinitConfig();\n\treturn pConfig.exists();'
        if source.count(anchor) != 1:
            raise ValueError('Original graphics detector profile publication changed')
        source = source.replace(anchor, '\tinitConfig();\n' + publication + '\n\treturn pConfig.exists();', 1)
    device = root / 'client/device_app.cpp'
    device_source = device.read_text(encoding='utf-8-sig')
    include = '#include "stock_graphics_detection.hpp"'
    if include not in device_source:
        anchor = '#include "device_app.hpp"'
        if device_source.count(anchor) != 1: raise ValueError('DeviceApp include changed')
        device_source = device_source.replace(anchor, anchor + '\n' + include, 1)
    missing_preferences = '    if (!preferences && !Moo::GraphicsSetting::init(DataSectionPtr())) return false;'
    if missing_preferences not in device_source:
        anchor = '    // load graphics settings\n'
        if device_source.count(anchor) != 1: raise ValueError('Graphics preferences startup changed')
        device_source = device_source.replace(anchor, missing_preferences + '\n\n' + anchor, 1)
    old_start = ("    if (!renderer_->init(isAssetProcessor, isSupportedDS))\n"
                 '    {\n        ERROR_MSG("DeviceApp::init()  Could not initialize renderer\'s pipeline.\\n");\n'
                 "        return false;\n    }")
    new_start = "    renderer_->prepareStockGraphicsStartup(isSupportedDS);"
    if new_start not in device_source:
        if device_source.count(old_start) != 1: raise ValueError('Renderer startup changed')
        device_source = device_source.replace(old_start, new_start, 1)
        device_source = device_source.replace('    bool isAssetProcessor = false;\n', '', 1)
    renderer = root / 'lib/moo/renderer.cpp'
    renderer_source = renderer.read_text(encoding='utf-8-sig')
    renderer_header = root / 'lib/moo/renderer.hpp'
    renderer_header_source = renderer_header.read_text(encoding='utf-8-sig')
    declaration = ('    void prepareStockGraphicsStartup(bool supportDS);\n'
                   '    bool finishStockGraphicsStartup();\n'
                   '    bool stockGraphicsStartupPending() const { return m_stockGraphicsStartupPending; }')
    if declaration not in renderer_header_source:
        anchor = '    bool                        init(bool isAssetProcessor = false, bool supportDS = false);'
        if renderer_header_source.count(anchor) != 1: raise ValueError('Renderer header changed')
        renderer_header_source = renderer_header_source.replace(anchor, anchor + '\n' + declaration, 1)
        anchor = '    bool                                        m_inited;'
        if renderer_header_source.count(anchor) != 1: raise ValueError('Renderer state changed')
        renderer_header_source = renderer_header_source.replace(anchor, anchor + '\n    bool m_stockGraphicsStartupPending = false;\n    bool m_stockSupportDS = false;', 1)
    if '#include "stock_graphics_detection.hpp"' not in renderer_source:
        anchor = '#include "renderer.hpp"'
        if renderer_source.count(anchor) != 1: raise ValueError('Renderer include changed')
        renderer_source = renderer_source.replace(anchor, anchor + '\n#include "stock_graphics_detection.hpp"', 1)
    marker = 'void Renderer::prepareStockGraphicsStartup(bool supportDS)'
    if marker not in renderer_source:
        anchor = 'bool Renderer::init(bool isAssetProcessor, bool supportDS)'
        if renderer_source.count(anchor) != 1: raise ValueError('Renderer init changed')
        methods = r'''void Renderer::prepareStockGraphicsStartup(bool supportDS)
{
    if (g_pipelineMacroSetting->options().empty())
    {
        g_pipelineMacroSetting->addOption("DEFERRED", "Deferred shading", supportDS, "1");
        g_pipelineMacroSetting->addOption("FORWARD", "Forward shading", true, "0");
        Moo::GraphicsSetting::add(g_pipelineMacroSetting);
    }
    if (!supportDS && g_pipelineMacroSetting->activeOption() == 0)
        g_pipelineMacroSetting->stockForceActiveOption(1);
    m_stockSupportDS = supportDS;
    m_stockGraphicsStartupPending = true;
}

bool Renderer::finishStockGraphicsStartup()
{
    finishStockGraphicsDetectionStartup();
    if (!m_stockSupportDS && g_pipelineMacroSetting->activeOption() == 0)
        g_pipelineMacroSetting->stockForceActiveOption(1);
    Moo::EffectMacroSetting::updateManagerInfix();
    const bool result = this->init(false, m_stockSupportDS);
    m_stockGraphicsStartupPending = false;
    return result;
}

'''
        renderer_source = renderer_source.replace(anchor, methods + anchor, 1)
        original = ('    g_pipelineMacroSetting->addOption("DEFERRED", "Deferred shading", supportDS, "1");\n'
                    '    g_pipelineMacroSetting->addOption("FORWARD",  "Forward shading", true, "0");\n'
                    '    Moo::GraphicsSetting::add(g_pipelineMacroSetting);')
        if renderer_source.count(original) != 1: raise ValueError('Renderer pipeline registration changed')
        replacement = ('    if (g_pipelineMacroSetting->options().empty())\n    {\n' +
                       '\n'.join('    ' + line for line in original.splitlines()) + '\n    }')
        renderer_source = renderer_source.replace(original, replacement, 1)
    original_pipeline = ('IRendererPipeline::EType Renderer::pipelineType() const\n'
                         '{\n    BW_GUARD;\n    MF_ASSERT(m_inited);')
    early_pipeline = ('IRendererPipeline::EType Renderer::pipelineType() const\n'
                      '{\n    BW_GUARD;\n'
                      '    if (m_stockGraphicsStartupPending && !m_inited)\n'
                      '        return g_pipelineMacroSetting->activeOption() == 0 ?\n'
                      '            IRendererPipeline::TYPE_DEFERRED_SHADING : IRendererPipeline::TYPE_FORWARD_SHADING;\n'
                      '    MF_ASSERT(m_inited);')
    if early_pipeline not in renderer_source:
        if renderer_source.count(original_pipeline) != 1: raise ValueError('Renderer pipeline type changed')
        renderer_source = renderer_source.replace(original_pipeline, early_pipeline, 1)
    context = root / 'lib/moo/render_context.cpp'
    context_source = context.read_text(encoding='utf-8-sig')
    if '#include "renderer.hpp"' not in context_source:
        anchor = '#include "render_context.hpp"'
        if context_source.count(anchor) != 1: raise ValueError('Render context include changed')
        context_source = context_source.replace(anchor, anchor + '\n#include "renderer.hpp"', 1)
    hook = ('        if (Renderer::pInstance() && Renderer::instance().stockGraphicsStartupPending() &&\n'
            '            !Renderer::instance().finishStockGraphicsStartup()) return false;')
    if hook not in context_source:
        anchor = '        DeviceCallback::createAllManaged();'
        if context_source.count(anchor) != 1: raise ValueError('Device callback startup changed')
        context_source = context_source.replace(anchor, hook + '\n\n' + anchor, 1)
    py = root / 'lib/romp/py_graphics_setting.cpp'
    py_source = py.read_text(encoding='utf-8-sig')
    if include not in py_source:
        anchor = '#include "py_graphics_setting.hpp"'
        if py_source.count(anchor) != 1: raise ValueError('Graphics Python include changed')
        py_source = py_source.replace(anchor, anchor + '\n' + include, 1)
    settings_include = '#include "stock_graphics_settings.hpp"'
    if settings_include not in py_source:
        py_source = py_source.replace(include, include + '\n' + settings_include, 1)
    old = ('\tMoo::GraphicsSettingsAutoDetector detector;\n\tdetector.init();\n'
           '\treturn (int)detector.detectBestSettings();')
    new = '\treturn autoDetectStockGraphicsSettings();'
    if new not in py_source:
        if py_source.count(old) != 1: raise ValueError('Graphics Python auto detect changed')
        py_source = py_source.replace(old, new, 1)
    application = Path(__file__).resolve().parent
    copies = ((application / 'stock_graphics_detection.hpp', root / 'lib/stock_graphics_detection.hpp'),
              (application.parent / 'include/wot/stock_graphics_detection.hpp', root / 'lib/wot/stock_graphics_detection.hpp'),
              (application / 'stock_gpu_detection.hpp', root / 'lib/stock_gpu_detection.hpp'),
              (application.parent / 'include/wot/stock_gpu_sampling.hpp', root / 'lib/wot/stock_gpu_sampling.hpp'),
              (application.parent / 'include/wot/stock_gpu_performancer.hpp', root / 'lib/wot/stock_gpu_performancer.hpp'))
    contents = [(destination, original.read_bytes()) for original, destination in copies]
    path.write_text(source, encoding='utf-8')
    device.write_text(device_source, encoding='utf-8')
    renderer.write_text(renderer_source, encoding='utf-8')
    renderer_header.write_text(renderer_header_source, encoding='utf-8')
    context.write_text(context_source, encoding='utf-8')
    py.write_text(py_source, encoding='utf-8')
    for destination, data in contents:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
