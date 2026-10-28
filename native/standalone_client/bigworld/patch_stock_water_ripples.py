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
    pending[path] = text

    path = root / 'lib/romp/water_simulation.cpp'
    text = path.read_text()
    # a7e46f/a7e49e: the propagation technique and quality flag replace the
    # public source's removed perturb/edge integer shader selectors.
    text = replace_once(text, '\tpEffect->SetInt( "perturb", 0 );',
        '\tpEffect->SetBool( "g_highQuality", Waters::highQualityMode() );')
    text = replace_once(text, '\t\t\tpEffect->SetInt("perturb", 1);',
        '\t\t\teffect->hTechnique("WaterSimulationPerturb");')
    # 00a7d831 restores WaterSimulation after the impulse pass; later cells
    # must propagate their height fields rather than inject another impulse.
    before = '\t\teffect->end();   \n\t}\n}\n\n\n// -----------------------------------------------------------------------------\n// Section: Simulation'
    after = '\t\teffect->end();\n\t\teffect->hTechnique("WaterSimulation");\n\t}\n}\n\n\n// -----------------------------------------------------------------------------\n// Section: Simulation'
    text = replace_once(text, before, after)
    rain_selector = '\tpEffect->SetInt( "edge", 0 );'
    rain_replacement = '\t// #1513 selects edge behaviour through neighbour booleans and techniques.'
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
    pending[path] = text
    for path, text in pending.items():
        path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
