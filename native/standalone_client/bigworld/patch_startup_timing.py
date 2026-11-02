#!/usr/bin/env python3
"""Expose pre-logger startup stages and FX/driver creation time in python.log."""

import argparse
from pathlib import Path

STARTUP_REPORT = '''\t// Script/App has installed Python stdout before task initialization returns.
\t// Use that route so these lines reach the python.log collected by reports.
\t// All task times are inclusive; overlapping totals must not be added.
\tfor (const auto& stage : wot_startup::finish())
\t\tPySys_WriteStdout( "STARTUP stage=%s calls=%u wall_ms=%.3f cpu_ms=%.3f "
\t\t\t"worst_ms=%.3f inclusive=1\\n", stage.stage.c_str(), stage.calls,
\t\t\tstage.wall_ms, stage.cpu_ms, stage.worst_ms );
'''


def replace_once(source, before, after):
    if source.count(before) != 1:
        raise ValueError('Pinned startup timing seam changed: ' + repr(before))
    return source.replace(before, after, 1)


def patch(engine):
    (engine / 'lib/cstdmf/stock_startup_timing.hpp').write_bytes(
        Path(__file__).with_name('stock_startup_timing.hpp').read_bytes())

    def edit(relative, changes):
        path = engine / relative
        source = path.read_text()
        source = replace_once(source, '#include "pch.hpp"',
                              '#include "pch.hpp"\n#include "cstdmf/stock_startup_timing.hpp"')
        for before, after in changes:
            source = replace_once(source, before, after)
        path.write_text(source)

    edit('lib/cstdmf/main_loop_task.cpp', [
        ('#ifdef DISPLAY_MEMORY\n\tBW::vector<const char*> toinitNames;',
         '\tBW::vector<const char*> toinitNames;'),
        ('\ttoinitNames.reserve( order_.size() );\n#endif',
         '\ttoinitNames.reserve( order_.size() );'),
        ('#ifdef DISPLAY_MEMORY\n\t\ttoinitNames.push_back( (tasks_.begin() + *it)->first );\n#endif',
         '\t\ttoinitNames.push_back( (tasks_.begin() + *it)->first );'),
        ('for (uint i = 0; i < toinit.size(); i++)\n\t{',
         'for (uint i = 0; i < toinit.size(); i++)\n\t{\n'
         '\t\twot_startup::Scope startupStage( toinitNames[i] );'),
    ])
    edit('client/app.cpp', [
        ('\tRecreateDeviceCallback::createInstance();',
         STARTUP_REPORT + '\n'
         '\tRecreateDeviceCallback::createInstance();'),
    ])
    edit('lib/moo/effect_compiler.cpp', [
        ('BinaryPtr EffectCompiler::loadOrCompile( const BW::string& resourceID )\n{',
         'BinaryPtr EffectCompiler::loadOrCompile( const BW::string& resourceID )\n{\n'
         '    wot_startup::Scope startupStage("fx.load_or_compile", true);'),
        ('    BinaryPtr original = EffectHelpers::loadEffectBinary(resourceID);',
         '    BinaryPtr original;\n    {\n'
         '        wot_startup::Scope readStage("fx.original_read", true);\n'
         '        original = EffectHelpers::loadEffectBinary(resourceID);\n    }'),
        ('        return EffectHelpers::loadEffectBinary( resourceID );',
         '        wot_startup::Scope cacheStage("fx.cache_hit", true);\n'
         '        return EffectHelpers::loadEffectBinary( resourceID );'),
        ('    BinaryPtr binary = compiler.compile( resourceID, &result );',
         '    BinaryPtr binary;\n    {\n'
         '        wot_startup::Scope compileStage("fx.source_compile", true);\n'
         '        binary = compiler.compile( resourceID, &result );\n    }'),
    ])
    edit('client/finale_app.cpp', [
        ('bool FinaleApp::cacheEffects( const BW::vector<BW::string> & effects ) const\n{',
         'bool FinaleApp::cacheEffects( const BW::vector<BW::string> & effects ) const\n{\n'
         '    wot_startup::Scope cacheStage("fx.eager_cache", true);'),
        ('void FinaleApp::runPreloads( ProgressTask & preloadTask )\n{',
         'void FinaleApp::runPreloads( ProgressTask & preloadTask )\n{\n'
         '    wot_startup::Scope preloadStage("resource.preloads", true);'),
    ])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine', type=Path)
    patch(parser.parse_args().engine)
