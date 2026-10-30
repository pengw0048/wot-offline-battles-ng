#!/usr/bin/env python3
"""Time the installed WorldApp scene owners without changing their calls.

Run after the stock WorldApp lifecycle patches. Compound/model work is first
attributed to its SpaceManager or GlobalEmbodiments parent, without adding a
second update or changing attachment ownership.
"""
import argparse
from pathlib import Path


PATH = 'client/world_app.cpp'
SIGNATURE = 'void WorldApp::tick( float dGameTime, float dRenderTime )'
STAGES = (
    ('WorldScene_BackgroundTasks', 'BgTaskManager::instance().tick();'),
    ('WorldScene_FileIOTasks', 'FileIOTaskManager::instance().tick();'),
    ('WorldScene_Spaces', 'SpaceManager::instance().tick( dGameTime );'),
    ('WorldScene_GlobalEmbodiments', 'GlobalEmbodiments::tick( dGameTime );'),
    ('WorldScene_BattlegroundElements', 'tickStockBattlegroundElements();'),
    ('WorldScene_GroundEffects', 'tickStockGroundEffects( dGameTime );'),
    ('WorldScene_TPS', 'tickStockTPSRuntime( dGameTime );'),
    ('WorldScene_TrackScrolls',
     'tickStockVehicleTrackScrolls( App::instance().getGameTimeFrameStart() );'),
    ('WorldScene_VehicleAudio', 'tickStockVehicleAudioTasks();'),
    ('WorldScene_VehicleTraces', 'tickStockVehicleTraces();'),
    ('WorldScene_Flocks', 'tickStockFlockManagers();'),
)
SEAMS = tuple(('        ' + call + '\n',
               '        {\n            PROFILER_SCOPED( ' + scope + ' );\n'
               '            ' + call + '\n        }\n')
              for scope, call in STAGES)
DYNAMIC_PATH = 'lib/space/dynamic_scene_provider.cpp'
DYNAMIC_SEAMS = (
    ('#include "dynamic_scene_provider.hpp"\n',
     '#include "dynamic_scene_provider.hpp"\n#include "cstdmf/profiler.hpp"\n'),
    ('void DynamicSceneProvider::tick( float dTime )\n{\n',
     'void DynamicSceneProvider::tick( float dTime )\n{\n'
     '\tPROFILER_SCOPED( DynamicScene_Tick );\n'
     '\tPROFILER_COUNTER( DynamicScene_TickObjects, static_cast<int32>(objects_.size()) );\n'),
    ('void DynamicSceneProvider::onPostTick()\n{\n',
     'void DynamicSceneProvider::onPostTick()\n{\n'
     '\tPROFILER_SCOPED( DynamicScene_PostTick );\n'
     '\tPROFILER_COUNTER( DynamicScene_PostObjects, static_cast<int32>(objectHandles_.size()) );\n'
     '\tPROFILER_COUNTER( DynamicScene_DirtyObjects, static_cast<int32>(numDirtyObjects_) );\n'
     '\tPROFILER_COUNTER( DynamicScene_Rebuilds, numDirtyObjects_ ? 1 : 0 );\n'),
    ('\t// Feed all the latest data into the core trees\n',
     '\t// Handle<24,8> bounds both N and the two insertion attempts per object to int32.\n'
     '\tPROFILER_COUNTER( DynamicScene_ReinsertAttempts, static_cast<int32>(objectHandles_.size() * 2) );\n'
     '\t// Feed all the latest data into the core trees\n'),
    ('\tMF_ASSERT( updatedObjects.size() <= numDirtyObjects_ );\n',
     '\tMF_ASSERT( updatedObjects.size() <= numDirtyObjects_ );\n'
     '\tPROFILER_COUNTER( DynamicScene_NotifyCalls, 1 );\n'
     '\tPROFILER_COUNTER( DynamicScene_NotifiedObjects, static_cast<int32>(updatedObjects.size()) );\n'),
)
DYNAMIC_TOKENS = ('DynamicScene_Tick', 'DynamicScene_PostTick',
                  'DynamicScene_TickObjects', 'DynamicScene_PostObjects',
                  'DynamicScene_DirtyObjects', 'DynamicScene_Rebuilds',
                  'DynamicScene_ReinsertAttempts', 'DynamicScene_NotifyCalls',
                  'DynamicScene_NotifiedObjects')


def tick_range(source):
    if source.count(SIGNATURE) != 1:
        raise ValueError('Pinned WorldApp tick signature changed')
    start = source.index(SIGNATURE)
    stop = source.index('\nvoid WorldApp::inactiveTick(', start)
    return start, stop


def patch_source(source):
    start, stop = tick_range(source)
    body = source[start:stop]
    if all(body.count(after) == 1 for before, after in SEAMS):
        restored = body
        for before, after in SEAMS:
            restored = restored.replace(after, before, 1)
        if (all(source.count(scope) == 1 for scope, call in STAGES) and
                all(restored.count(before) == 1 for before, after in SEAMS)):
            return source
    if (any(scope in source for scope, call in STAGES) or
            any(body.count(before) != 1 for before, after in SEAMS)):
        raise ValueError('Installed WorldApp scene-profiler seams are partial, duplicated or changed')
    for before, after in SEAMS:
        body = body.replace(before, after, 1)
    return source[:start] + body + source[stop:]


def patch_dynamic_source(source):
    # Do not replace any part of removeObject or its reentrant retirement fence.
    if all(source.count(after) == 1 for before, after in DYNAMIC_SEAMS):
        restored = source
        for before, after in DYNAMIC_SEAMS:
            restored = restored.replace(after, before, 1)
        if (all(restored.count(before) == 1 for before, after in DYNAMIC_SEAMS) and
                all(source.count(token) == sum(after.count(token) for before, after in DYNAMIC_SEAMS)
                    for token in DYNAMIC_TOKENS)):
            return source
    if (any(token in source for token in DYNAMIC_TOKENS) or
            any(source.count(before) != 1 for before, after in DYNAMIC_SEAMS)):
        raise ValueError('Dynamic scene profiler seams are partial, duplicated or changed')
    for before, after in DYNAMIC_SEAMS:
        source = source.replace(before, after, 1)
    return source


def prepared(path, transform):
    data = path.read_bytes()
    bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
    text = data[len(bom):].decode('utf-8')
    crlf = '\r\n' in text
    if '\r' in text.replace('\r\n', '') or (crlf and text.count('\r\n') != text.count('\n')):
        raise ValueError('Mixed WorldApp source line endings')
    source = text.replace('\r\n', '\n')
    patched = transform(source)
    if patched != source:
        return bom + patched.replace('\n', '\r\n' if crlf else '\n').encode('utf-8')
    return None


def patch(root):
    # Validate both source owners before modifying either cached engine file.
    changes = [(Path(root) / name, prepared(Path(root) / name, transform))
               for name, transform in ((PATH, patch_source), (DYNAMIC_PATH, patch_dynamic_source))]
    for path, data in changes:
        if data is not None:
            path.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
