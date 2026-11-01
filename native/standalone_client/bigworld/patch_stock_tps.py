"""Restore #1513 native tPS owners without routing Influx through Pixie quality."""
import argparse
from pathlib import Path


def replace(source, anchor, replacement):
    if replacement in source:
        return source
    if source.count(anchor) != 1:
        raise RuntimeError('The reviewed tPS owner anchor changed: ' + repr(anchor))
    return source.replace(anchor, replacement)


def patch(root):
    root = Path(root)
    pending = []
    path = root / 'lib/particle/meta_particle_system.hpp'
    source = replace(path.read_text(), '\tParticleSystems\t\tsystems_;',
                     '\tParticleSystems\t\tsystems_;\n\tuint32 inverseRenderGroups_ = 0;')
    pending.append((path, source))
    path = root / 'lib/particle/meta_particle_system.cpp'
    source = replace(path.read_text(), '#include "pch.hpp"',
                     '#include "pch.hpp"\n#include "stock_tps_quality.hpp"')
    source = replace(source, '''MetaParticleSystemPtr MetaParticleSystem::clone() const
{
    BW_GUARD;
\tMetaParticleSystemPtr result = new MetaParticleSystem();''', '''MetaParticleSystemPtr MetaParticleSystem::clone() const
{
    BW_GUARD;
    // 009e3a70 rejects disabled inverse render groups before allocating.
    if (stockTPSGroupDisabled( inverseRenderGroups_ )) return NULL;
\tMetaParticleSystemPtr result = new MetaParticleSystem();
    result->inverseRenderGroups_ = inverseRenderGroups_;''')
    source = replace(source, '''bool MetaParticleSystem::tick( float dTime )
{
\tBW_GUARD;
\tbool updated = false;''', '''bool MetaParticleSystem::tick( float dTime )
{
\tBW_GUARD;
    // 009e5b10 releases disabled systems before their next native update.
    if (stockTPSGroupDisabled( inverseRenderGroups_ )) removeAllSystems();
\tbool updated = false;''')
    source = replace(source, '\t// save cache to file\n\tpDS->save();',
                     '''    // 009e5480 writes this field. The original 009e42a0 loader
    // does not read it; restoring an XML reader would change #1513 behavior.
    pDS->writeUInt( "addition/inv_render_groups", inverseRenderGroups_ );
\t// save cache to file
\tpDS->save();''')
    pending.append((path, source))
    path = root / 'lib/particle/actions/particle_system_action.hpp'
    source = replace(path.read_text(), '\tPSA_TYPE_ID_MAX\t\t\t\t= 16',
                     '\tPSA_INFLUX_TYPE_ID\t\t\t= 16,\n\tPSA_TYPE_ID_MAX\t\t\t\t= 17')
    pending.append((path, source))
    path = root / 'lib/particle/actions/particle_system_action.cpp'
    source = replace(path.read_text(), '#include "pch.hpp"',
                     '#include "pch.hpp"\n#include "stock_tps_scene.hpp"')
    for signature, addition in (
        ('const BW::string & ParticleSystemAction::typeToName(int type)',
         '    if (type == PSA_INFLUX_TYPE_ID) return stockInfluxActionName();'),
        ('int ParticleSystemAction::nameToType(const BW::string &name)',
         '    if (name == stockInfluxActionName()) return PSA_INFLUX_TYPE_ID;'),
        ('ParticleSystemActionPtr ParticleSystemAction::createActionOfType(int type)',
         '    if (type == PSA_INFLUX_TYPE_ID) return createStockInfluxPSA();')):
        source = replace(source, signature + '\n{', signature + '\n{\n' + addition)
    source = replace(source, 'int type = pAction->typeID();',
                     'int type = pAction->typeID();\n    if (type == PSA_INFLUX_TYPE_ID) return wrapStockInfluxPSA( pAction );')
    pending.append((path, source))
    path = root / 'lib/particle/particle_system.cpp'
    source = replace(path.read_text(), '#include "pch.hpp"',
                     '#include "pch.hpp"\n#include "stock_tps_scene.hpp"')
    source = replace(source, '\tthis->clearBoundingBox();\n\treturn true;\n}',
                     '\tthis->clearBoundingBox();\n    for (const auto & action : actions_) resetStockInfluxMotion( action.getObject() );\n\treturn true;\n}')
    pending.append((path, source))
    path = root / 'client/world_app.cpp'
    source = replace(path.read_text(), '#include "pch.hpp"',
                     '#include "pch.hpp"\n#include "stock_tps_scene.hpp"')
    if '        tickStockTPSRuntime( dGameTime );' not in source:
        source = replace(source, '        GlobalEmbodiments::tick( dGameTime );',
                         '        GlobalEmbodiments::tick( dGameTime );\n        tickStockTPSRuntime( dGameTime );')
    # GasCloud refreshes the shared splitter before the real tPS quad producer.
    source = source.replace('        tickStockTPSRuntime( dGameTime );\n        tickStockBattlegroundElements();',
                            '        tickStockBattlegroundElements();\n        tickStockTPSRuntime( dGameTime );')
    if source.count('        tickStockTPSRuntime( dGameTime );') != 1:
        raise RuntimeError('The tPS frame owner must tick exactly once')
    # DrawContext::drawUserItem rejects items after end(), even if their
    # transparent flush has not run yet. Queue both tPS streams while World
    # still collects; Facade retains the actual transparent/Gas draw phase.
    source = replace(source, '    colourDrawContext.end( ',
                     '    drawStockTPSRuntime();\n    colourDrawContext.end( ')
    pending.append((path, source))
    path = root / 'client/facade_app.cpp'
    source = replace(path.read_text(), '#include "pch.hpp"',
                     '#include "pch.hpp"\n#include "stock_tps_scene.hpp"')
    flush = '        App::instance().drawContext( App::COLOUR_DRAW_CONTEXT ).flush( Moo::DrawContext::TRANSPARENT_CHANNEL_MASK );'
    if source.count(flush) != 1:
        raise RuntimeError('The tPS transparent flush owner changed')
    # Upgrade an already patched SDK as well as a pristine checkout.
    source = source.replace('        drawStockTPSRuntime();\n', '')
    pending.append((path, source))
    path = root / 'client/script_app.cpp'
    source = replace(path.read_text(), '#include "pch.hpp"',
                     '#include "pch.hpp"\n#include "stock_tps_scene.hpp"')
    source = replace(source, '\tBigWorldClientScript::fini();',
                     '\tfiniStockTPSRuntime();\n\tBigWorldClientScript::fini();')
    pending.append((path, source))
    for path, source in pending:
        if path.read_text() != source:
            path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
