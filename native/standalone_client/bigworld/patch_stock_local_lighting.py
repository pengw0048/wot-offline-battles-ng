#!/usr/bin/env python3
"""Attach native local-light volumes and the real VSM cache to LightsManager."""
import argparse
from pathlib import Path


def apply(root):
    root = Path(root)
    pending = {}

    def source(relative):
        return pending.get(relative, (root / relative).read_text(encoding='utf-8-sig'))

    def edit(relative, old, new, marker=None):
        current = source(relative)
        if (marker or new) in current:
            return
        if current.count(old) != 1:
            raise ValueError('Local lighting owner anchor changed: ' + relative + ': ' + repr(old))
        pending[relative] = current.replace(old, new, 1)

    def function(relative, signature, body):
        current = source(relative)
        start = current.index(signature)
        brace = current.index('{', start)
        depth, end = 1, brace + 1
        while depth:
            if current[end] == '{':
                depth += 1
            elif current[end] == '}':
                depth -= 1
            end += 1
        replacement = signature + '\n{\n' + body + '\n}'
        pending[relative] = current[:start] + replacement + current[end:]

    header = 'lib/moo/deferred_lights_manager.hpp'
    edit(header, '#include "moo/light_container.hpp"', '#include "moo/light_container.hpp"\n#include <memory>', '#include <memory>')
    edit(header, 'class LightsManager :', 'class StockLocalLighting;\n\nclass LightsManager :', 'class StockLocalLighting;')
    edit(header, '\tMoo::LightContainer\t\tm_lights;',
         '\tstd::unique_ptr<StockLocalLighting> stockLocalLighting_;\n\tMoo::LightContainer\t\tm_lights;',
         'std::unique_ptr<StockLocalLighting> stockLocalLighting_;')
    cpp = 'lib/moo/deferred_lights_manager.cpp'
    edit(cpp, '#include "deferred_lights_manager.hpp"',
         '#include "deferred_lights_manager.hpp"\n#include "stock_local_lighting.hpp"', '#include "stock_local_lighting.hpp"')
    edit(cpp, 'LightsManager::LightsManager()\n{',
         'LightsManager::LightsManager() : stockLocalLighting_(new StockLocalLighting)\n{')
    function(cpp, 'bool LightsManager::init()', '    BW_GUARD;\n    return stockLocalLighting_->initialise();')
    function(cpp, 'void LightsManager::tick(float dt)', '    BW_GUARD;\n    (void)dt;\n    stockLocalLighting_->tick();')
    function(cpp, 'void LightsManager::gatherVisibleLights(const Moo::LightContainerPtr& lights)',
             '    BW_GUARD;\n    stockLocalLighting_->gatherVisibleLights(lights);')
    function(cpp, 'void LightsManager::prepareLights()',
             '    BW_GUARD;\n    if (!stockLocalLighting_->prepare()) ERROR_MSG("Local lighting: shadow/volume preparation failed\\n");')
    for kind in ('Omnis', 'Spots'):
        function(cpp, 'void LightsManager::draw' + kind + '()',
                 '    BW_GUARD;\n    if (!stockLocalLighting_->draw' + kind + '()) ERROR_MSG("Local lighting: original ' + kind.lower() + ' draw failed\\n");')
    for operation in ('create', 'delete'):
        function(cpp, 'void LightsManager::' + operation + 'UnmanagedObjects()',
                 '    BW_GUARD;\n    stockLocalLighting_->' + operation + 'UnmanagedObjects();')
    edit(cpp, '    success &= createEffect(m_material, "shaders/std_effects/resolve_lighting.fx");',
         '    success &= createEffect(m_material, "shaders/std_effects/resolve_lighting.fx");\n'
         '    stockLocalLighting_->createManagedObjects();', '    stockLocalLighting_->createManagedObjects();')
    edit(cpp, '''    m_cone   = Moo::VisualManager::instance()->get("system/models/fx_unite_cone.visual");
    m_sphere = Moo::VisualManager::instance()->get("system/models/fx_unit_sphere.visual");

    success &= m_cone.exists() && m_sphere.exists();''',
         '    // Original unit cone/sphere belong to StockLocalLighting, with their exact names.')
    edit(cpp, '    m_material = NULL;',
         '    stockLocalLighting_->deleteManagedObjects();\n    m_material = NULL;',
         '    stockLocalLighting_->deleteManagedObjects();')
    for line in ('    const uint32 g_instanceCount    = 1000;\n', '    const float  g_LODRadiusError   = 0.5f;\n'):
        current = source(cpp)
        pending[cpp] = current.replace(line, '')

    for kind in ('omni', 'spot'):
        header = 'lib/moo/' + kind + '_light.hpp'
        edit(header, '\tint\t\t\t\t\tpriority() const { return priority_; }',
             '\tbool stockLocalInstancing() const { return stockLocalInstancing_; }\n'
             '\tvoid stockLocalInstancing(bool enabled) { stockLocalInstancing_ = enabled; }\n'
             '\tint\t\t\t\t\tpriority() const { return priority_; }', 'bool stockLocalInstancing() const')
        edit(header, '\tint\t\t\t\t\tpriority_;',
             '\tint\t\t\t\t\tpriority_;\n\tbool stockLocalInstancing_ = false;', 'bool stockLocalInstancing_ = false;')

    here = Path(__file__).resolve().parent
    copies = {root / 'lib' / name: (here / name).read_text() for name in ('stock_local_lighting.hpp', 'stock_local_lighting.cpp')}
    copies[root / 'lib/wot/stock_local_lighting.hpp'] = (here.parent / 'include/wot/stock_local_lighting.hpp').read_text()
    for relative, text in pending.items():
        (root / relative).write_text(text)
    for destination, text in copies.items():
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
