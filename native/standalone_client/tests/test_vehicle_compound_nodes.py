"""Exercise the production compound attachment owner across Python reentry."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def production_harness():
    source = (ROOT / 'bigworld/vehicle_appearance.cpp').read_text()
    lighting = (ROOT / 'bigworld/stock_local_lighting.cpp').read_text()
    host = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using uint32=unsigned;
namespace BW {using string=std::string;}
struct Vector3 {
 float x=0,y=0,z=0;static Vector3 zero(){return {};}
 Vector3 operator-(const Vector3&v)const{return {x-v.x,y-v.y,z-v.z};}
 Vector3& operator*=(float v){x*=v;y*=v;z*=v;return *this;}
};
struct Matrix {
 float value=0;Vector3 rows[3];static const Matrix identity;
 explicit Matrix(float v=0):value(v){}
 void multiply(const Matrix&a,const Matrix&b){value=a.value+b.value;}
 Vector3 applyToOrigin()const{return {value};}
 Vector3 applyPoint(const Vector3&v)const{return {value+v.x,v.y,v.z};}
 void translation(const Vector3&v){value=v.x;}
 Vector3& operator[](int n){return rows[n];}
};
const Matrix Matrix::identity;
struct BoundingBox {
 Vector3 minBounds()const{return {2,0,0};}
 Vector3 maxBounds()const{return {5,0,0};}
};
struct MatrixProvider {MatrixProvider(bool,int*){};virtual ~MatrixProvider(){}};
#define Py_Header(TYPE,BASE) inline static int s_type_=0;
namespace Moo {
enum {RENDERING_PASS_COLOR,RENDERING_PASS_REFLECTION,RENDERING_PASS_SHADOWS};
struct DrawContext {
 enum {OPAQUE_CHANNEL_MASK=1};
 int pass=0;int renderingPassType()const{return pass;}
 void begin(int){};void end(int){};void flush(int){};
};
struct Context {unsigned frameTimestamp()const{return 1;}};
Context& rc(){static Context value;return value;}
}
thread_local const Moo::DrawContext * activeLocalShadowDraw = NULL;
@LOCAL_SCOPE@;
@LOCAL_CONTEXT@
struct Scene{};struct IntersectionSet{};
struct SceneDrawContext {
 Moo::DrawContext& draw;
 SceneDrawContext(Scene&,Moo::DrawContext&value):draw(value){}
};
namespace DrawHelpers {
 std::function<void(const SceneDrawContext&)> callback;
 void drawSceneVisibilitySet(const SceneDrawContext&context,Scene&,const IntersectionSet&){callback(context);}
}
@LOCAL_DRAW@
struct MatrixLiaison {
 virtual ~MatrixLiaison(){};
 virtual const Matrix& getMatrix()const=0;
 virtual bool setMatrix(const Matrix&)=0;
};
struct PyObject {int refs=0;virtual ~PyObject(){};};
template<class T> struct Ptr {
 T* p=nullptr;
 Ptr(){};Ptr(T*v):p(v){if(p)++p->refs;}
 Ptr(const Ptr&v):Ptr(v.p){}
 ~Ptr(){if(p&&!--p->refs)delete p;}
 Ptr& operator=(const Ptr&v){Ptr hold(v);std::swap(p,hold.p);return *this;}
 T* get()const{return p;}T* operator->()const{return p;}
 explicit operator bool()const{return p!=nullptr;}
};
using PyObjectPtr=Ptr<PyObject>;
PyObject none;int error=0;int PyExc_TypeError=1;int PyExc_ValueError=2;
#define Py_RETURN_NONE return &none
void PyErr_SetString(int value,const char*){error=value;}
bool PyErr_Occurred(){return error!=0;}
struct Provider {std::function<void()> callback;float value=0;
 void matrix(Matrix&m){if(callback)callback();m.value=value;}
};
using MatrixProviderPtr=std::shared_ptr<Provider>;
struct PyAttachment:PyObject {
 MatrixLiaison* liaison=nullptr;bool attached=false,inWorld=false;
 int enters=0,leaves=0,detaches=0,ticks=0,draws=0,updates=0;float observedPose=0;
 std::function<void()> onAttach,onEnter,onLeave,onDetach,onTick,onBounds;
 static bool Check(PyObject*p){return dynamic_cast<PyAttachment*>(p)!=nullptr;}
 virtual bool attach(MatrixLiaison*p){if(onAttach)onAttach();liaison=p;attached=true;return true;}
 virtual void detach(){attached=false;liaison=nullptr;++detaches;if(onDetach)onDetach();}
 virtual void enterWorld(){inWorld=true;++enters;if(onEnter)onEnter();}
 virtual void leaveWorld(){inWorld=false;++leaves;if(onLeave)onLeave();}
 void tick(float){++ticks;if(onTick)onTick();}
 void updateAnimations(const Matrix&m,float){++updates;observedPose=m.value;}
 void draw(Moo::DrawContext&,const Matrix&m){++draws;observedPose=m.value;}
 void worldTransform(const Matrix&){}
 void localBoundingBox(BoundingBox&){if(onBounds)onBounds();}
 bool isAttached()const{return attached;}bool isInWorld()const{return inWorld;}
 MatrixLiaison* matrixLiaison()const{return liaison;}
};
using PyAttachmentPtr=Ptr<PyAttachment>;
using PyModelPtr=PyAttachmentPtr;
@NODE@;
@PART@;
struct CompoundAttachment;
using CompoundAttachmentPtr=std::shared_ptr<CompoundAttachment>;
@STATE@;
using CompoundStatePtr=std::shared_ptr<CompoundState>;
@ITEM@;
@FIND@
@REMOVE@
@VISIT@
@BOUNDS@;
struct Owner:PyAttachment {
 CompoundStatePtr state_;std::map<size_t,PyObjectPtr> handles_;
 explicit Owner(CompoundStatePtr state):state_(state){}
 bool visible_=true;
 @TICK@
 @UPDATE@
 @DRAW@
 @RESET@
};
struct Node {
 CompoundStatePtr state_;size_t node_;
 @ATTACH@
 @DETACH@
 @LOCAL_GET@
 @LOCAL_SET@
 @DANGLING@
 @POSITION@
};
int main(){
 auto state=std::make_shared<CompoundState>();state->nodes.resize(2);
 state->nodes[0].parent=-1;state->nodes[1].parent=0;
 state->nodes[0].world.value=10;state->nodes[1].world.value=20;
 Owner owner(state);Node root{state,0},bone{state,1};
 auto retained=PyAttachmentPtr(new PyAttachment);
 assert(bone.attach(PyObjectPtr(retained.get()),Matrix{3},false)==&none);
 auto item=findCompoundAttachment(state,retained.get());
 assert(item&&retained->isAttached()&&retained->isInWorld()&&retained->enters==1);
 assert(retained->liaison->getMatrix().value==23);
 Matrix ignored{99};assert(retained->liaison->setMatrix(ignored));
 assert(retained->liaison->getMatrix().value==23);
 owner.tick(.1f);
 state->nodes[1].world.value=40;
 state->nodes[1].local.value=30;state->nodes[0].local.value=10;
 owner.updateAnimations(Matrix{},0);
 Moo::DrawContext context;owner.draw(context,Matrix{});
 assert(retained->ticks==1&&retained->updates==1&&retained->draws==1&&retained->observedPose==43);
 auto other=std::make_shared<CompoundState>();Node foreign{other,0};
 foreign.detach(PyObjectPtr(retained.get()));assert(item->active);
 error=0;assert(!root.attach(PyObjectPtr(retained.get()),Matrix{},false)&&error==PyExc_TypeError);
 assert(state->attachments.size()==1);error=0;
 retained->onLeave=[&]{assert(!findCompoundAttachment(state,retained.get()));root.detach(PyObjectPtr(retained.get()));};
 root.detach(PyObjectPtr(retained.get()));
 assert(!retained->isAttached()&&!retained->isInWorld()&&retained->leaves==1&&retained->detaches==1);
 root.detach(PyObjectPtr(retained.get()));assert(retained->detaches==1);
 retained->onLeave={};
 auto provider=std::make_shared<Provider>();provider->value=11;
 root.local(provider);assert(root.local()==provider);assert(state->evaluate());
 assert(root.position().x==11);root.local(nullptr);assert(!root.local());
 assert(state->evaluate()&&root.position().x==11);
 // A tick callback may remove a later item and add a new one. Snapshot
 // traversal must neither visit the retired item nor visit the new item early.
 auto a=PyAttachmentPtr(new PyAttachment),b=PyAttachmentPtr(new PyAttachment),c=PyAttachmentPtr(new PyAttachment);
 assert(root.attach(PyObjectPtr(a.get()),Matrix{},false));
 assert(root.attach(PyObjectPtr(b.get()),Matrix{},true));
 context.pass=Moo::RENDERING_PASS_SHADOWS;owner.draw(context,Matrix{});
 assert(a->draws==1&&b->draws==1); // Sunlight keeps the native shared mask.
 Scene scene;IntersectionSet objects;
 DrawHelpers::callback=[&](const SceneDrawContext&value){
  assert(stockLocalLightingShadowContext(value.draw));owner.draw(value.draw,Matrix{});
 };
 drawShadowObjects(scene,context,objects);assert(a->draws==1&&b->draws==2);
 assert(!stockLocalLightingShadowContext(context));DrawHelpers::callback={};
 {
  LocalShadowDrawScope local(context);assert(stockLocalLightingShadowContext(context));
  Moo::DrawContext nested;
  assert(!stockLocalLightingShadowContext(nested));
  try {LocalShadowDrawScope inner(nested);assert(!stockLocalLightingShadowContext(context));
       assert(stockLocalLightingShadowContext(nested));throw 1;}catch(int){}
  assert(stockLocalLightingShadowContext(context)&&!stockLocalLightingShadowContext(nested));
 }
 assert(!stockLocalLightingShadowContext(context));
 context.pass=Moo::RENDERING_PASS_COLOR;owner.draw(context,Matrix{});
 assert(a->draws==2&&b->draws==3);
 a->onTick=[&]{root.detach(PyObjectPtr(b.get()));root.attach(PyObjectPtr(c.get()),Matrix{},false);};
 owner.tick(.1f);
 assert(a->ticks==1&&b->ticks==0&&c->ticks==0);a->onTick={};
 c->onLeave=[&]{assert(root.isDangling());owner.reset();};
 owner.reset();assert(root.isDangling()&&state->attachments.empty());
 assert(!a->isAttached()&&!c->isAttached()&&a->detaches==1&&c->detaches==1);
 assert(root.position().x==0&&!root.local());owner.reset();
 assert(root.attach(PyObjectPtr(retained.get()),Matrix{},false)==&none&&!retained->isAttached());
 // Reset can occur before a virtual attachment has installed its liaison.
 state=std::make_shared<CompoundState>();state->nodes.resize(1);state->nodes[0].parent=-1;
 Owner owner2(state);Node node{state,0};auto late=PyAttachmentPtr(new PyAttachment);
 late->onAttach=[&]{owner2.reset();};
 assert(node.attach(PyObjectPtr(late.get()),Matrix{},false)==&none);
 assert(!late->isAttached()&&!late->isInWorld()&&late->detaches==1);
 state=std::make_shared<CompoundState>();state->nodes.resize(1);state->nodes[0].parent=-1;
 Owner owner3(state);Node node3{state,0};auto entering=PyAttachmentPtr(new PyAttachment);
 entering->onEnter=[&]{owner3.reset();};
 assert(node3.attach(PyObjectPtr(entering.get()),Matrix{},false)==&none);
 assert(!entering->isAttached()&&!entering->isInWorld()&&entering->enters==1&&entering->leaves==1);
 // Detach followed by reattach during leave/detach must not retire the new
 // owner when the old callback resumes.
 state=std::make_shared<CompoundState>();state->nodes.resize(2);
 Owner owner4(state);Node n0{state,0},n1{state,1};auto moved=PyAttachmentPtr(new PyAttachment);
 n0.attach(PyObjectPtr(moved.get()),Matrix{1},false);
 moved->onDetach=[&]{moved->onDetach={};n1.attach(PyObjectPtr(moved.get()),Matrix{2},false);};
 n0.detach(PyObjectPtr(moved.get()));
 assert(moved->isAttached()&&state->attachments.size()==1&&state->attachments[0]->node==1);
 owner4.reset();assert(!moved->isAttached());
 // Bounds providers keep their last committed result when obtaining the
 // real part's bounds synchronously retires and releases that part.
 state=std::make_shared<CompoundState>();state->nodes.resize(1);
 state->nodes[0].world.value=10;state->parts.resize(1);
 state->parts[0].mount=0;state->parts[0].model=PyModelPtr(new PyAttachment);
 Owner owner5(state);PyCompoundPartBounds bounds(state,0);Matrix result;
 bounds.matrix(result);assert(result.value==12);
 state->parts[0].model->onBounds=[&]{owner5.reset();};
 bounds.matrix(result);assert(!state->active&&state->parts.empty()&&result.value==12);
}
'''
    replacements = {
        'NODE': 'struct CompoundNode', 'PART': 'struct CompoundPart',
        'STATE': 'struct CompoundState', 'ITEM': 'struct CompoundAttachment :',
        'FIND': 'CompoundAttachmentPtr findCompoundAttachment(',
        'REMOVE': 'void removeCompoundAttachment(',
        'VISIT': 'template<class Callback>', 'RESET': 'void reset()',
        'TICK': 'virtual void tick( float elapsed )',
        'UPDATE': 'virtual void updateAnimations( const Matrix & /* world */',
        'DRAW': 'virtual void draw( Moo::DrawContext & context, const Matrix & /* world */',
        'BOUNDS': 'class PyCompoundPartBounds :',
        'ATTACH': 'PyObject * attach(', 'DETACH': 'void detach( PyObjectPtr',
        'LOCAL_GET': 'MatrixProviderPtr local() const',
        'LOCAL_SET': 'void local( MatrixProviderPtr',
        'DANGLING': 'bool isDangling() const', 'POSITION': 'Vector3 position() const',
    }
    for name, signature in replacements.items():
        host = host.replace('@' + name + '@', block(source, signature))
    host = host.replace('@LOCAL_SCOPE@', block(lighting, 'class LocalShadowDrawScope'))
    host = host.replace('@LOCAL_CONTEXT@', block(lighting, 'bool stockLocalLightingShadowContext('))
    host = host.replace('@LOCAL_DRAW@', block(lighting, 'void drawShadowObjects('))
    return host


class CompoundNodeTests(unittest.TestCase):
    def test_production_attachment_pose_lifecycle_and_reentry(self):
        with tempfile.TemporaryDirectory(prefix='compound-node-owner-') as temporary:
            directory = Path(temporary)
            (directory / 'probe.cpp').write_text(production_harness())
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined']
            subprocess.run(command + [str(directory / 'probe.cpp'), '-o', str(directory / 'probe')], check=True)
            subprocess.run([str(directory / 'probe')], check=True)

    def test_exact_pyc_sticker_setup_attach_detach_clear_order(self):
        client = os.environ.get('WOT_0922_CLIENT')
        pyenv = shutil.which('pyenv')
        if not client or not pyenv:
            self.skipTest('Set WOT_0922_CLIENT and install pinned pyenv CPython 2.7.18')
        environment = dict(os.environ, PYENV_VERSION='2.7.18', PYTHONDONTWRITEBYTECODE='1')
        python = subprocess.check_output([pyenv, 'which', 'python2.7'], env=environment, text=True).strip()
        script = r'''
import marshal,os,sys,types,zipfile
z=zipfile.ZipFile(os.path.join(sys.argv[1],'res/packages/scripts.pkg'))
code=marshal.loads(z.read('scripts/client/VehicleStickers.pyc')[8:])
functions={}
def visit(code,path=''):
    for child in code.co_consts:
        if isinstance(child,types.CodeType):
            name=(path+'.' if path else '')+child.co_name
            functions[name]=child
            visit(child,name)
visit(code)
class Object(object):pass
replay=Object();replay.g_replayCtrl=Object()
globals={'BattleReplay':replay,'__builtins__':__builtins__}
attach=types.FunctionType(functions['ModelStickers.attachStickers'],globals)
detach=types.FunctionType(functions['ModelStickers.detachStickers'],globals)
events=[]
class Sticker(object):
    attached=False
    def setupSuperModel(self,geometry,matrix):
        assert not self.attached
        self.geometry=geometry
        events.append(('setup',geometry,matrix))
    def clear(self):
        assert not self.attached
        events.append(('clear',))
class Node(object):
    def attach(self,sticker):
        assert not sticker.attached and sticker.geometry is not None
        sticker.attached=True;events.append(('attach',))
    def detach(self,sticker):
        assert sticker.attached
        sticker.attached=False;events.append(('detach',))
owner=Object();owner.detachStickers=types.MethodType(detach,owner)
owner._ModelStickers__model=None
owner._ModelStickers__toPartRootMatrix='identity'
owner._ModelStickers__parentNode=None
owner._ModelStickers__stickerModel=Sticker()
owner._ModelStickers__slotsByType={}
assert attach(owner,'geometry1',Node(),False,None) is None
assert events==[('setup','geometry1','identity'),('attach',)]
assert attach(owner,'geometry2',Node(),True,'toRoot') is None
assert events==[('setup','geometry1','identity'),('attach',),('detach',),('clear',),('setup','geometry2','toRoot'),('attach',)]
assert detach(owner) is None
assert events[-2:]==[('detach',),('clear',)]
assert owner._ModelStickers__model is None and owner._ModelStickers__parentNode is None
assert not owner._ModelStickers__isDamaged
count=len(events);assert detach(owner) is None and len(events)==count
print('Exact #1513 VehicleStickers attach/rebind/detach/clear bytecode passed')
'''
        subprocess.run([python, '-c', script, client], env=environment, check=True)


if __name__ == '__main__':
    unittest.main()
