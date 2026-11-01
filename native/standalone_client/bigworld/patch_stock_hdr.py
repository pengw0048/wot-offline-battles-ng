#!/usr/bin/env python3
"""Drive the original HDR luminance and ToneMapping effects from public HDRSupport."""
import argparse
from pathlib import Path


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError('Unexpected public engine source for ' + label)
    return text.replace(old, new, 1)


SELECT_OLD = 'ID3DXEffect* effect = m_averageLuminanceMat->pEffect()->pEffect();\n'
SELECT_NEW = ('ID3DXEffect* effect = m_averageLuminanceMat->properties().select(*m_averageLuminanceMat->pEffect()) ?\n'
              '            m_averageLuminanceMat->pEffect()->pEffect() : NULL;\n')

TONEMAPPING = '''void HDRSupport::doTonemapping()
{
    BW_GUARD;
    BW_SCOPED_DOG_WATCHER("tone mapping");
    MF_ASSERT( m_bloomMaps.size() );

    if (!drawStockToneMapping(m_toneMappingMat, g_cfg, m_finalAvgLumRT.get(), m_hdrRT.get(),
            m_bloomMaps[0].second.get(), m_stockColorCorrection))
        ERROR_MSG("HDR: original tone mapping failed\\n");
}
'''


def function(text, signature):
    start = text.index(signature)
    depth, index = 0, text.index('{', start)
    while True:
        depth += {'{': 1, '}': -1}.get(text[index], 0)
        index += 1
        if depth == 0:
            return text[start:index]


def patch_settings(root, pending):
    path = root / 'lib/romp/hdr_settings.hpp'
    text = path.read_text()
    text = replace_once(text, '\t\tfloat m_whitePoint;\n\t};',
        '\t\tfloat m_whitePoint;\n\t\tfloat m_blackPoint;\n\t};', 'HDR tonemapping black point')
    text = replace_once(text, '\tToneMapping\t\tm_tonemapping;\n',
        '\tToneMapping\t\tm_tonemapping;\n'
        '\t// #1513 a6f440: three profiles; m_tonemapping mirrors the active one.\n'
        '\tToneMapping\t\tm_tonemappings[3];\n'
        '\tint\t\t\t\tm_activeToneMapping;\n'
        '\tBW::string\t\tm_colorCorrectionMap;\n', 'HDR tonemapping profiles')
    pending[path] = text

    path = root / 'lib/romp/hdr_settings.cpp'
    text = path.read_text()
    text = replace_once(text, '#include "hdr_settings.hpp"\n',
        '#include "hdr_settings.hpp"\n#include "stock_hdr.hpp"\n', 'HDR settings include')
    # a6f330's original defaults.
    text = replace_once(text, '        m_whitePoint(1.5f)\n',
        '        m_whitePoint(5.0f),\n        m_blackPoint(0.15f)\n', 'HDR tonemapping defaults')
    text = replace_once(text, '        m_avgLumUpdateInterval(0.25f)\n',
        '        m_avgLumUpdateInterval(0.25f),\n        m_activeToneMapping(1)\n', 'HDR active profile default')
    old = '''    //-- tone mapping parameters.
    {
        ToneMapping& t = m_tonemapping;

        t.m_eyeDarkLimit            = iData->readFloat  ("tonemapping/eyeDarkLimit", t.m_eyeDarkLimit);
        t.m_eyeLightLimit           = iData->readFloat  ("tonemapping/eyeLightLimit", t.m_eyeLightLimit);
        t.m_middleGray              = iData->readFloat  ("tonemapping/middleGray", t.m_middleGray);
        t.m_whitePoint              = iData->readFloat  ("tonemapping/whitePoint", t.m_whitePoint);
    }
'''
    new = '''    //-- tone mapping parameters.
    loadStockToneMappings(iData, *this);
    m_colorCorrectionMap = iData->readString("colorCorrection/map", m_colorCorrectionMap);
'''
    text = replace_once(text, old, new, 'HDR tonemapping reader')
    # a6f0db..a6f185 keep public's storage, but the second slot is the
    # sunlight multiplier and the fourth the sun multiplier (no fog).
    old = '''        e.m_skyLumMultiplier        = iData->readFloat  ("environment/skyLumMultiplier", e.m_skyLumMultiplier);
        e.m_sunLumMultiplier        = iData->readFloat  ("environment/sunLumMultiplier", e.m_sunLumMultiplier);
        e.m_ambientLumMultiplier    = iData->readFloat  ("environment/ambientLumMultiplier", e.m_ambientLumMultiplier);
        //-- Note: for backward capability we use sun luminance multiplier as fallback, in case if\x20
        //--       fog luminance multiplier is not presented in cfg.
        e.m_fogLumMultiplier        = iData->readFloat  ("environment/fogLumMultiplier", m_environment.m_sunLumMultiplier);
'''
    new = '''        e.m_skyLumMultiplier        = iData->readFloat  ("environment/skyLumMultiplier", e.m_skyLumMultiplier);
        e.m_ambientLumMultiplier    = iData->readFloat  ("environment/ambientLumMultiplier", e.m_ambientLumMultiplier);
        e.m_sunLumMultiplier        = m_version < 2 ?
            iData->readFloat("environment/sunLumMultiplier", e.m_fogLumMultiplier) :
            iData->readFloat("environment/sunlightLumMultiplier", e.m_sunLumMultiplier);
        e.m_fogLumMultiplier        = iData->readFloat  ("environment/sunLumMultiplier", e.m_fogLumMultiplier);
'''
    text = replace_once(text, old, new, 'HDR environment multipliers')
    pending[path] = text


def patch_support(root, pending):
    path = root / 'lib/romp/hdr_support.hpp'
    text = path.read_text()
    text = replace_once(text, '\tMoo::EffectMaterialPtr\t\t\t\tm_toneMappingMat;\n',
        '\tMoo::EffectMaterialPtr\t\t\t\tm_toneMappingMat;\n'
        '\tMoo::BaseTexturePtr\t\t\t\tm_stockColorCorrection;\n', 'HDR colour correction owner')
    pending[path] = text

    path = root / 'lib/romp/hdr_support.cpp'
    text = path.read_text()
    text = replace_once(text, '#include "hdr_support.hpp"\n',
        '#include "hdr_support.hpp"\n#include "stock_hdr.hpp"\n', 'HDR support include')
    # a70300 publishes (1,1,1,1) while HDR is disabled.
    text = replace_once(text, '            Vector4 params(0.0f, 1.0f, 1.0f, 1.0f);\n',
        '            Vector4 params(1.0f, 1.0f, 1.0f, 1.0f);\n', 'HDR disabled environment multipliers')
    # A frame whose deferred preparation fails skips intercept; its resolve
    # must not pop the previous frame's HDR target.
    text = replace_once(text, '        //-- set main back buffer render target as current.\n        m_hdrRT->pop();\n',
        '        //-- set main back buffer render target as current.\n        m_successed = false;\n        m_hdrRT->pop();\n',
        'HDR one pop per intercept')
    text = replace_once(text, '    g_cfg = cfg ? *cfg : HDRSettings();\n',
        '    g_cfg = cfg ? *cfg : HDRSettings();\n'
        '    m_stockColorCorrection = loadStockColorCorrection(g_cfg.m_colorCorrectionMap);\n',
        'HDR colour correction load')
    # average_luminance.fx and tonemapping.fx resolve to the original FXOs,
    # whose techniques are named as a70500/a70a20 select them.
    for old, new in (
            ('m_averageLuminanceMat->hTechnique("DOWN_SAMPLE_COLOR");',
             'm_averageLuminanceMat->hTechnique("DownsampleColor");'),
            ('m_averageLuminanceMat->hTechnique(g_cfg.m_useLogAvgLuminance ? "LOG_AVERAGE_LUM" : "AVERAGE_LUM");',
             'm_averageLuminanceMat->hTechnique(g_cfg.m_useLogAvgLuminance ? "AverageLogLum" : "AverageLum");'),
            ('m_averageLuminanceMat->hTechnique("DOWN_SAMPLE_LUM");',
             'm_averageLuminanceMat->hTechnique("DownsampleLum");'),
            ('m_averageLuminanceMat->hTechnique(g_cfg.m_useLogAvgLuminance ? "FINAL_AVERAGE_LOG_LUM" : "FINAL_AVERAGE_LUM");',
             'm_averageLuminanceMat->hTechnique(g_cfg.m_useLogAvgLuminance ? "FinalAverageLogLum" : "FinalAverageLum");'),
            ('effect->SetTexture("g_previousMipMap", m_hdrRT->pTexture());',
             'setStockHdrTarget(effect, "g_previousMipMap", m_hdrRT.get());'),
            ('effect->SetTexture("g_previousMipMap", downSampledHDRRT->pTexture());',
             'setStockHdrTarget(effect, "g_previousMipMap", downSampledHDRRT);'),
            ('effect->SetTexture("g_previousMipMap", m_avgLumRTs[i - 1]->pTexture());',
             'setStockHdrTarget(effect, "g_previousMipMap", m_avgLumRTs[i - 1].get());'),
            ('"shaders/hdr/hdr_tonemapping.fx"', '"shaders/hdr/tonemapping.fx"')):
        text = replace_once(text, old, new, 'HDR original effect: ' + new)
    # End releases the ARIE binding, so values written before the next begin
    # would miss this material's own binding; select it first, as water does.
    if SELECT_NEW not in text:
        if text.count(SELECT_OLD) != 4:
            raise RuntimeError('Unexpected public engine source for HDR luminance bindings')
        for indent in ('        ', '            '):
            text = text.replace(indent + SELECT_OLD + indent + '{\n',
                                indent + SELECT_NEW + indent + 'if (effect)\n' + indent + '{\n')
        if SELECT_OLD in text or text.count(SELECT_NEW) != 4:
            raise RuntimeError('Unexpected public engine source for HDR luminance blocks')
    if TONEMAPPING not in text:
        text = text.replace(function(text, 'void HDRSupport::doTonemapping()\n'), TONEMAPPING.rstrip('\n'), 1)
    pending[path] = text


def patch(root):
    root = Path(root)
    application = Path(__file__).resolve().parent
    copies = ((application / 'stock_hdr.hpp', root / 'lib/stock_hdr.hpp'),
              (application.parent / 'include/wot/stock_hdr_tonemapping.hpp',
               root / 'lib/wot/stock_hdr_tonemapping.hpp'))
    contents = [(destination, source.read_bytes()) for source, destination in copies]
    pending = {}
    patch_settings(root, pending)
    patch_support(root, pending)
    for path, text in pending.items():
        path.write_text(text)
    for destination, data in contents:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
