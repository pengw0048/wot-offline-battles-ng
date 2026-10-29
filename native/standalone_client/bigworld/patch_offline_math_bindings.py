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
    if source.count(after) == 1:
        return source
    if source.count(before) != 1 or after in source:
        raise ValueError('Pinned numerical binding seam changed: '+repr(before))
    return source.replace(before, after, 1)



def patch_query_chain(root, pending):
    # Keep ordered numerical laws intact; adapt only the synchronous owner.
    path = root/'offline_math_batch_native.cpp'
    pending[path] = once(pending[path], '#include "offline_simulation_diagnostics.h"',
        '#include "offline_simulation_diagnostics.h"\n'
        '#if defined(WOT_NATIVE_ENGINE_QUERY) || defined(WOT_NATIVE_ROTATION_QUERY)\n'
        '#define WOT_NATIVE_QUERY_BRIDGE\n'
        '#include "stock_offline_query_bridge.hpp"\n'
        '#endif')
    path = root/'offline_simulation_motion_python.inc'
    source = pending[path]
    source = once(source, '        bool departing(PyObject *hit) {\n',
        '''        #ifdef WOT_NATIVE_QUERY_BRIDGE
        stock_offline_query::Context *native_ = nullptr;
        std::map<std::uint64_t, stock_offline_query::Hit> native_hits_;
        #endif
        bool departing(PyObject *hit) {
''')
    source = once(source,
        '            session.point(items(hit)[1], normal);\n            if (normal[1] < -.2)',
        '''            session.point(items(hit)[1], normal);
            return departing_point(point, normal);
        }
        bool departing_point(const double *point, const double *normal) {
            if (normal[1] < -.2)''')
    source = once(source, '            auto position=session.vector(snapshot.position);\n',
        '''            #ifdef WOT_NATIVE_QUERY_BRIDGE
            if(!snapshot.supplied_departing)native_=stock_offline_query::begin(capabilities);
            if(native_) {stock_offline_query::rounded(snapshot.position,position_);return;}
            #endif
            auto position=session.vector(snapshot.position);
''')
    source = once(source, '        void departure(const WorldRequest &request) {\n',
        '''        ~WorldFrontier() {
            #ifdef WOT_NATIVE_QUERY_BRIDGE
            stock_offline_query::end(native_);
            #endif
        }
        bool finish() {
            #ifdef WOT_NATIVE_QUERY_BRIDGE
            return stock_offline_query::beforeEffect(native_);
            #else
            return true;
            #endif
        }
        void effect_returned() {
            #ifdef WOT_NATIVE_QUERY_BRIDGE
            if(!stock_offline_query::afterEffect(native_))throw engine_query::Failed();
            #endif
        }
        bool native_ground(const WorldRequest &request, WorldResponse &response) {
            #ifdef WOT_NATIVE_QUERY_BRIDGE
            if(stock_offline_query::groundAvailable(native_)) {
                ray(WORLD_GROUND,request,response,nullptr);return stock_offline_query::groundAvailable(native_);
            }
            #endif
            return false;
        }
        void departure(const WorldRequest &request) {
''')
    source = once(source, '            return hits_[static_cast<unsigned>(handle-1)].get();',
        '''            auto &cached=hits_[static_cast<unsigned>(handle-1)];
            #ifdef WOT_NATIVE_QUERY_BRIDGE
            if(!cached.get()) {
                const auto found=native_hits_.find(handle);
                if(found==native_hits_.end())throw Unsupported();
                cached=engine_query::Object(stock_offline_query::materialize(found->second));
            }
            #endif
            return cached.get();''')
    source = once(source, '            engine_query::Object start,end;\n            session.segment(request.start,request.end,start,end,response.start,response.end);',
        '''            #ifdef WOT_NATIVE_QUERY_BRIDGE
            if(native_ && !external_departing) {
                stock_offline_query::beginRay(native_);
                const auto old_planes=planes_.size();
                stock_offline_query::Hit hit;
                double current[3];stock_offline_query::rounded(request.start,response.start);
                stock_offline_query::rounded(request.end,response.end);
                std::memcpy(current,response.start,sizeof current);
                auto result=stock_offline_query::Unbound;
                for(unsigned attempt=0;attempt<=recasts_;++attempt) {
                    result=stock_offline_query::ray(native_,query_filter,op==WORLD_GROUND,current,response.end,hit);
                    if(result==stock_offline_query::Failed)throw engine_query::Failed();
                    if(result!=stock_offline_query::Found || op!=WORLD_HORIZONTAL || !has_departing_ ||
                       !(request.flags&1) || !departing_point(hit.point,hit.normal))break;
                    double length=0.;stock_offline_query::departureStep(response.end,hit.point,epsilon_,current,length);
                    if(length<=epsilon_) {result=stock_offline_query::Miss;break;}
                }
                stock_offline_query::finishRay(native_,result!=stock_offline_query::Unbound);
                if(result!=stock_offline_query::Unbound) {
                    if(result==stock_offline_query::Found) {
                        std::memcpy(response.point,hit.point,sizeof response.point);
                        std::memcpy(response.normal,hit.normal,sizeof response.normal);
                        hits_.push_back(engine_query::Object());
                        response.handle=hits_.size();response.status=1;
                        native_hits_.emplace(response.handle,hit);
                    }
                    return;
                }
                planes_.resize(old_planes);
                if(!finish())throw engine_query::Failed();
                if(!query_filter)return;
            }
            #endif
            engine_query::Object start,end;
            session.segment(request.start,request.end,start,end,response.start,response.end);''')
    source = once(source,
        '                        if(op!=WORLD_GROUND)throw Unsupported();\n                        OwnedTuple rows(1);rows.set(0,world_request(WORLD_GROUND,q[i]));',
        '''                        if(op!=WORLD_GROUND)throw Unsupported();
                        if(frontier_->native_ground(q[i],r[i])) {
                            stopped=stopped || (!r[i].status && (q[i].flags&1));continue;
                        }
                        OwnedTuple rows(1);rows.set(0,world_request(WORLD_GROUND,q[i]));''')
    source = once(source,
        '            OwnedTuple rows(n);\n            for(size_t i=0;i<n;++i) {\n                auto value=world_request(op,q[i]);',
        '''            if(!frontier_->finish())throw engine_query::Failed();
            OwnedTuple rows(n);
            for(size_t i=0;i<n;++i) {
                auto value=world_request(op,q[i]);''')
    source = once(source, '            auto result=query(op,rows,n);\n            bridge.input();',
        '            auto result=query(op,rows,n);\n            frontier_->effect_returned();\n            bridge.input();')
    source = once(source,
        '        void end_world()override {\n            NativeProfilePhase bridge(NativePhase::Pack);\n            frontier_.reset();',
        '''        void end_world()override {
            NativeProfilePhase bridge(NativePhase::Pack);
            if(frontier_ && !frontier_->finish() && !world_error_)
                world_error_=std::make_exception_ptr(engine_query::Failed());
            frontier_.reset();''')
    pending[path] = source
    path = root/'offline_world_query_python.inc'
    source = path.read_text()
    source = once(source,
        '                    if(op!=WORLD_GROUND)throw Unsupported();\n                    OwnedTuple rows(1);rows.set(0,world_request(WORLD_GROUND,request));',
        '''                    if(op!=WORLD_GROUND)throw Unsupported();
                    if(frontier->native_ground(request,response)) {
                        stopped=stopped || (!response.status && (request.flags&1));continue;
                    }
                    OwnedTuple rows(1);rows.set(0,world_request(WORLD_GROUND,request));''')
    source = once(source, '        OwnedTuple rows(count);\n',
        '        if(!frontier->finish())throw engine_query::Failed();\n        OwnedTuple rows(count);\n')
    source = once(source, '        auto result=invoke(op,rows,count);bridge.input();',
        '        auto result=invoke(op,rows,count);frontier->effect_returned();bridge.input();')
    source = once(source,
        '        const int result=profile.run(wot_world_run,&snapshot,native_world_query_dispatch,&context);\n',
        '        const int result=profile.run(wot_world_run,&snapshot,native_world_query_dispatch,&context);\n'
        '        if(!frontier.finish())throw engine_query::Failed();\n')
    pending[path] = source


def patch_rotation_chain(root, pending):
    path = root/'offline_math_batch_native.cpp'
    pending[path] = once(pending[path], '#include "offline_world_query_python.inc"',
        '#include "offline_world_query_python.inc"\n'
        '#ifdef WOT_NATIVE_ROTATION_QUERY\n'
        '#include "stock_motion_rotation_python.inc"\n'
        '#endif')
    pending[path] = once(pending[path],
        '    if (!initialize_api() || !initialize_keys()) return;\n',
        '    if (!initialize_api() || !initialize_keys()) return;\n'
        '#ifdef WOT_NATIVE_ROTATION_QUERY\n'
        '    if (!stock_rotation_binding::initialize()) return;\n'
        '#endif\n')
    path = root/'offline_simulation_motion_python.inc'
    source = pending[path]
    source = once(source, '        WorldSnapshot snapshot_;\n',
        '        WorldSnapshot snapshot_;\n'
        '        stock_motion_rotation::DeparturePolicy *rotation_policy_ = nullptr;\n'
        '        PyObject *rotation_query_ = nullptr;\n'
        '        bool rotationAccepts(const double *p,const double *n) {\n'
        '            stock_motion_rotation::Contact hit;\n'
        '            std::copy(p,p+3,hit.point.begin());std::copy(n,n+3,hit.normal.begin());\n'
        '            if(native_ && !stock_offline_query::beforeEffect(native_))throw engine_query::Failed();\n'
        '            const bool accepted=rotation_policy_->accepts(hit);\n'
        '            if(native_) {if(!stock_offline_query::afterDeparture(native_))throw engine_query::Failed();}\n'
        '            else {engine_query::Object live(PyObject_CallMethod(rotation_query_,const_cast<char *>("_live"),NULL));}\n'
        '            return accepted;\n'
        '        }\n')
    source = once(source,
        '        WorldFrontier(PyObject *capabilities, PyObject *tuning, const WorldSnapshot &snapshot):\n'
        '            snapshot_(snapshot), session(capabilities) {',
        '        WorldFrontier(PyObject *capabilities, PyObject *tuning, const WorldSnapshot &snapshot,\n'
        '                stock_motion_rotation::DeparturePolicy *rotation_policy=nullptr,bool rotation_world=false):\n'
        '            snapshot_(snapshot), rotation_policy_(rotation_policy), session(capabilities) {\n'
        '            if(rotation_world)rotation_query_=PyMethod_GET_SELF(items(capabilities)[7]);')
    source = once(source,
        '            if(!snapshot.supplied_departing)native_=stock_offline_query::begin(capabilities);',
        '#ifdef WOT_NATIVE_ENGINE_QUERY\n'
        '            if(!snapshot.supplied_departing)native_=stock_offline_query::begin(capabilities);\n'
        '#endif\n'
        '#ifdef WOT_NATIVE_ROTATION_QUERY\n'
        '            if(rotation_world && !native_)native_=stock_offline_query::begin(capabilities);\n'
        '#endif')
    source = once(source,
        'result!=stock_offline_query::Found || op!=WORLD_HORIZONTAL || !has_departing_ ||\n'
        '                       !(request.flags&1) || !departing_point(hit.point,hit.normal)',
        'result!=stock_offline_query::Found || op!=WORLD_HORIZONTAL || (!has_departing_ && !rotation_policy_) ||\n'
        '                       !(request.flags&1) || !(rotation_policy_?rotationAccepts(hit.point,hit.normal):departing_point(hit.point,hit.normal))')
    source = once(source,
        '(!has_departing_ && !external_departing) ||\n'
        '                   !(request.flags&1) || !(external_departing?external_departing(result.get()):departing(result.get()))',
        '(!has_departing_ && !external_departing && !rotation_policy_) ||\n'
        '                   !(request.flags&1) || !rotationHit(result.get(),external_departing)')
    source = once(source, '        bool departing(PyObject *hit) {\n',
        '        bool rotationHit(PyObject *hit,const std::function<bool(PyObject *)> &external) {\n'
        '            if(!rotation_policy_)return external?external(hit):departing(hit);\n'
        '            require_sequence(hit,2);double p[3],n[3];\n'
        '            session.point(items(hit)[0],p);session.point(items(hit)[1],n);\n'
        '            return rotationAccepts(p,n);\n'
        '        }\n'
        '        bool departing(PyObject *hit) {\n')
    source = once(source, '                const auto old_planes=planes_.size();\n',
        '                const auto old_planes=planes_.size();bool rotation_crossed=false;\n')
    source = once(source, '                       !(request.flags&1) || !(rotation_policy_?rotationAccepts(hit.point,hit.normal):departing_point(hit.point,hit.normal)))break;',
        '                       !(request.flags&1) || !(rotation_policy_?(rotation_crossed=true,rotationAccepts(hit.point,hit.normal)):departing_point(hit.point,hit.normal)))break;')
    source = once(source, '                stock_offline_query::finishRay(native_,result!=stock_offline_query::Unbound);',
        '                if(result==stock_offline_query::Unbound && rotation_crossed) {\n'
        '                    PyErr_SetString(PyExc_RuntimeError,"Native rotation ray changed after departure; no replay is permitted");throw engine_query::Failed();\n'
        '                }\n'
        '                stock_offline_query::finishRay(native_,result!=stock_offline_query::Unbound);')
    pending[path] = source
    path = root/'offline_math_batch_native.cpp'
    pending[path] = once(pending[path], '#include "stock_offline_query_bridge.hpp"',
        '#include "stock_offline_query_bridge.hpp"\n#include "stock_motion_rotation_batch.hpp"')

def patch(root, native_query=False, rotation_query=False):
    root = Path(root)/'native'
    pending = {}
    path = root/'offline_math_batch_native.cpp'
    original = path.read_text()
    if expanded_methods(root, original) != METHODS:
        raise ValueError('Pinned v0.10.1 module method table changed')
    prepared_motion = (root/'offline_simulation_motion_python.inc').read_text() if rotation_query else ''
    if rotation_query and ('stock_motion_rotation_python.inc' in original or
            'stock_rotation_binding::initialize' in original or
            'stock_motion_rotation::DeparturePolicy' in prepared_motion):
        # Rotation extends several already-adapted common seams. Validate its
        # final owner rather than nesting those common wrappers on reapply.
        motion = prepared_motion
        world = (root/'offline_world_query_python.inc').read_text()
        expected = ('stock_motion_rotation::DeparturePolicy *rotation_policy=nullptr',
                    'stock_offline_query::afterDeparture(native_)',
                    'rotation_crossed=true,rotationAccepts',
                    'if(rotation_world && !native_)')
        include = '#ifdef WOT_NATIVE_ROTATION_QUERY\n#include "stock_motion_rotation_python.inc"\n#endif'
        initialize = '#ifdef WOT_NATIVE_ROTATION_QUERY\n    if (!stock_rotation_binding::initialize()) return;\n#endif'
        if (any(value not in motion for value in expected) or
                'frontier->effect_returned();' not in world or
                original.count(include) != 1 or original.count(initialize) != 1 or
                original.count('#include "stock_motion_rotation_python.inc"') != 1 or
                original.count('stock_rotation_binding::initialize()') != 1):
            raise ValueError('Incomplete prepared rotation binding')
        return
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
    source = path.read_text()
    if 'auto &cached=hits_[static_cast<unsigned>(handle-1)];' not in source:
        source = once(source, 'return hits_[handle-1].get();',
                      'return hits_[static_cast<unsigned>(handle-1)].get();')
    # The preceding guard bounds the index by the owned list's unsigned size.
    # Make its existing conversion explicit for MSVC /WX, without changing it.
    pending[path] = source
    # Ordinary translation remains default-off after the earlier regression.
    # Rotation is compiled independently and has its own runtime admission guard.
    if native_query or rotation_query:
        patch_query_chain(root, pending)
    if rotation_query:
        patch_rotation_chain(root, pending)
    # Validate the complete transaction before publishing any adapted source.
    for path, source in pending.items():
        path.write_text(source, encoding='utf-8', newline='')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source_root', type=Path)
    patch(parser.parse_args().source_root)
