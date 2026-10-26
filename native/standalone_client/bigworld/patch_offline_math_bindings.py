"""Adapt the fixed fork v0.10.1 numerical module to the owned Python C API.

The numerical laws and 103 module methods remain untouched. Python binding
selection is independent of the platform: Win32 retains its real process,
window and thread-clock operations when using the embedded Python headers.
"""
import argparse
from pathlib import Path
import re

PINNED_COMMIT = '144619f6eb2925f8da4b13637bf65b8550fc898f'
SOURCES = (
    'offline_math_batch_geometry.cpp', 'offline_async_worker.cpp',
    'offline_navigation.cpp', 'native_visibility_core.cpp',
    'offline_visibility.cpp', 'offline_contact_roster.cpp',
    'offline_destructible_geometry.cpp', 'world_stage.cpp',
    'navigation_query_stage.cpp', 'offline_simulation.cpp',
    'offline_simulation_control.cpp', 'offline_simulation_motion.cpp',
    'offline_simulation_weapons.cpp', 'offline_simulation_navigation.cpp',
    'offline_math_batch_native.cpp',
)
METHODS = tuple('''driver_aim driver_geometry projectile_math projectile_spatial contact_ground
release_suspension_step rotation_envelope rotation_departure control_pose control_observation_rows
contact_materialize release_drive parking_distances sim_diag_begin sim_diag_end sim_open sim_close
sim_lifetime sim_control_configure sim_control_update sim_control_begin sim_control_contacts
sim_control_contacts_materialized sim_control_humans sim_control_observations sim_control_team_contacts
sim_control_finish sim_control_forget sim_control_drive sim_control_driver_event sim_control_driver_restore
sim_control_traffic sim_control_traffic_state sim_control_snapshot sim_control_radio
sim_control_radio_summaries sim_control_radio_actors sim_control_source_still
sim_motion_configure sim_motion_install sim_motion_patch sim_motion_begin sim_motion_prepare
sim_motion_advance sim_motion_settle sim_motion_patch_fields sim_motion_after_weapon sim_motion_descriptor
sim_motion_snapshot sim_weapon_install sim_weapon_snapshot sim_weapon_command sim_weapon_remove
sim_weapon_aim sim_weapon_ballistic sim_weapon_commit sim_weapon_pending sim_weapon_ack
sim_navigation_order_events sim_navigation_async_snapshot sim_navigation_receipts
sim_navigation_grid_snapshot sim_navigation_bot_snapshot sim_navigation_grid_command
sim_navigation_install sim_navigation_snapshot sim_navigation_remove sim_navigation_command
destr_box_geometry destr_required_chunks destr_open destr_close destr_update destr_query
engine_query_filter vis_frontier_frame vis_frontier_actor vis_frontier_snapshot nav_query_run
nav_query_filter thread_cpu_seconds window_state world_run_native world_run contact_roster vis_open
vis_update vis_submit vis_poll vis_reduce vis_cancel vis_close nav_open nav_submit nav_poll nav_answer
nav_cancel nav_close translation_fraction translation_contacts slide_translation rotation_current
rotation_fraction'''.split())

HEADER_BEFORE = '''#ifdef WOT_HOST_PYTHON
#include <Python.h>
#define WOT_CDECL
#else
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define WOT_CDECL __cdecl
'''
HEADER_AFTER = '''// Independent binding adapter for fork v0.10.1 / 144619f6.
#if defined(WOT_HOST_PYTHON) && !defined(WOT_PYTHON_C_API)
#define WOT_PYTHON_C_API
#endif
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#define WOT_CDECL __cdecl
#else
#define WOT_CDECL
#endif
#ifdef WOT_PYTHON_C_API
#include <Python.h>
#else
'''
EXPORT_BEFORE = '''#ifdef WOT_HOST_PYTHON
extern "C" __attribute__((visibility("default"))) void initoffline_math_batch_native(void) {
#else
extern "C" __declspec(dllexport) void __cdecl initoffline_math_batch_native(void) {
#endif'''
EXPORT_AFTER = '''#if defined(_WIN32)
extern "C" __declspec(dllexport) void __cdecl initoffline_math_batch_native(void) {
#else
extern "C" __attribute__((visibility("default"))) void initoffline_math_batch_native(void) {
#endif'''


def expanded_methods(root, source):
    begin = source.index('PyMethodDef methods[] = {')
    end = source.index('bool initialize_keys()', begin)
    table = source[begin:end]
    macros = {}
    for path in sorted(root.glob('*.inc')):
        lines = path.read_text().splitlines()
        i = 0
        while i < len(lines):
            match = re.match(r'#define (WOT_\w+_METHODS)', lines[i])
            if match:
                name = match.group(1)
                block = [lines[i]]
                while block[-1].endswith('\\') and i+1 < len(lines):
                    i += 1
                    block.append(lines[i])
                macros[name] = '\n'.join(block)[len('#define '+name):]
            i += 1
    # Control registers nested BASE/CHECKPOINT/SOURCE lists. Expand recursively
    # rather than silently losing their twenty actual runtime methods.
    for unused in range(32):
        tokens = re.findall(r'\bWOT_\w+_METHODS\b', table)
        if not tokens:
            break
        if any(name not in macros for name in tokens):
            raise ValueError('Unknown numerical module method macro')
        table = re.sub(r'\bWOT_\w+_METHODS\b', lambda match: macros[match.group(0)], table)
    else:
        raise ValueError('Recursive numerical module method macro')
    return tuple(re.findall(r'\{\s*"([^"]+)"\s*,', table))


def once(source, before, after):
    if source.count(after) == 1 and before not in source:
        return source
    if source.count(before) != 1 or after in source:
        raise ValueError('Pinned numerical binding seam changed: '+repr(before))
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)/'native'
    pending = {}
    path = root/'offline_math_batch_native.cpp'
    original = path.read_text()
    if expanded_methods(root, original) != METHODS:
        raise ValueError('Pinned v0.10.1 module method table changed')
    source = once(original, HEADER_BEFORE, HEADER_AFTER)
    source = once(source, EXPORT_BEFORE, EXPORT_AFTER)
    source = once(source, '#ifdef WOT_HOST_PYTHON\n#include <time.h>',
                  '#if !defined(_WIN32)\n#include <time.h>')
    # All remaining old host choices in this TU are Python object/API access.
    source = source.replace('#ifdef WOT_HOST_PYTHON', '#ifdef WOT_PYTHON_C_API')
    if expanded_methods(root, source) != METHODS:
        raise ValueError('Numerical adapter changed the module methods')
    pending[path] = source
    path = root/'offline_destructible_prepare_python.inc'
    source = path.read_text()
    if source.count('#ifdef WOT_HOST_PYTHON') == 2:
        source = source.replace('#ifdef WOT_HOST_PYTHON', '#ifdef WOT_PYTHON_C_API')
    elif source.count('#ifdef WOT_PYTHON_C_API') != 2:
        raise ValueError('Pinned destructible Python accessors changed')
    pending[path] = source
    path = root/'offline_navigation_query_python.inc'
    source = path.read_text()
    if source.count('#ifdef WOT_HOST_PYTHON') == 2 and source.count('#ifndef WOT_HOST_PYTHON') == 1:
        source = source.replace('#ifdef WOT_HOST_PYTHON', '#if !defined(_WIN32)')
        source = source.replace('#ifndef WOT_HOST_PYTHON', '#if defined(_WIN32)')
    elif source.count('#if !defined(_WIN32)') != 2 or source.count('#if defined(_WIN32)') != 1:
        raise ValueError('Pinned thread/window platform seams changed')
    pending[path] = source
    path = root/'offline_simulation_motion_python.inc'
    source = once(path.read_text(), 'return hits_[handle-1].get();',
                  'return hits_[static_cast<unsigned>(handle-1)].get();')
    # The preceding guard bounds the index by the owned list's unsigned size.
    # Make its existing conversion explicit for MSVC /WX, without changing it.
    pending[path] = source
    # Validate the complete transaction before publishing any adapted source.
    for path, source in pending.items():
        path.write_text(source, encoding='utf-8', newline='')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_root', type=Path)
    patch(parser.parse_args().source_root)
