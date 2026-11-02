"""Execute the countdown body bind before any player motion in real CPython 2.7.

The extracted production functions own the Python bindings and rollback. The
small typed Battle fixture exposes their cold resource/lifetime boundary. The
complete production Owner constructor and CameraHistory reset run without a
player driver or physical slice. This proves provider ownership, not native
Windows body rendering.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_vehicle_physics_owner import block
from test_stock_wire_codec import python27_audit_runtime

ROOT = Path(__file__).resolve().parents[1]

HOST = r'''
#define WOT_CLIENT_SELF_HOST_BUILD
#include "stock_native_bot_python.hpp"
#include "stock_native_client_state.hpp"
#include <deque>
namespace stock_native_client_drive {
using Point=stock_native_client_state::Point;
@MOTION_SAMPLE@;
@CAMERA_MOTION@;
@CAMERA_DECLARATION@;
@HYDRAULIC_PITCH@;
}
#include <functional>
#include <memory>
#include <set>
namespace p=stock_native_bot_python;
namespace stock_native_client_state {
@HANDLES@
@STATE_FIND@
}
namespace native_battle {
using Object=stock_native_client_state::Handle;
@BORROW@
@DESCRIBE@
struct Local {
    Object model,swinging_animator,swinging_restore,placing_compensation;
    std::shared_ptr<void> presentation;
    stock_native_client_state::Point position{},camera_velocity{};
};
struct Battle {
    std::unique_ptr<Local> local=std::make_unique<Local>();
    struct Engine {Object math,bigworld,constants;}engine;
    std::shared_ptr<stock_native_client_state::State> vehicles=std::make_shared<stock_native_client_state::State>();
    std::set<std::string> disabled;
    std::vector<std::string> warnings;
    unsigned owners=0;
    bool featureEnabled(const std::string& name)const{return !disabled.count(name);}
    void warn(const std::string& name,const std::string& error){disabled.insert(name);warnings.push_back(error);}
};
}
using native_battle::Battle;
@FIELD@
@PUT@
@OBJ@
@INVOKE_ARGS@
@INVOKE@
@CONSTRUCT@
@MATRIX@
namespace state=stock_native_client_state;
using stock_native_client_drive::Point;
using stock_native_client_drive::CameraHistory;
using stock_native_client_drive::HydraulicPitch;
namespace stock_native_client_drive {
@CAMERA_CONSTRUCTOR@
@CAMERA_RESET@
}
namespace query {
class ProjectileTurrets {};
std::shared_ptr<ProjectileTurrets> projectileTurrets(Battle&) {return std::make_shared<ProjectileTurrets>();}
}
@NUMBER@
@CONSTANTS@
@OWNER@;

using Handle=std::shared_ptr<Owner>;
using SelfOwner=Owner;
@ACQUIRE@
@OPTIONAL@
@BIND@
@ATTACH@
@RETIRE@
PyObject* failure(const std::exception& error){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,error.what());return nullptr;}
void release(PyObject* capsule){auto* b=static_cast<Battle*>(PyCapsule_GetPointer(capsule,"countdown.battle"));if(b)delete b;else PyErr_Clear();}
Battle& battle(PyObject* capsule){auto* b=static_cast<Battle*>(PyCapsule_GetPointer(capsule,"countdown.battle"));if(!b)throw p::Error("Invalid countdown fixture battle");return *b;}
PyObject* create(PyObject*,PyObject* args){PyObject* model,*math;int current=0,local=1,exists=1;if(!PyArg_ParseTuple(args,"OO|iii",&model,&math,&current,&local,&exists))return nullptr;try{
 auto b=std::make_unique<Battle>();b->local->model=native_battle::borrow(model);b->engine.math=native_battle::borrow(math);
 auto constants=p::dict();p::set(constants.get(),"SERVER_TICK_LENGTH",p::number(.1));b->engine.constants=native_battle::borrow(constants.get());
 auto bigworld=p::dict();b->engine.bigworld=native_battle::borrow(bigworld.get());b->vehicles->local_player_id=7;
 if(exists){auto actor=std::make_shared<state::Actor>();actor->key="player:7";actor->local=local;actor->ready=true;actor->has_current=current;actor->current={999.,998.,997.,.9,.8,.7};b->vehicles->actors[actor->key]=actor;}
 auto root=field(model,"matrix");if(root.get()!=Py_None)b->local->position=p::numbers<3>(field(root.get(),"position").get());b->local->camera_velocity={.5,0.,1.};
 auto out=PyCapsule_New(b.get(),"countdown.battle",release);if(out)b.release();return out;
 }catch(const std::exception& e){return failure(e);}}

PyObject* attach(PyObject*,PyObject* args){PyObject* capsule,*entity;if(!PyArg_ParseTuple(args,"OO",&capsule,&entity))return nullptr;try{auto& b=battle(capsule);auto before=b.local->presentation;attachSelfPresentation(b,entity);if(before!=b.local->presentation)++b.owners;Py_RETURN_NONE;}catch(const std::exception& e){return failure(e);}}
PyObject* snapshot(PyObject*,PyObject* args){PyObject* capsule;if(!PyArg_ParseTuple(args,"O",&capsule))return nullptr;try{auto& b=battle(capsule);auto out=p::dict();p::set(out.get(),"animator",obj(b.local->swinging_animator));p::set(out.get(),"restore",obj(b.local->swinging_restore));p::set(out.get(),"compensation",obj(b.local->placing_compensation));p::set(out.get(),"owners",p::integer(b.owners));p::set(out.get(),"warnings",p::strings(b.warnings));
 auto* actor=b.vehicles->find("player:7");p::set(out.get(),"has_current",p::boolean(actor&&actor->has_current));p::set(out.get(),"ready",p::boolean(actor&&actor->ready));if(actor)p::set(out.get(),"actor_current",p::tupleNumbers(actor->current));
 if(b.local->presentation){auto owner=std::static_pointer_cast<Owner>(b.local->presentation);p::set(out.get(),"camera_clock",p::number(owner->camera.clock()));p::set(out.get(),"camera_samples",p::integer(owner->camera.samples().size()));if(!owner->camera.samples().empty())p::set(out.get(),"camera_spawn",p::tupleNumbers(owner->camera.samples().front().position));p::set(out.get(),"camera_velocity",p::tupleNumbers(owner->velocity));}
 return out.release();}catch(const std::exception& e){return failure(e);}}
PyObject* retire(PyObject*,PyObject* args){PyObject* capsule;if(!PyArg_ParseTuple(args,"O",&capsule))return nullptr;try{retireSelfPresentation(battle(capsule));Py_RETURN_NONE;}catch(const std::exception& e){return failure(e);}}
PyObject* epoch(PyObject*,PyObject* args){PyObject* capsule;if(!PyArg_ParseTuple(args,"O",&capsule))return nullptr;try{++battle(capsule).vehicles->epoch;Py_RETURN_NONE;}catch(const std::exception& e){return failure(e);}}
PyObject* factory(PyObject* self,PyObject* args){return PyObject_CallObject(self,args);}
PyMethodDef factoryDef={"native_matrix_factory",factory,METH_VARARGS,"Fixture native Math boundary."};
PyObject* nativeFactory(PyObject*,PyObject* args){PyObject* value;if(!PyArg_ParseTuple(args,"O",&value))return nullptr;return PyCFunction_New(&factoryDef,value);}
PyObject* dispatch(PyObject* self,PyObject* args){auto object=PyTuple_GET_ITEM(self,0),name=PyTuple_GET_ITEM(self,1);p::Ref method(PyObject_GetAttr(object,name));return PyObject_CallObject(method.get(),args);}
PyMethodDef dispatchDef={"native_matrix_primitive",dispatch,METH_VARARGS,"Fixture native Math boundary."};
PyObject* wrap(PyObject*,PyObject* args){return PyCFunction_New(&dispatchDef,args);}
PyMethodDef methods[]={{"create",create,METH_VARARGS,""},{"attach",attach,METH_VARARGS,""},{"snapshot",snapshot,METH_VARARGS,""},{"retire",retire,METH_VARARGS,""},{"epoch",epoch,METH_VARARGS,""},{"native_factory",nativeFactory,METH_VARARGS,""},{"wrap",wrap,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC initcountdown_self_test(){Py_InitModule("countdown_self_test",methods);}
'''

CASES = r'''
import countdown_self_test as native
class O(object):
 def __init__(self,**values):self.__dict__.update(values)
class M(object):
 created=0
 def __init__(self):
  M.created+=1;self.identity_calls=0;self.setIdentity=native.wrap(self,'identity')
 def identity(self):self.identity_calls+=1
class Animator(object):
 def __init__(self):
  self.comp=object();self.world=object();self.comp_writes=0;self.world_writes=0;self.fail_world=False;self.fail_rollback=False
 @property
 def placingCompensationMatrix(self):return self.comp
 @placingCompensationMatrix.setter
 def placingCompensationMatrix(self,value):
  self.comp_writes+=1
  if self.fail_rollback and self.comp_writes>1:raise RuntimeError('compensation rollback refused')
  self.comp=value
 @property
 def worldMatrix(self):return self.world
 @worldMatrix.setter
 def worldMatrix(self,value):
  self.world_writes+=1
  if self.fail_world and self.world_writes==1:raise RuntimeError('world assignment refused')
  self.world=value
math=O(Matrix=native.native_factory(M))
def fixture():
 animator=Animator();appearance=O(swingingAnimator=animator);entity=O(appearance=appearance)
 root=O(position=(11.,12.,13.),yaw=.2,pitch=.03,roll=-.01)
 model=O(matrix=root);battle=native.create(model,math)
 return battle,entity,model,animator,root
b,e,model,a,root=fixture();before=dict(root.__dict__);old=(a.comp,a.world)
# The only operation before countdown completion is the production cold bind.
# There is no motion driver in this host, so it cannot hide an initial slice.
native.attach(b,e);s=native.snapshot(b)
assert a.world is root and a.comp is s['compensation']
assert s['restore']==(a,old[0],old[1]) and s['owners']==1 and s['warnings']==[]
assert root.__dict__==before and a.comp.identity_calls==1
assert s['has_current'] is False and s['ready'] is True
assert s['actor_current']==(999.,998.,997.,.9,.8,.7,0.,0.)
assert s['camera_clock']==0. and s['camera_samples']==1 and s['camera_spawn']==root.position
assert s['camera_velocity']==(.5,0.,1.)
# The exact Owner guard also admits a player that already has a motion receipt,
# and continues to reject missing or nonlocal actor identity.
unused,ready_entity,ready_model,ready_animator,ready_root=fixture()
ready=native.create(ready_model,math,1);native.attach(ready,ready_entity)
assert native.snapshot(ready)['has_current'] is True
for current,local,exists in ((0,0,1),(1,0,1),(0,1,0),(1,1,0)):
 invalid=native.create(model,math,current,local,exists)
 try:native.attach(invalid,e)
 except RuntimeError as error:assert 'no local player' in str(error)
 else:raise AssertionError('Invalid local actor identity admitted')
writes=(a.comp_writes,a.world_writes);restore=s['restore'];created=M.created
native.attach(b,e);s=native.snapshot(b)
assert s['restore'] is restore and s['owners']==1 and M.created==created
assert writes==(a.comp_writes,a.world_writes) and root.__dict__==before
# A stock appearance refresh owns the old animator. Bind only its replacement,
# retaining the replacement's own providers for eventual local teardown.
replacement=Animator();prior=(replacement.comp,replacement.world)
e.appearance.swingingAnimator=replacement
native.attach(b,e);s=native.snapshot(b)
assert s['animator'] is replacement and s['restore']==(replacement,prior[0],prior[1])
assert replacement.world is root and replacement.comp is a.comp
assert writes==(a.comp_writes,a.world_writes) and root.__dict__==before
# A failed replacement never mutates the animator retired by stock appearance.
failed=Animator();failed.fail_world=True;prior=(failed.comp,failed.world)
e.appearance.swingingAnimator=failed;writes=(replacement.comp_writes,replacement.world_writes)
native.attach(b,e);s=native.snapshot(b)
assert (failed.comp,failed.world)==prior and s['animator'] is None and s['restore'] is None
assert writes==(replacement.comp_writes,replacement.world_writes)
# Native setter failure rolls both providers back and retains no partial bind.
b,e,model,a,root=fixture();old=(a.comp,a.world);a.fail_world=True
native.attach(b,e);s=native.snapshot(b)
assert (a.comp,a.world)==old and s['animator'] is None and s['restore'] is None
assert len(s['warnings'])==1 and 'world assignment refused' in s['warnings'][0]
writes=(a.comp_writes,a.world_writes);native.attach(b,e)
assert writes==(a.comp_writes,a.world_writes) # Failed optional feature is fenced.
# A failed rollback retains the record needed by teardown; it is never silently
# forgotten while a native controller may still refer to the battle matrix.
b,e,model,a,root=fixture();old=(a.comp,a.world);a.fail_world=True;a.fail_rollback=True
native.attach(b,e);s=native.snapshot(b)
assert s['animator'] is a and s['restore']==(a,old[0],old[1]) and len(s['warnings'])==1
# No animator/provider is a safe no-op at this cold boundary.
b,e,model,a,root=fixture();e.appearance.swingingAnimator=None
native.attach(b,e);assert native.snapshot(b)['animator'] is None
assert (a.comp_writes,a.world_writes)==(0,0)
b,e,model,a,root=fixture();model.matrix=None
native.attach(b,e);assert native.snapshot(b)['animator'] is None
assert (a.comp_writes,a.world_writes)==(0,0)
# The real acquire fence replaces an epoch-retired owner; repeated retirement
# stays harmless and another entity cannot reuse the previous owner identity.
b,e,model,a,root=fixture();native.attach(b,e);native.epoch(b);native.attach(b,e)
assert native.snapshot(b)['owners']==2
native.retire(b);native.retire(b);native.attach(b,e)
assert native.snapshot(b)['owners']==3
other=O(appearance=O(swingingAnimator=Animator()));native.attach(b,other)
assert native.snapshot(b)['owners']==4
print('Production countdown attach/bind: no motion, idempotence, animator refresh, rollback and owner fences passed')
'''


class CountdownSelfPresentationTests(unittest.TestCase):
    def test_startup_binds_body_after_attach_and_before_running(self):
        source = (ROOT / 'bigworld/stock_native_battle_startup.cpp').read_text()
        finish = block(source, 'void finishEntityStartup(Battle& b)')
        model = finish.index('local::attachPresentation(b);')
        query = finish.index('visibility::start(b);')
        prepare = finish.index('if (!b.worker) local::prepare(b);')
        bind = finish.index('if (!b.worker) stock_native_client_drive::attachSelfPresentation(')
        running = finish.index('b.phase = Phase::Running;')
        self.assertLess(model, bind)
        self.assertLess(query, bind)
        self.assertLess(prepare, bind)
        self.assertLess(bind, running)

    def test_production_cold_bind_and_lifecycle_in_python27(self):
        python = python27_audit_runtime()
        prefix = Path(subprocess.check_output([python, '-c', 'import sys;print(sys.prefix)'], text=True).strip())
        presentation = (ROOT / 'bigworld/stock_native_client_self_presentation.cpp').read_text()
        battle = (ROOT / 'bigworld/stock_native_battle.cpp').read_text()
        battle_header = (ROOT / 'bigworld/stock_native_battle.hpp').read_text()
        state = (ROOT / 'bigworld/stock_native_client_state.cpp').read_text()
        camera = (ROOT / 'bigworld/stock_native_client_drive_presentation.cpp').read_text()
        camera_header = (ROOT / 'bigworld/stock_native_client_drive_presentation.hpp').read_text()
        snippets = {
            'FIELD': block(presentation, 'p::Ref field('),
            'PUT': block(presentation, 'void put('),
            'OBJ': block(presentation, 'p::Ref obj('),
            'INVOKE_ARGS': block(presentation, 'p::Ref invoke(PyObject*o,const char*n,PyObject*args)'),
            'INVOKE': block(presentation, 'p::Ref invoke(PyObject*o,const char*n) '),
            'CONSTRUCT': block(presentation, 'p::Ref construct('),
            'MATRIX': block(presentation, 'p::Ref matrix('),
            'OWNER': block(presentation, 'struct Owner {'),
            'NUMBER': block(presentation, 'double number('),
            'CONSTANTS': presentation[presentation.index('constexpr double swing_move_seconds'):presentation.index('struct Owner {')],
            'STATE_FIND': '\n'.join(block(state, signature) for signature in (
                'const Actor* State::find(', '\nActor* State::find(')),
            'MOTION_SAMPLE': block(camera_header, 'struct MotionSample {'),
            'CAMERA_MOTION': block(camera_header, 'struct CameraMotion {'),
            'CAMERA_DECLARATION': block(camera_header, 'class CameraHistory {'),
            'HYDRAULIC_PITCH': block(camera_header, 'struct HydraulicPitch {'),
            'CAMERA_CONSTRUCTOR': block(camera, 'CameraHistory::CameraHistory('),
            'CAMERA_RESET': block(camera, 'void CameraHistory::reset('),
            'ACQUIRE': block(presentation, 'Handle acquire('),
            'OPTIONAL': block(presentation, 'void optional('),
            'BIND': block(presentation, 'void bindSwing('),
            'ATTACH': block(presentation, 'void attachSelfPresentation('),
            'RETIRE': block(presentation, 'void retireSelfPresentation('),
            'BORROW': block(battle_header, 'inline Object borrow('),
            'DESCRIBE': block(battle, 'std::string describe('),
            'HANDLES': '\n'.join(block(state, signature) for signature in (
                'Handle::Handle(const Handle&', 'Handle& Handle::operator=(const Handle&', 'Handle::~Handle()')),
        }
        source = HOST
        for name, text in snippets.items():
            source = source.replace('@' + name + '@', text)
        with tempfile.TemporaryDirectory(prefix='countdown-self-presentation-') as directory:
            folder = Path(directory)
            cpp = folder / 'host.cpp'
            cpp.write_text(source)
            command = ['c++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                       '-Wno-register', '-bundle', '-undefined', 'dynamic_lookup',
                       '-isystem', str(prefix / 'include/python2.7'), '-I' + str(ROOT / 'bigworld'), '-I' + str(ROOT.parent),
                       str(cpp), '-o', str(folder / 'countdown_self_test.so')]
            built = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([python, '-B', '-c', CASES], cwd=folder, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
