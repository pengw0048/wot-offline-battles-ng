"""Connect Water impulses to the #1513 simulation techniques and material owner."""
from pathlib import Path
import argparse


def replace_once(text, before, after):
    if after in text:
        return text
    if text.count(before) != 1:
        raise RuntimeError('Water ripple consumer anchor changed: ' + repr(before))
    return text.replace(before, after)


def patch(root):
    root = Path(root)
    pending = {}
    path = root / 'lib/romp/water.cpp'
    text = path.read_text()
    text = replace_once(text,
        '    simulationEffect_->hTechnique( "water_simulation" );',
        '    simulationEffect_->hTechnique( "WaterSimulation" );')
    text = replace_once(text,
        '    ComObjectWrap<ID3DXEffect> pEffect = simulationEffect_->pEffect()->pEffect();',
        '    if (!simulationEffect_->properties().select(*simulationEffect_->pEffect()))\n'
        '    {\n        simulationWatch.stop();\n        Moo::rc().popRenderState();\n        return;\n    }\n'
        '    ComObjectWrap<ID3DXEffect> pEffect = simulationEffect_->pEffect()->pEffect();')
    # loadResourcesComplete only primes g_cellInfo, which renderSimulation
    # uploads each frame before the first block/impulse draw. Select its real
    # material too; a failed selection skips that optional priming write.
    before = '''        if (selfWater && Waters::s_simulationEffect_)
        {
            ComObjectWrap<ID3DXEffect> pEffect = Waters::s_simulationEffect_->pEffect()->pEffect();'''
    after = '''        if (selfWater && Waters::s_simulationEffect_ &&
            Waters::s_simulationEffect_->properties().select(*Waters::s_simulationEffect_->pEffect()))
        {
            ComObjectWrap<ID3DXEffect> pEffect = Waters::s_simulationEffect_->pEffect()->pEffect();'''
    text = replace_once(text, before, after)

    # The parameter cache retains an ID3DXEffect, whereas ARIE's Properties
    # owns the actual per-material binding. Select before any surface writes,
    # including the first draw and a recompiled/replaced effect.
    before = '''    if (!cells_.size() || !createdCells_ || (nVertices_==0))
        return;\x20

    static DogWatch waterWatch( "Water" );'''
    after = '''    if (!cells_.size() || !createdCells_ || (nVertices_==0))
        return;

    Waters::s_effect_->checkEffectRecompiled();
    if (!Waters::s_effect_->properties().select(*Waters::s_effect_->pEffect()))
        return;
    paramCache_->effect(Waters::s_effect_->pEffect()->pEffect());

    static DogWatch waterWatch( "Water" );'''
    text = replace_once(text, before, after)
    text = replace_once(text, '        if (selfWater && Waters::s_effect_)',
        '        if (selfWater && Waters::s_effect_ &&\n'
        '            Waters::s_effect_->properties().select(*Waters::s_effect_->pEffect()))')
    # A cell's manual parameters are consumed by BeginPass. CommitChanges
    # between cells has no open pass, and a failed Begin/BeginPass must never
    # issue a draw using a previous shader.
    before = '''                if (bound)
                    Waters::s_effect_->commitChanges();
                else
                {\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20
                    bound=true;
                    Waters::s_effect_->begin();
                }'''
    after = '''                if (!bound)
                {
                    if (!Waters::s_effect_->begin()) break;
                    bound=true;
                }'''
    text = replace_once(text, before, after)
    text = replace_once(text, '                    Waters::s_effect_->beginPass(pass);',
        '                    if (!Waters::s_effect_->beginPass(pass)) continue;')

    # These textures are explicitly supplied by Water, not automatic semantic
    # textures. Release the existing binding's default-pool targets before the
    # owner destroys/recreates them; do not allocate a binding during teardown.
    helper = '''static void releaseStockWaterRenderTargets(Moo::EffectMaterialPtr material,
    bool simulation)
{
    if (!material || !material->pEffect()) return;
    ID3DXEffect* effect = material->properties().existingStockBinding();
    if (!effect) effect = material->pEffect()->pEffect();
    if (!effect) return;
    static const char* const simulationTargets[] = {
        "heightMap", "previousHeightMap", "neighbourHeightMap0",
        "neighbourHeightMap1", "neighbourHeightMap2", "neighbourHeightMap3"
    };
    static const char* const surfaceTargets[] = {
        "bbCopy", "simulationMap", "rainSimulationMap", "reflectionMap"
    };
    const char* const* names = simulation ? simulationTargets : surfaceTargets;
    const size_t count = simulation ? sizeof(simulationTargets)/sizeof(*simulationTargets) :
        sizeof(surfaceTargets)/sizeof(*surfaceTargets);
    for (size_t index = 0; index != count; ++index)
    {
        D3DXHANDLE handle = effect->GetParameterByName(NULL, names[index]);
        if (handle) effect->SetTexture(handle, NULL);
    }
}

'''
    text = replace_once(text, 'Water::~Water()\n{\n    BW_GUARD;',
        helper + 'Water::~Water()\n{\n    BW_GUARD;\n'
        '    releaseStockWaterRenderTargets(Waters::s_effect_, false);\n'
        '    releaseStockWaterRenderTargets(Waters::s_simulationEffect_, true);')
    text = replace_once(text, 'void Water::deleteUnmanagedObjects( )\n{\n    BW_GUARD;',
        'void Water::deleteUnmanagedObjects( )\n{\n    BW_GUARD;\n'
        '    releaseStockWaterRenderTargets(Waters::s_effect_, false);\n'
        '    releaseStockWaterRenderTargets(Waters::s_simulationEffect_, true);\n'
        '    paramCache_->effect(NULL);')
    pending[path] = text

    path = root / 'lib/romp/water_simulation.cpp'
    text = path.read_text()
    # a7e46f/a7e49e: the propagation technique and quality flag replace the
    # public source's removed perturb/edge integer shader selectors.
    text = replace_once(text, '\tpEffect->SetInt( "perturb", 0 );',
        '\tpEffect->SetBool( "g_highQuality", Waters::highQualityMode() );')
    # The old once-only write assumed a permanent D3DX effect. A newly
    # allocated/recompiled ARIE material starts from authored defaults instead.
    text = replace_once(text, '''#ifndef EDITOR_ENABLED
\tstatic bool once = true;

\tif (once)
\t{
\t\tonce = false;
#endif''', '\t// Republish cell constants to the current material binding.\n\t{')
    text = replace_once(text, '''#ifndef EDITOR_ENABLED
\t}
#endif
\t//TODO: cleanup the variable uploads...''',
        '\t}\n\t//TODO: cleanup the variable uploads...')
    # No pass has begun here; BeginPass commits these values below.
    text = replace_once(text,
        '\tpEffect->SetBool( "g_highQuality", Waters::highQualityMode() );\n\tpEffect->CommitChanges();',
        '\tpEffect->SetBool( "g_highQuality", Waters::highQualityMode() );\n'
        '\t// BeginPass uploads the current simulation uniforms.')
    text = replace_once(text, '\t\t\tpEffect->SetInt("perturb", 1);',
        '\t\t\teffect->hTechnique("WaterSimulationPerturb");')
    # 00a7d831 restores WaterSimulation after the impulse pass; later cells
    # must propagate their height fields rather than inject another impulse.
    before = '\t\teffect->end();   \n\t}\n}\n\n\n// -----------------------------------------------------------------------------\n// Section: Simulation'
    after = '\t\teffect->end();\n\t\teffect->hTechnique("WaterSimulation");\n\t}\n}\n\n\n// -----------------------------------------------------------------------------\n// Section: Simulation'
    text = replace_once(text, before, after)
    rain_selector = '\tpEffect->SetInt( "edge", 0 );'
    rain_replacement = ('\t// Rain has no cell neighbours; clear a prior edge cell\'s flags.\n' +
        ''.join('\tpEffect->SetBool("%s", false);\n' % name
                for name in ('Y0_enabled', 'X0_enabled', 'Y2_enabled', 'X2_enabled')).rstrip())
    previous_rain = '\t// #1513 selects edge behaviour through neighbour booleans and techniques.'
    if previous_rain in text:
        text = replace_once(text, previous_rain, rain_replacement)
    if rain_replacement not in text:
        if text.count(rain_selector) != 2:
            raise RuntimeError('Water simulation edge selector sites changed')
        text = text.replace(rain_selector, rain_replacement, 1)
    text = replace_once(text, '\t\tpEffect->SetInt( "edge", edge_ );',
        '\t\t// #1513 has no integer edge shader selector.')
    # The non-edge branch must clear neighbour booleans left by a prior cell.
    text = replace_once(text, '\telse\n\t\tpEffect->SetInt( "edge", 0 );',
        '\telse\n\t{\n'
        '\t\tpEffect->SetBool("Y0_enabled", false);\n'
        '\t\tpEffect->SetBool("X0_enabled", false);\n'
        '\t\tpEffect->SetBool("Y2_enabled", false);\n'
        '\t\tpEffect->SetBool("X2_enabled", false);\n\t}')
    # Native uniforms are bools; SetInt would fail against the typed ARIE
    # reflection even when the public D3DX implementation coerced it.
    for name in ('Y0_enabled', 'X0_enabled', 'Y2_enabled', 'X2_enabled'):
        text = replace_once(text, '\t\tpEffect->SetInt( "%s", %s );' % (name, name[:2].lower()),
            '\t\tpEffect->SetBool( "%s", %s != 0 );' % (name, name[:2].lower()))
    before = 'ComObjectWrap<ID3DXEffect> pEffect = effect->pEffect()->pEffect();'
    after = ('if (!effect->properties().select(*effect->pEffect())) return;\n'
             '\tComObjectWrap<ID3DXEffect> pEffect = effect->pEffect()->pEffect();')
    # Exactly five reviewed entry points own the simulation material reads:
    # drawMovements, simulateRain, bindAsNeighbour, simulate, stitchEdges.
    selected = text.count(after)
    if selected == 0:
        if text.count(before) != 5:
            raise RuntimeError('Water simulation material entry points changed')
        text = text.replace(before, after)
    elif selected != 5:
        raise RuntimeError('Water simulation material patch is incomplete')
    text = replace_once(text, '\teffect->begin();\n\teffect->beginPass(0);',
        '\tif (!effect->begin()) return;\n'
        '\tif (!effect->beginPass(0)) { effect->end(); return; }')
    text = replace_once(text, '\t\t\teffect->begin();\n\t\t\teffect->beginPass(0);',
        '\t\t\tif (!effect->begin())\n'
        '\t\t\t{ effect->hTechnique("WaterSimulation"); return; }\n'
        '\t\t\tif (!effect->beginPass(0))\n'
        '\t\t\t{ effect->end(); effect->hTechnique("WaterSimulation"); return; }')
    text = replace_once(text, '\t\tpEffect->CommitChanges();\n\t\tMoo::rc().drawPrimitive',
        '\t\tif (FAILED(pEffect->CommitChanges())) continue;\n\t\tMoo::rc().drawPrimitive')
    pending[path] = text
    for path, text in pending.items():
        path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
