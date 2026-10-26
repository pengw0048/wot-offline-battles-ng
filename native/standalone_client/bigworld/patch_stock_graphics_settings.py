#!/usr/bin/env python3
"""Restore #1513 graphics registry views and preference/status lifecycle."""
import argparse
from pathlib import Path


def function(source, marker):
    start = source.index(marker)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def patch(root):
    root = Path(root)
    pending = {}

    def edit(name, before, after, present=None):
        source = pending.get(name)
        if source is None:
            source = (root / name).read_text(encoding='utf-8-sig')
        if (present or after) in source:
            pending[name] = source
            return
        if source.count(before) != 1:
            raise ValueError('Original graphics registry seam changed: ' + name + ': ' + repr(before))
        pending[name] = source.replace(before, after, 1)

    header = 'lib/moo/graphics_settings.hpp'
    edit(header, '\tbool\t\t\t\tdelayed() const\t\t\t{\treturn delayed_;\t}',
         '\tbool\t\t\t\tdelayed() const\t\t\t{\treturn delayed_;\t}\n'
         '\tbool stockImportant() const { return stockImportant_; }\n'
         '\tvoid stockImportant(bool value) { stockImportant_ = value; }\n'
         '\tbool stockArrayOptions() const { return stockArrayOptions_; }\n'
         '\tvoid stockArrayOptions(bool value) { stockArrayOptions_ = value; }',
         '\tbool stockImportant() const')
    edit(header, '\tGraphicsSettingVector   slaves_;',
         '\tGraphicsSettingVector   slaves_;\n'
         '\t// The original flags default to important, scalar option data.\n'
         '\tbool stockImportant_ = true;\n'
         '\tbool stockArrayOptions_ = false;')

    picker = 'lib/moo/graphics_settings_picker.hpp'
    edit(picker, '\tSystemPerformance detectBestSettings(bool forceNewDetection = false);',
         '\tSystemPerformance detectBestSettings(bool forceNewDetection = false);\n'
         '\tstatic void invalidateStockDetection() { lastDetection = PERF_LEVEL_COUNT; }')

    cpp = 'lib/moo/graphics_settings.cpp'
    edit(cpp, '#include "graphics_settings.hpp"',
         '#include "graphics_settings.hpp"\n#include "stock_graphics_settings.hpp"\n#include <algorithm>',
         '#include "stock_graphics_settings.hpp"')
    source = pending[cpp]
    marker = '\tif( !section.exists() )// cannot find preferences.xml'
    ready = '\tif (!prepareStockGraphicsPreferences(section, tempSettings, overrideAutoDetectSettingsGroup))'
    if ready not in source:
        if source.count(marker) != 1:
            raise ValueError('Original graphics preference initialization changed')
        start = source.index(marker)
        end = source.index('\n\t//make copy of old settings', start)
        pending[cpp] = source[:start] + ready + '\n\t\treturn false;\n' + source[end:]
    edit(cpp, '\tsection->writeInt( "graphicsSettingsVersion",GRAPHICS_SETTINGS_VERSION_NOTES_FORWARD );',
         '\twriteStockGraphicsPreferencesMetadata(section);')
    source = pending[cpp]
    original = function(source, 'void GraphicsSetting::write(')
    if 'stockImportant()' not in original:
        start = original.index('\tGraphicsSettingVector::const_iterator setIt  = settings().begin();')
        replacement = '''\tGraphicsSettingVector sortedSettings = settings();
    std::sort(sortedSettings.begin(), sortedSettings.end(),
        [](const GraphicsSettingPtr& a, const GraphicsSettingPtr& b) { return a->label() < b->label(); });
    for (const auto& setting : sortedSettings)
    {
        if (!setting->stockImportant()) continue;
        DataSectionPtr entry = section->newSection("entry");
        entry->writeString("label", setting->label());
        entry->writeInt("activeOption", setting->activeOption());
    }
}'''
        pending[cpp] = source.replace(original, original[:start] + replacement, 1)

    python = 'lib/romp/py_graphics_setting.cpp'
    edit(python, '#include "py_graphics_setting.hpp"',
         '#include "py_graphics_setting.hpp"\n#include "stock_graphics_settings.hpp"',
         '#include "stock_graphics_settings.hpp"')
    source = pending[python]
    old = function(source, 'static PyObject * graphicsSettings()')
    new = 'static PyObject * graphicsSettings()\n{\n\treturn stockGraphicsSettingsView();\n}'
    if old != new:
        pending[python] = source.replace(old, new, 1)
    edit(python, '\t\t\tif (optionIndex < int( (*setIt)->options().size() ))',
         '\t\t\tif (optionIndex >= 0 && optionIndex < int( (*setIt)->options().size() ))')
    edit(python, '\t\t\t\t\t(*setIt)->selectOption( optionIndex );',
         '\t\t\t\t\t(*setIt)->selectOption( optionIndex, true );')

    application = Path(__file__).resolve().parent
    copies = (
        (application / 'stock_graphics_settings.hpp', root / 'lib/stock_graphics_settings.hpp'),
        (application.parent / 'include/wot/stock_graphics_settings.hpp', root / 'lib/wot/stock_graphics_settings.hpp'),
    )
    contents = [(destination, path.read_bytes()) for path, destination in copies]
    for path, source in pending.items():
        (root / path).write_text(source, encoding='utf-8')
    for destination, data in contents:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
