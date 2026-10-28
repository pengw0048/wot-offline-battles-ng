"""Connect original WGSD2 decals to their real four deferred target phases."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    pending = {}

    def edit(path, before, after, present=None):
        source = pending.get(path, (root / path).read_text(encoding='utf-8-sig'))
        if source.count(present or after) == 1:
            return
        if source.count(before) != 1:
            raise ValueError('Pinned WGSD2 owner seam changed: ' + path + ': ' + repr(before))
        pending[path] = source.replace(before, after, 1)

    manager = 'lib/moo/deferred_decals_manager.cpp'
    edit(manager, '#include "deferred_decals_manager.hpp"',
         '#include "deferred_decals_manager.hpp"\n#include "stock_space_decals.hpp"', '#include "stock_space_decals.hpp"')
    edit(manager, 'DecalsManager::~DecalsManager()\n{',
         'DecalsManager::~DecalsManager()\n{\n    finaliseStockSpaceDecals();',
         '    finaliseStockSpaceDecals();')
    # Later providers extend this return expression. Only the call inside the
    # pinned init function establishes that this owner was installed.
    current = pending.get(manager, (root / manager).read_text(encoding='utf-8-sig'))
    signature = 'bool DecalsManager::init()\n{'
    if current.count(signature) != 1:
        raise ValueError('Pinned WGSD2 init owner changed: ' + manager)
    body = current.split(signature, 1)[1].split('\n}', 1)[0]
    init_present = body.count('initialiseStockSpaceDecals()') == 1
    if not init_present:
        edit(manager, '''    m_dynamicDecalsManager.reset(new DynamicDecalManager());

    return true;''', '''    m_dynamicDecalsManager.reset(new DynamicDecalManager());

    return initialiseStockSpaceDecals();''')
    edit(manager, 'void DecalsManager::setQualityOption(int option)\n{\n    BW_GUARD;',
         'void DecalsManager::setQualityOption(int option)\n{\n    BW_GUARD;\n'
         '    setStockSpaceDecalsQuality(option);')

    pipeline = 'lib/moo/deferred_pipeline.cpp'
    edit(pipeline, '#include "deferred_pipeline.hpp"',
         '#include "deferred_pipeline.hpp"\n#include "stock_space_decals.hpp"', '#include "stock_space_decals.hpp"')
    edit(pipeline, '    m_decalsManager->draw();', '''    // a90760 phase 0 writes the actual original GBuffer channel order.
    if (!m_stockDeferredBuffers->drawSpaceDecals())
        ERROR_MSG("WGSD2: original GBuffer decal drawing failed\\n");
    m_decalsManager->draw();''')
    edit(pipeline, '    m_lightsManager->draw();', '''    m_lightsManager->draw();
    // a90760 phase 3 writes ENV_MAPPING into the intercepted HDR target.
    if (!drawStockSpaceDecalsPhase(3))
        ERROR_MSG("WGSD2: original environment decal drawing failed\\n");''')
    edit(pipeline, '    m_dynamicShadowManager->receive();', '''    // a91980 phase 1 draws into the completed DBE full/half target.
    if (!m_stockDeferredBuffers->drawSpaceDecalsAO())
        ERROR_MSG("WGSD2: original ambient-occlusion decal drawing failed\\n");
    m_dynamicShadowManager->receive();''')

    shadows = 'lib/moo/shadow_manager.cpp'
    edit(shadows, '#include "shadow_manager.hpp"',
         '#include "shadow_manager.hpp"\n#include "stock_space_decals.hpp"', '#include "stock_space_decals.hpp"')
    edit(shadows, '                m_screenSpaceShadowMap->pop();', '''                // a91980 phase 2 consumes the enabled SUN resolve owner.
                // This target is independent of local-light VSM and DBE.B.
                if (!drawStockSpaceDecalsPhase(2))
                    ERROR_MSG("WGSD2: original SUN shadow decal drawing failed\\n");
                m_screenSpaceShadowMap->pop();''')

    # Validate every pinned seam before publishing any file.
    header = Path(__file__).with_name('stock_space_decals.hpp').read_bytes()
    for path, source in pending.items():
        (root / path).write_text(source, encoding='utf-8')
    (root / 'lib/stock_space_decals.hpp').write_bytes(header)
    print('Original WGSD2 decal renderer lifecycle and actual target phases installed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
