"""Real CPython 2.7 contract harness for the production native query provider.

Only scene traversal, Vector3 presentation, and native callable identity are
host fixtures. Production provider, numeric skin core and scene query execute.
"""
import ast
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_battle_scene_queries import ROOT, FAKE_HEADERS, engine_includes, ground_water_query
from test_stock_offline_query_skin import FUNCTIONS


def selected(source, names):
    tree = ast.parse(source)
    found = [node for node in tree.body if isinstance(node, (ast.FunctionDef, ast.ClassDef)) and node.name in names]
    if {node.name for node in found} != set(names):
        raise AssertionError('Pinned native query source seam changed')
    return '\n\n'.join(ast.get_source_segment(source, node) for node in found)


def consumer_source(fork):
    spec = importlib.util.spec_from_file_location('query_bindings', ROOT / 'bigworld/stock_offline_query_bindings.py')
    bindings = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bindings)
    scripts = fork / 'src/res/scripts/client/gui/mods/offline_lan_0922'
    sensor_source = bindings.sensor((scripts / 'destructibles_sensor.py').read_text())
    sensor_suffix = sensor_source[sensor_source.index('\n# Canonical objects for the independent synchronous native query binding.'):]
    sensor_names = FUNCTIONS | set(bindings.SENSOR_LAWS) | {'collide_motion_segment'}
    sensor_source = selected(sensor_source, sensor_names)
    authority_source = selected((scripts / 'destructibles_authority.py').read_text(), {'is_destroyed', 'destroyed_keys'})
    engine_source = bindings.engine((scripts / 'native_engine_query.py').read_text())
    world_adapted = bindings.world((scripts / 'world_collision.py').read_text())
    world_suffix = world_adapted[world_adapted.index('\n_native_query_trace_codes ='):]
    world_source = selected(world_adapted, {'_trace_collision_filter'})
    return CONSUMER_SETUP + '\n'.join((
        'exec(%r, sensor.__dict__)' % sensor_source,
        'exec(%r, authority.__dict__)' % authority_source,
        'exec(%r, world.__dict__)' % world_source,
        'SENSOR_LAWS = %r' % (bindings.SENSOR_LAWS,),
    )) + CONSUMER_MODULES + '\n'.join((
        '\nexec(%r, sensor.__dict__)' % sensor_suffix,
        'exec(%r, world.__dict__)' % world_suffix,
        'exec(%r, engine.__dict__)\n' % engine_source,
    )) + CONSUMER_CASES


CONSUMER_SETUP = r'''
from __future__ import print_function
import sys, types, math, struct, gc, StringIO
import Bridge, Host, _BigWorld
PREFIX = 'gui.mods.offline_lan_0922'
def module(name):
    value = types.ModuleType(name)
    value.__path__ = []
    value.__package__ = name.rsplit('.', 1)[0] if '.' in name else name
    sys.modules[name] = value
    if '.' in name:
        parent, child = name.rsplit('.', 1)
        setattr(sys.modules[parent], child, value)
    return value
module('gui'); module('gui.mods'); package = module(PREFIX)
sensor = module(PREFIX + '.destructibles_sensor')
authority = module(PREFIX + '.destructibles_authority')
world = module(PREFIX + '.world_collision')
engine = module(PREFIX + '.native_engine_query')
setattr(package, 'destructibles_authority', authority)
diagnostics = module(PREFIX + '.worker_diagnostics')
diagnostics.current = lambda: None
diagnostics.observed_ray = lambda label, function, *args: function(*args)
flags = module(PREFIX + '.collision_flags'); flags.VEHICLE_SKIP_FLAGS = 0x50
def f32(value): return struct.unpack('<f', struct.pack('<f', value))[0]
class Vector3(object):
    def __init__(self, x=0., y=0., z=0.):
        self.x, self.y, self.z = map(f32, (x,y,z))
    def __len__(self): return 3
    def __getitem__(self, i): return (self.x,self.y,self.z)[i]
    def __sub__(self, v): return Vector3(self.x-v[0],self.y-v[1],self.z-v[2])
    def __add__(self, v): return Vector3(self.x+v[0],self.y+v[1],self.z+v[2])
    def scale(self, k): return Vector3(self.x*f32(k),self.y*f32(k),self.z*f32(k))
    @property
    def length(self): return f32(math.sqrt(f32(f32(f32(self.x*self.x)+f32(self.y*self.y))+f32(self.z*self.z))))
    def normalise(self):
        length=self.length
        if abs(length)>1.e-8:
            value=self.scale(f32(1./length));self.x,self.y,self.z=value
Math=module('Math');Math.Vector3=Vector3
BigWorld=module('BigWorld');BigWorld.wg_collideSegment=_BigWorld.wg_collideSegment
class Namespace(object):
    def __init__(self, **kw): self.__dict__.update(kw)
sensor._INTEGER_TYPES=(int,long)
sensor._native_geometry=Namespace(box_probe=lambda *a,**k:None, box_envelope=lambda *a,**k:None,
    motion_witnesses=lambda *a,**k:None,contact_members=lambda *a,**k:None)
sensor._native_sensor=None
sensor._CATALOG_POINT_EPSILON=.075;sensor._SHOT_RAY_EPSILON=.0001
sensor._DESTRUCTIBLE_BIN_METRES=8.;sensor._SOFT_STATIC_MAX_SKIPS=4
sensor.VEHICLE_SKIP_FLAGS=0x50
sensor._DIAGNOSTICS_ENABLED=False
sensor.observed_ray=diagnostics.observed_ray
sensor.combat_count=lambda *a,**k:None
'''

CONSUMER_MODULES = r'''
sensor._DEFAULT_POINT_BOX_PROBE_1513=sensor._point_in_world_box
sensor._DEFAULT_SEGMENT_BOX_PROBE_1513=sensor._segment_world_box_interval
sensor._DEFAULT_CONTACT_BOUNDS=sensor._bin_keys_for_bounds
sensor._DEFAULT_CONTACT_CELL=sensor._destructible_bin_key
sensor._DEFAULT_DESTR_AUTHORITY=sensor._get_destr_authority
sensor._DEFAULT_GROUND_FILTER=sensor.ground_collision_filter
sensor._MOTION_SKIN_LAWS_1513=tuple(getattr(sensor,n) for n in (
    '_point_in_world_box','_segment_world_box_interval','_projected_box_interval_1513','_original_side_face_1513'))
sensor._native_query_laws=tuple((sensor.__dict__,n,getattr(sensor,n)) for n in SENSOR_LAWS)
authority._native_query_laws=tuple((authority.__dict__,n,getattr(authority,n),getattr(authority,n).func_code) for n in ('is_destroyed','destroyed_keys'))
'''

CONSUMER_CASES = r'''
TRIANGLE=((-1.,0.,-1.),(0.,0.,1.),(1.,0.,-1.))
START=(0.,10.,0.); END=(0.,-10.,0.)
import offline_math_batch_native as backend
def reset():
    Host.removeSpace(1);Host.addSpace(1)
    sensor._destructible_catalog={'baked_instances':{},'tree_instances':{}}
    sensor.g_offh_destr_instances={}
    sensor.g_offh_destr_contact_bins={(0,0):set([(10,1)])}
    sensor.g_offh_destr_broken_cache={}
    sensor.g_offh_destr_speculative=set()
    sensor.g_offh_tree_state={'native_committed':set()}
    sensor.g_offh_destr_runtime_space=1
    sensor.g_offh_destr_ground_skips=0
    sensor.g_offh_destr_native_fragile_replacements=set()
    sensor.g_offh_destr_isolated_slots=set();sensor.g_offh_destr_isolated_chunks=set()
    authority._state={'chunks':{10:{'keys':set()}}}
def query(ground=False):
    owner=Namespace(_avatar=Namespace(spaceID=1),_runtime=Namespace(math=Math,bigworld=BigWorld),
                    _generation=1,_bots=Namespace(round_id=1))
    q=engine.EngineQuery(owner,backend)
    batch=sensor.ground_filter_batch_1513(sensor.ground_collision_filter) if ground else None
    engine._native_query_begin(q,batch)
    ctx=Bridge.open(q.capabilities)
    assert ctx is not None, 'canonical query was not bound'
    return q,ctx
def surface(material,distance,item=1,chunk=10,flags=0,raises=False):
    Host.addSurface(1,(material<<8)|flags,distance,TRIANGLE,None,'raise' if raises else (chunk,item))
def plain(hit):
    return None if hit is None else (tuple(hit[0]),tuple(hit[1]),hit[2],tuple(hit[3]),hit[4],hit[5])
def reference(keep):
    return plain(sensor.collide_motion_segment(1,Vector3(*START),Vector3(*END),keep,BigWorld.wg_collideSegment))
def native(ctx,keep,ground=False,useGround=False):
    called=[]
    def profile(frame,event,arg):
        if event=='call' and frame.f_globals.get('__name__','').startswith(PREFIX):
            called.append((frame.f_globals['__name__'],frame.f_code.co_name))
    sys.setprofile(profile)
    try: result=Bridge.ray(ctx,keep,int(ground),START,END,int(useGround))
    finally: sys.setprofile(None)
    assert not called, called
    return result
def check(keep,q,ctx):
    expected=reference(keep)
    status,actual=native(ctx,keep)
    assert status==(2 if expected is not None else 1),(status,actual,expected)
    assert actual==expected,(actual,expected)
    assert Bridge.before(ctx)
    q.raise_failure()

reset(); surface(4,3.)
q,ctx=query();check(None,q,ctx)
for broken in (False,True):
    for material in (70,71,86,87,100,101):
        reset();surface(material,1.);surface(4,3.,2)
        if broken: authority._state['chunks'][10]['keys'].add((1,material))
        q,ctx=query()
        keep=sensor._live_broken_collision_filter_1513([(10,1)])
        check(keep,q,ctx)
reset();surface(71,1.);surface(4,3.,2)
q,ctx=query();keep=sensor._live_broken_collision_filter_1513([(10,1)])
assert native(ctx,keep)[1][2]==71
assert Bridge.before(ctx)
authority._state['chunks'][10]['keys'].add((1,71))
assert Bridge.after(ctx)
assert native(ctx,keep)[1][2]==4
assert Bridge.before(ctx)
q.raise_failure()

# Trace records both rejected and accepted native candidates at its old cap.
reset();surface(71,1.);surface(4,3.,2)
authority._state['chunks'][10]['keys'].add((1,71))
q,ctx=query();trace={};keep=world._trace_collision_filter(sensor._live_broken_collision_filter_1513([(10,1)]),trace)
status,hit=native(ctx,keep);assert hit[2]==4
assert trace['native_surface_candidates']==[]
assert Bridge.before(ctx)
assert trace['native_surface_candidates']==[(71,0,1,10,False),(4,0,2,10,True)],trace

# Ground preparation does not call its Python provider for each ray.
reset();surface(71,1.);surface(4,3.,2)
authority._state['chunks'][10]['keys'].add((1,71))
q,ctx=query(True);assert Bridge.groundAvailable(ctx)
status,hit=native(ctx,None,True,True);assert status==2 and hit[2]==4
assert Bridge.before(ctx)

# Unknown custom filters explicitly retain the generic path.
reset();surface(4,1.);q,ctx=query()
assert native(ctx,lambda *a:False)[0]==0
assert Bridge.before(ctx)

# The outer factory code alone does not prove its nested keep callback.
# A legal reconstructed closure must retain the generic callback's effects.
reset();surface(4,1.);q,ctx=query()
keep=sensor._live_broken_collision_filter_1513([(10,1)])
custom_calls=[]
def custom_keep(hit,identity):
    custom_calls.append((tuple(hit),identity));return False

def captured_cell(value):return (lambda:value).func_closure[0]
cells=list(keep.func_closure)
cells[keep.func_code.co_freevars.index('keep_native_surface')]=captured_cell(custom_keep)
custom=types.FunctionType(keep.func_code,keep.func_globals,keep.func_name,
    keep.func_defaults,tuple(cells))
tag=list(keep._native_query_filter);tag[4]=custom_keep
custom._native_query_filter=tuple(tag)
Host.visits() # This fixture drains its visit ledger on read.
assert native(ctx,custom)[0]==0
assert Host.visits()==[] and custom_calls==[]
assert q._native_query_status==('generic','custom_keep_native_surface')
assert reference(custom) is None
assert custom_calls==[((4,0,1,10),(10,1))],custom_calls
q.raise_failure()

# The typed path rounds every endpoint through the same float32 boundary.
saved_start,saved_end=START,END
for START,END in (((-.123456789,10.,.234567891),(.345678912,-10.,-.456789123)),
                  ((16777216.,10.,0.),(16777217.,-10.,0.)),
                  ((1.,2.,3.),(1.+1.e-10,2.,3.))):
    reset();surface(4,1.);q,ctx=query();check(None,q,ctx)
START,END=saved_start,saved_end

# A stale different-material face is excluded only inside its proved broken
# component. This exercises the actual scene/filter/recast stack, not only
# the standalone skin law's returned interval.
for budget in (0,4):
    reset();surface(73,1.);surface(4,3.,2)
    sensor._SOFT_STATIC_MAX_SKIPS=budget
    axes=((1.,0.,0.),(0.,1.,0.),(0.,0.,1.))
    sensor.g_offh_destr_instances={(10,1):{'kind':'structure','boxes':(
        ((0.,9.,0.),axes,74),((0.,13.,0.),axes,73))}}
    authority._state['chunks'][10]['keys'].add((1,74))
    q,ctx=query();keep=sensor._live_broken_collision_filter_1513([(10,1)])
    check(keep,q,ctx)
sensor._SOFT_STATIC_MAX_SKIPS=4

# A failed identity preserves the horizontal remembered RuntimeError.
reset();surface(4,1.,raises=True);q,ctx=query()
status,hit=native(ctx,None);assert status==3 and hit is None
try:q.raise_failure()
except RuntimeError as error:assert str(error)=='host identity owner failed'
else:raise AssertionError('missing identity failure')

# Generation invalidation remains terminal, not an ordinary miss.
reset();surface(4,1.);q,ctx=query();q.owner._generation=2
status,hit=native(ctx,None);assert status==3
try:q.raise_failure()
except RuntimeError as error:assert str(error)=='Native engine query owner changed'
else:raise AssertionError('missing ownership failure')

# Only the four errors contained by the maintained ground wrapper recover.
for exception in (AttributeError,IndexError,TypeError,ValueError,RuntimeError,MemoryError):
    reset();surface(4,1.,raises=True);Bridge.identityError(exception);q,ctx=query()
    status,hit=native(ctx,None,True)
    if exception in (AttributeError,IndexError,TypeError,ValueError):
        assert status==1 and hit is None,(exception,status,hit)
        q.raise_failure()
    else:
        assert status==3,(exception,status)
        try:q.raise_failure()
        except exception:pass
        else:raise AssertionError('ground erased a terminal error')
Bridge.identityError(None)

class Diagnostic(object):
    def __init__(self):
        self.counts={};self._stack=[];self._stages={};self._actor_key=None;self._actors={}
    def count(self,name,value=1):self.counts[name]=self.counts.get(name,0)+value
    def geometry(self,*args):pass
    def _add_stage(self,*args):pass

HOT=set(('_ray','_segment','_skin','_filter','_alias','_can_recast','_skipped','_scale','_normalise',
         'reject_broken_skin','_compiled_motion_skin_1513','_owned_component_skin_1513','_anonymous_original_surface_1513'))
def world_case(entry,generic=False,passive=False,fail_after_effect=False):
    reset()
    if fail_after_effect:
        wall=((-1.,-1.,0.),(0.,1.,0.),(1.,-1.,0.))
        Host.addSurface(1,4<<8,1.,wall,None,(10,1))
    diagnostic=Diagnostic();diagnostics.current=lambda:diagnostic
    q,ctx=query(True);del ctx;diagnostic.counts.clear()
    if generic:q._native_query_binding=None
    hot=[];effects=[];opcodes=[]
    keep=sensor._live_broken_collision_filter_1513([(10,1)])
    snapshot=(.123456789,0.,.234567891,.15,5.,.04,.2 if passive else 0.,0.,0.,
              -1.5,1.5,3.5,3.5,0.,2.,int(passive),0,0,0)
    def dispatch(op,rows):
        q._live();opcodes.append(op)
        if op==20:return q.capabilities,(4,.001,1.28)
        if op==21:return None
        if op==1:return keep
        if op==7:
            a=rows[0][0];return sensor.ground_collision_filter(a[0],a[2])
        if op==5:
            effects.append(('resolve',plain(rows[0][2])))
            if fail_after_effect:q.owner._generation+=1
            return (0,)
        if op==6:
            effects.append(('trace',plain(rows[0][2])));return None
        raise AssertionError(('unexpected world opcode',op))
    def profile(frame,event,arg):
        if event=='call' and frame.f_globals.get('__name__','').startswith(PREFIX) and frame.f_code.co_name in HOT:
            hot.append(frame.f_code.co_name)
    sys.setprofile(profile)
    status=None;error=None
    try:
        if entry=='world':status=backend.world_run_native(snapshot,dispatch,q.capabilities,(4,.001,1.28))
        else:status=Bridge.motionWorld(snapshot,dispatch)
        q.raise_failure()
    except Exception as failure:error=(type(failure).__name__,str(failure))
    finally:sys.setprofile(None)
    if not generic:
        assert not hot,(entry,hot)
        assert diagnostic.counts.get('native_query_ray',0)>0,(entry,diagnostic.counts,q._native_query_status)
    else:
        assert hot,(entry,'generic path did not exercise capabilities')
    if fail_after_effect:
        assert error==('RuntimeError','Native engine query owner changed'),(entry,status,error)
        assert len(effects)==1 and effects[0][0]=='resolve',(entry,effects)
    else:
        assert status==0 and error is None,(entry,status,error)
        if passive:
            assert (7 not in opcodes) if not generic else (7 in opcodes),(entry,opcodes)
    print('entry=%s generic=%s passive=%s native_rays=%s python_hot_callbacks=%s effects=%s' %
          (entry,generic,passive,diagnostic.counts.get('native_query_ray',0),len(hot),len(effects)))
    diagnostics.current=lambda:None
    return effects,error
for entry in ('world','motion'):
    for passive in (False,True):
        actual=world_case(entry,False,passive)
        expected=world_case(entry,True,passive)
        assert actual==expected,(entry,passive,actual,expected)
    actual=world_case(entry,False,False,True)
    expected=world_case(entry,True,False,True)
    assert actual==expected,(entry,actual,expected)
del ctx,q
gc.collect()
print('native query provider contract passed')
'''


def build_bridge(test, path, fork):
    python = Path(os.environ.get('WOT_PYTHON27', '/Users/peng/.pyenv/versions/2.7.18/bin/python2.7'))
    config = python.with_name('python2.7-config')
    if not config.exists(): test.skipTest('Pinned CPython 2.7 development files unavailable')
    cxx = os.environ.get('CXX', 'c++')
    if not shutil.which(cxx): test.skipTest('C++ compiler unavailable')
    for header in FAKE_HEADERS:
        target = path / 'include' / header
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text('#pragma once\n')
    engine_includes(test, path)
    (path / 'include/stock_ground_water_query.inc').write_text(ground_water_query())
    source = (ROOT / 'tests/stock_battle_scene_queries_host.hpp').read_text()
    anchor = '    bool operator==(const Vector3 & o) const'
    source = source.replace(anchor, '''    Vector3 operator+(const Vector3 & o) const { return Vector3(x+o.x,y+o.y,z+o.z); }
    Vector3 operator*(float scale) const { return Vector3(x*scale,y*scale,z*scale); }
    float length() const { return std::sqrt(x*x+y*y+z*z); }
    void normalise() { float len=length(); if (std::abs(len)>1.e-8f) { float r=1.f/len; x*=r;y*=r;z*=r; } }
''' + anchor)
    (path / 'include/stock_offline_query_bridge_host.hpp').write_text(source)
    scene = (ROOT / 'tests/stock_battle_scene_queries_host.cpp').read_text().replace(
        '#include "stock_battle_scene_queries_host.hpp"', '#include "stock_offline_query_bridge_host.hpp"')
    scene = scene.replace('PyErr_SetString(PyExc_RuntimeError, "host identity owner failed");',
        'PyErr_SetString(hostIdentityException ? hostIdentityException : PyExc_RuntimeError, "host identity owner failed");')
    (path / 'include/stock_offline_query_scene_host.inc').write_text(scene)
    flags = [f for f in shlex.split(subprocess.check_output([str(config), '--cflags'], text=True))
             if f != '-DNDEBUG' and not f.startswith('-O')]
    libs = shlex.split(subprocess.check_output([str(config), '--ldflags'], text=True))
    binary = path / 'bridge-host'
    spec = importlib.util.spec_from_file_location('math_bindings', ROOT / 'bigworld/patch_offline_math_bindings.py')
    adapter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(adapter)
    generated = path / 'fork'
    shutil.copytree(fork / 'native', generated / 'native')
    adapter.patch(generated)
    binding_cpp = generated / 'native/offline_math_batch_native.cpp'
    binding_cpp.write_text(binding_cpp.read_text() + MOTION_ENTRY)
    subprocess.run([cxx, *native_test_compile_flags(cxx), '-std=c++17', '-g', '-fno-omit-frame-pointer',
                    '-O0',
                    '-Wno-register', '-Wno-deprecated-register', '-Wno-writable-strings', '-Wno-unused-function', '-UNDEBUG', '-ffp-contract=off',
                    '-DWOT_PYTHON_C_API', '-DWOT_NATIVE_ENGINE_QUERY',
                    '-I' + str(path / 'include'), '-I' + str(ROOT / 'bigworld'), '-I' + str(fork / 'native'),
                    str(ROOT / 'tests/stock_offline_query_bridge_host.cpp'),
                    str(ROOT / 'bigworld/stock_offline_query_skin.cpp'),
                    *[str(generated / 'native' / name) for name in adapter.SOURCES],
                    *flags, *libs, '-o', str(binary)], check=True)
    return binary


MOTION_ENTRY = r'''
// Test-only entry into the production motion Engine's world adapter. Actor
// movement integration is not replaced or claimed by this boundary test.
extern "C" PyObject * stockTestMotionWorld(PyObject *, PyObject * args) {
    try {
        require_sequence(args,2,true);
        double v[19];world_array(items(args)[0],v,19);
        WorldSnapshot q={};std::memcpy(q.position,v,3*sizeof(double));
        q.yaw=v[3];q.speed=v[4];q.dt=v[5];q.pitch=v[6];q.roll=v[7];q.motion_yaw=v[8];
        std::memcpy(q.bounds,v+9,4*sizeof(double));std::memcpy(q.heights,v+13,2*sizeof(double));
        q.passive=v[15]!=0.;q.airborne=v[16]!=0.;q.exact_footprint=v[17]!=0.;q.supplied_departing=v[18]!=0.;
        auto context=std::make_shared<SimulationContext>(1,1);
        sim_motion_bridge::Engine engine(context,items(args)[1]);
        offline_simulation::motion::State state;
        std::copy(q.position,q.position+3,state.pose.position.begin());
        state.pose.yaw=q.yaw;state.pose.pitch=q.pitch;state.pose.roll=q.roll;state.speed=q.speed;
        offline_simulation::motion::Descriptor descriptor;
        std::copy(q.bounds,q.bounds+4,descriptor.bounds.begin());
        std::copy(q.heights,q.heights+2,descriptor.heights.begin());
        const offline_simulation::ActorKey actor{offline_simulation::ActorKind::Bot,1};
        const bool bypass=engine.begin_world(actor,state,descriptor,q,true,true,false);
        int result=0;
        if(!bypass)result=wot_world_run(&q,[](void * raw,unsigned op,const WorldRequest * request,
            unsigned count,WorldResponse * response) {
                return static_cast<sim_motion_bridge::Engine *>(raw)->dispatch_world(op,request,count,response);
            },&engine);
        engine.end_world();
        return int_new(result);
    } catch(...) { if(PyErr_Occurred())return NULL;return int_new(-2); }
}
'''


class NativeBridgeTests(unittest.TestCase):
    def test_provider_contract(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source: self.skipTest('Fixed v0.10.1 source input is required')
        with tempfile.TemporaryDirectory(prefix='native-query-bridge-') as folder:
            root = Path(folder)
            binary = build_bridge(self, root, Path(source))
            consumer = root / 'consumer.py'
            consumer.write_text(consumer_source(Path(source)))
            if os.environ.get('WOT_QUERY_HOST_EXPORT'):
                shutil.copy2(binary, os.environ['WOT_QUERY_HOST_EXPORT'])
            if os.environ.get('WOT_QUERY_CONSUMER_EXPORT'):
                shutil.copyfile(consumer, os.environ['WOT_QUERY_CONSUMER_EXPORT'])
            env = dict(os.environ, PYTHONDONTWRITEBYTECODE='1', ASAN_OPTIONS='detect_leaks=0')
            process = subprocess.run([str(binary), str(consumer)], env=env, capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            self.assertIn('native query provider contract passed', process.stdout)
            self.assertIn('entry=world generic=False', process.stdout)
            self.assertIn('entry=motion generic=False', process.stdout)
            print(process.stdout.rstrip())


if __name__ == '__main__':
    unittest.main()
