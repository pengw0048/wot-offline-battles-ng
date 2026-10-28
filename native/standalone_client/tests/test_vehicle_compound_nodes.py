"""Exercise the production compound attachment owner across Python reentry."""
from pathlib import Path
import os
import importlib.util
import io
import shutil
import struct
import subprocess
from native_test_compile_flags import native_test_compile_flags
import sys
import tempfile
import tarfile
import unittest
import zipfile

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
 float value=0,y=0,z=0;Vector3 rows[3];static const Matrix identity;
 explicit Matrix(float v=0):value(v){}
 void multiply(const Matrix&a,const Matrix&b){value=a.value+b.value;y=a.y+b.y;z=a.z+b.z;}
 Vector3 applyToOrigin()const{return {value,y,z};}
 Vector3 applyPoint(const Vector3&v)const{return {value+v.x,y+v.y,z+v.z};}
 void translation(const Vector3&v){value=v.x;y=v.y;z=v.z;}
 void setTranslate(const Vector3&v){translation(v);rows[0]={1,0,0};rows[1]={0,1,0};rows[2]={0,0,1};}
 Vector3& operator[](int n){return rows[n];}
};
const Matrix Matrix::identity;
struct BoundingBox {
 Vector3 minBounds()const{return {2,0,0};}
 Vector3 maxBounds()const{return {5,0,0};}
};
struct MatrixProvider {MatrixProvider(bool,int*){};virtual ~MatrixProvider(){}};
#define Py_Header(TYPE,BASE) inline static int s_type_=0;
struct PyAttachment;using PyModel=PyAttachment;
#define ERROR_MSG(...) ((void)0)
namespace Moo {
enum {RENDERING_PASS_COLOR,RENDERING_PASS_REFLECTION,RENDERING_PASS_SHADOWS};
struct DrawContext {
 struct OverrideBlock{};
 OverrideBlock* activeOverride=nullptr;const PyModel* approved=nullptr;
 OverrideBlock* stockExchangeOverrideBlock(OverrideBlock*next){auto previous=activeOverride;activeOverride=next;return previous;}
 const PyModel* stockApprovedReflectionModel()const{return approved;}
 const PyModel* stockExchangeApprovedReflectionModel(const PyModel*next){auto previous=approved;approved=next;return previous;}
 enum {OPAQUE_CHANNEL_MASK=1};
 int pass=0;int renderingPassType()const{return pass;}
 void begin(int){};void end(int){};void flush(int){};
};
struct Context {bool reflected=false;bool reflectionScene()const{return reflected;}unsigned frameTimestamp()const{return 1;}};
Context& rc(){static Context value;return value;}
}
thread_local const Moo::DrawContext * activeLocalShadowDraw = NULL;
@LOCAL_SCOPE@;
@LOCAL_CONTEXT@
struct CompoundRenderingResources {
 bool open=true;
 std::shared_ptr<Moo::DrawContext::OverrideBlock> stipple=std::make_shared<Moo::DrawContext::OverrideBlock>();
};
std::shared_ptr<CompoundRenderingResources> compoundRenderingResources;
@STIPPLE_SCOPE@;
@REFLECTION_SCOPE@;
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
struct Provider {std::function<void()> callback;float value=0;Matrix stored;bool full=false;
 virtual ~Provider(){};
 void matrix(Matrix&m){if(callback)callback();if(full)m=stored;else m.value=value;}
};
struct PyMatrix:Provider {void set(const Matrix&authored){stored=authored;full=true;}};
struct MatrixProviderPtr:std::shared_ptr<Provider> {
 using std::shared_ptr<Provider>::shared_ptr;
 using std::shared_ptr<Provider>::operator=;
 MatrixProviderPtr(){};
 MatrixProviderPtr(const std::shared_ptr<Provider>&value):std::shared_ptr<Provider>(value){}
 template<class T> MatrixProviderPtr(T*value,bool):std::shared_ptr<Provider>(value){}
 MatrixProviderPtr& operator=(std::nullptr_t){reset();return *this;}
};
struct PyAttachment:PyObject {
 MatrixLiaison* liaison=nullptr;bool attached=false,inWorld=false;
 bool shown=true,priority=false;
 bool visible()const{return shown;}void visible(bool value){shown=value;}
 void highPriorityReflection(bool value){priority=value;}
 int enters=0,leaves=0,detaches=0,ticks=0,draws=0,updates=0;float observedPose=0;
 const Moo::DrawContext::OverrideBlock* observedOverride=nullptr;
 std::function<void()> onAttach,onEnter,onLeave,onDetach,onTick,onBounds,onDraw;
 static bool Check(PyObject*p){return dynamic_cast<PyAttachment*>(p)!=nullptr;}
 virtual bool attach(MatrixLiaison*p){if(onAttach)onAttach();liaison=p;attached=true;return true;}
 virtual void detach(){attached=false;liaison=nullptr;++detaches;if(onDetach)onDetach();}
 virtual void enterWorld(){inWorld=true;++enters;if(onEnter)onEnter();}
 virtual void leaveWorld(){inWorld=false;++leaves;if(onLeave)onLeave();}
 void tick(float){++ticks;if(onTick)onTick();}
 void updateAnimations(const Matrix&m,float){++updates;observedPose=m.value;}
 void draw(Moo::DrawContext&,const Matrix&);
 const PyModel* getPyModel()const{return this;}
 void worldTransform(const Matrix&){}
 void localBoundingBox(BoundingBox&){if(onBounds)onBounds();}
 bool isAttached()const{return attached;}bool isInWorld()const{return inWorld;}
 MatrixLiaison* matrixLiaison()const{return liaison;}
};
using PyAttachmentPtr=Ptr<PyAttachment>;
using PyModelPtr=PyAttachmentPtr;
struct WaterScene;
struct WaterSceneRenderer {
 inline static unsigned count=0;inline static WaterScene* current=nullptr;
 static WaterScene* currentScene(){return current;}
 static void incReflectionCount(){++count;}
};
struct WaterScene {
 unsigned maximum=100,queries=0;float excludedRoot=99999;
 bool shouldReflect(const Vector3&position,PyModel*model){++queries;
  return position.x!=excludedRoot&&(model->priority||WaterSceneRenderer::count<maximum);}
};
void PyAttachment::draw(Moo::DrawContext&context,const Matrix&world){
 if(!shown)return;
 if(Moo::rc().reflectionScene()&&context.stockApprovedReflectionModel()!=this){
  if(!WaterSceneRenderer::currentScene()||!WaterSceneRenderer::currentScene()->shouldReflect(world.applyToOrigin(),this))return;
  WaterSceneRenderer::incReflectionCount();
 }
 ++draws;observedPose=world.value;observedOverride=context.activeOverride;
 auto callback=onDraw;if(callback)callback();
}
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
 explicit Owner(CompoundStatePtr state):state_(state){
  if(state_->parts.empty()){CompoundPart part;part.model=PyModelPtr(new PyAttachment);part.mount=0;state_->parts.push_back(part);}
 }
 bool skipColorPass_=false,highPriorityReflection_=false,stipple_=false;
 @VISIBLE_GET@
 @VISIBLE_SET@
 @PRIORITY_GET@
 @PRIORITY_SET@
 @IN_WORLD@
 @OWNER_MATRIX_SET@
 @OWNER_POSITION_GET@
 @OWNER_POSITION_SET@
 @ENTER@
 @LEAVE@
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
 assert(!owner.inWorld());owner.enterWorld();assert(owner.inWorld());
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
 owner.skipColorPass_=true;owner.draw(context,Matrix{});assert(a->draws==2&&b->draws==3);
 context.pass=Moo::RENDERING_PASS_SHADOWS;owner.draw(context,Matrix{});
 assert(a->draws==3&&b->draws==4);owner.skipColorPass_=false;
 context.pass=Moo::RENDERING_PASS_COLOR;
 owner.visible(false);assert(!owner.visible());owner.draw(context,Matrix{});
 assert(a->draws==3&&b->draws==4);owner.visible(true);assert(owner.visible());
 owner.isHighPriorityReflection(true);assert(owner.isHighPriorityReflection());
 assert(state->parts[0].model->priority);
 a->onTick=[&]{root.detach(PyObjectPtr(b.get()));root.attach(PyObjectPtr(c.get()),Matrix{},false);};
 owner.tick(.1f);
 assert(a->ticks==1&&b->ticks==0&&c->ticks==0);a->onTick={};
 c->onLeave=[&]{assert(root.isDangling());owner.reset();};
 owner.reset();assert(root.isDangling()&&state->attachments.empty()&&!owner.inWorld()&&!owner.visible());
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
 state=std::make_shared<CompoundState>();state->nodes.resize(1);state->nodes[0].parent=-1;
 state->nodes[0].local.rows[0]={7,8,9};state->nodes[0].world.value=3;
 Owner owner6(state);auto old=std::make_shared<Provider>();old->value=5;state->matrix=old;
 owner6.position({20,30,40});assert(state->matrix.get()!=old.get());
 assert(owner6.position().x==3);assert(state->evaluate());
 assert(owner6.position().x==20&&owner6.position().y==30&&owner6.position().z==40);
 assert(state->nodes[0].local.rows[0].x==1&&state->nodes[0].local.rows[0].y==0);
 owner6.reset();owner6.position({50,60,70});assert(owner6.position().x==0);
 // Original reflection approval is atomic for all parts, even when there
 // is only one budget slot left. An unrelated nested PyModel still queries.
 auto makeState=[](size_t count,float x){
  auto value=std::make_shared<CompoundState>();value->nodes.resize(1);value->nodes[0].parent=-1;
  value->nodes[0].world.value=x;value->parts.resize(count);
  for(auto&part:value->parts){part.model=PyModelPtr(new PyAttachment);part.mount=0;}
  return value;
 };
 auto reflectedState=makeState(3,10);Owner reflectedOwner(reflectedState);
 WaterScene water;water.maximum=1;WaterSceneRenderer::current=&water;WaterSceneRenderer::count=0;
 Moo::rc().reflected=true;context.pass=Moo::RENDERING_PASS_REFLECTION;
 auto ordinary=PyModelPtr(new PyAttachment);
 reflectedState->parts[0].model->onDraw=[&]{
  assert(context.stockApprovedReflectionModel()==reflectedState->parts[0].model.get());
  ordinary->draw(context,Matrix{});
  assert(context.stockApprovedReflectionModel()==reflectedState->parts[0].model.get());
 };
 reflectedOwner.draw(context,Matrix{});
 assert(WaterSceneRenderer::count==3&&water.queries==2&&ordinary->draws==0);
 for(auto&part:reflectedState->parts)assert(part.model->draws==1);
 assert(!context.stockApprovedReflectionModel());
 // A sibling compound invoked from a part gets its own approval and budget;
 // its scoped per-model identity restores the outer part when it returns.
 auto nestedState=makeState(2,20);Owner nestedOwner(nestedState);
 water.maximum=4;water.queries=0;WaterSceneRenderer::count=0;
 reflectedState->parts[0].model->onDraw=[&]{nestedOwner.draw(context,Matrix{});
  assert(context.stockApprovedReflectionModel()==reflectedState->parts[0].model.get());};
 reflectedOwner.draw(context,Matrix{});
 assert(WaterSceneRenderer::count==5&&water.queries==2);
 for(auto&part:reflectedState->parts)assert(part.model->draws==2);
 for(auto&part:nestedState->parts)assert(part.model->draws==1);
 reflectedState->parts[0].model->onDraw={};
 // Independent attachments query exactly once and use the parent priority,
 // while their native PyModel draw skips only that same approved identity.
 auto reflectedChild=PyAttachmentPtr(new PyAttachment);Node reflectedNode{reflectedState,0};
 reflectedNode.attach(PyObjectPtr(reflectedChild.get()),Matrix{5},false);
 water.maximum=8;water.queries=0;WaterSceneRenderer::count=0;
 reflectedOwner.draw(context,Matrix{});
 assert(WaterSceneRenderer::count==4&&water.queries==2&&reflectedChild->draws==1);
 reflectedOwner.isHighPriorityReflection(true);water.maximum=0;water.queries=0;WaterSceneRenderer::count=0;
 reflectedOwner.draw(context,Matrix{});
 assert(WaterSceneRenderer::count==4&&water.queries==2&&reflectedChild->draws==2&&!reflectedChild->priority);
 reflectedOwner.isHighPriorityReflection(false);water.maximum=8;water.excludedRoot=10;
 water.queries=0;WaterSceneRenderer::count=0;
 reflectedOwner.draw(context,Matrix{});
 assert(WaterSceneRenderer::count==1&&water.queries==2&&reflectedChild->draws==3);
 for(auto&part:reflectedState->parts)assert(part.model->draws==4);
 // The real stipple override wraps parts only. Child attachments recover
 // the caller's override, including nested scopes and exception unwinding.
 Moo::rc().reflected=false;context.pass=Moo::RENDERING_PASS_COLOR;
 compoundRenderingResources=std::make_shared<CompoundRenderingResources>();
 Moo::DrawContext::OverrideBlock callerOverride;context.activeOverride=&callerOverride;
 reflectedOwner.stipple_=true;reflectedOwner.draw(context,Matrix{});
 for(auto&part:reflectedState->parts)assert(part.model->observedOverride==compoundRenderingResources->stipple.get());
 assert(reflectedChild->observedOverride==&callerOverride&&context.activeOverride==&callerOverride);
 {
  CompoundStippleScope outer(context,compoundRenderingResources->stipple);
  auto nested=std::make_shared<Moo::DrawContext::OverrideBlock>();
  try{CompoundStippleScope inner(context,nested);assert(context.activeOverride==nested.get());throw 1;}catch(int){}
  assert(context.activeOverride==compoundRenderingResources->stipple.get());
 }
 assert(context.activeOverride==&callerOverride);
 Moo::rc().reflected=true;water.excludedRoot=99999;water.queries=0;WaterSceneRenderer::count=0;
 reflectedState->parts[0].model->onDraw=[] {throw 2;};
 try{reflectedOwner.draw(context,Matrix{});assert(false);}catch(int){}
 assert(context.activeOverride==&callerOverride&&!context.stockApprovedReflectionModel());
 reflectedState->parts[0].model->onDraw={};
 std::weak_ptr<Moo::DrawContext::OverrideBlock> shader=compoundRenderingResources->stipple;
 {CompoundStippleScope pinned(context,compoundRenderingResources->stipple);
  compoundRenderingResources->stipple.reset();assert(!shader.expired());}
 assert(shader.expired()&&context.activeOverride==&callerOverride);
 reflectedOwner.reset();nestedOwner.reset();compoundRenderingResources.reset();
 Moo::rc().reflected=false;WaterSceneRenderer::current=nullptr;
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
        'VISIBLE_GET': 'bool visible() const', 'VISIBLE_SET': 'void visible( bool value )',
        'PRIORITY_GET': 'bool isHighPriorityReflection() const',
        'PRIORITY_SET': 'void isHighPriorityReflection( bool value )',
        'IN_WORLD': 'bool inWorld() const',
        'OWNER_MATRIX_SET': 'void matrix( MatrixProviderPtr value )',
        'ENTER': 'virtual void enterWorld()', 'LEAVE': 'virtual void leaveWorld()',
        'ATTACH': 'PyObject * attach(', 'DETACH': 'void detach( PyObjectPtr',
        'LOCAL_GET': 'MatrixProviderPtr local() const',
        'LOCAL_SET': 'void local( MatrixProviderPtr',
        'DANGLING': 'bool isDangling() const', 'POSITION': 'Vector3 position() const',
    }
    for name, signature in replacements.items():
        host = host.replace('@' + name + '@', block(source, signature))
    compound = source[source.index('class PyCompoundModel :'):]
    host = host.replace('@OWNER_POSITION_GET@', block(compound, 'Vector3 position() const'))
    host = host.replace('@OWNER_POSITION_SET@', block(compound, 'void position( const Vector3 & value )'))
    host = host.replace('@LOCAL_SCOPE@', block(lighting, 'class LocalShadowDrawScope'))
    host = host.replace('@LOCAL_CONTEXT@', block(lighting, 'bool stockLocalLightingShadowContext('))
    host = host.replace('@LOCAL_DRAW@', block(lighting, 'void drawShadowObjects('))
    host = host.replace('@STIPPLE_SCOPE@', block(source, 'class CompoundStippleScope'))
    host = host.replace('@REFLECTION_SCOPE@', block(source, 'class CompoundReflectionScope'))
    return host


def production_resource_harness():
    source = (ROOT / 'bigworld/vehicle_appearance.cpp').read_text()
    host = (ROOT / 'tests/stock_vehicle_compound_resources_host.hpp').read_text()
    signatures = {
            'CONSTANT': 'class CompoundStippleConstant',
            'BINDING': 'void clearCompoundStippleBinding(',
            'CLEAR_MATERIAL': 'void clearCompoundStippleMaterial(',
            'RESOURCES': 'struct CompoundRenderingResources',
            'INITIALISE': 'bool initialiseStockVehicleAppearanceResources()',
            'FINI': 'void finiStockVehicleAppearanceResources()',
    }
    for name, signature in signatures.items():
        host = host.replace('@' + name + '@', block(source, signature))
    return host


class CompoundNodeTests(unittest.TestCase):
    def test_production_stipple_resource_init_failure_and_teardown(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        host = production_resource_harness()
        with tempfile.TemporaryDirectory(prefix='compound-stipple-resource-') as temporary:
            root = Path(temporary)
            (root / 'probe.cpp').write_text(host)
            command = ['c++', *native_test_compile_flags('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            subprocess.run(command + [str(root / 'probe.cpp'), '-o', str(root / 'probe')], check=True)
            subprocess.run([str(root / 'probe')], check=True)

    def test_public_override_patch_is_atomic_and_idempotent(self):
        archive = os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE')
        if not archive or not Path(archive).is_file():
            self.skipTest('Set WOT_BIGWORLD_SOURCE_ARCHIVE to the pristine pinned public source archive')
        spec = importlib.util.spec_from_file_location('compound_render_patch', ROOT / 'bigworld/patch_vehicle_compound_rendering.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        names = ['lib/moo/draw_context.hpp', 'lib/duplo/pymodel.cpp', 'client/script_app.cpp']
        with tarfile.open(archive) as source:
            original = {}
            for name in names:
                matches = [member for member in source.getmembers() if member.name.endswith('/' + name)]
                self.assertEqual(len(matches), 1)
                original[name] = source.extractfile(matches[0]).read().decode('utf-8')
        with tempfile.TemporaryDirectory(prefix='compound-render-patch-') as temporary:
            root = Path(temporary)
            for name in names:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(original[name])
            patch.patch_engine(root)
            first = [(root / name).read_bytes() for name in names]
            patch.patch_engine(root)
            self.assertEqual(first, [(root / name).read_bytes() for name in names])
            self.assertEqual(first[0].count(b'stockExchangeOverrideBlock('), 1)
            self.assertEqual(first[0].count(b'stockExchangeApprovedReflectionModel('), 1)
            self.assertEqual(first[1].count(b'drawContext.stockApprovedReflectionModel() != this'), 1)
            self.assertEqual(first[2].count(b'finiStockVehicleAppearanceResources();'), 1)
            self.assertLess(first[2].index(b'finiStockVehicleAppearanceResources();'),
                            first[2].index(b'BigWorldClientScript::fini();'))
            for name in names:
                (root / name).write_text(original[name])
            body = block(original[names[1]], 'void PyModel::draw( Moo::DrawContext& drawContext,')
            changed = original[names[1]].replace(body, body.replace('if (Moo::rc().reflectionScene())', 'if (false)', 1))
            (root / names[1]).write_text(changed)
            before = [(root / name).read_bytes() for name in names]
            with self.assertRaises(ValueError):
                patch.patch_engine(root)
            self.assertEqual(before, [(root / name).read_bytes() for name in names])
            for name in names:
                (root / name).write_text(original[name])
            (root / names[2]).write_text(original[names[2]].replace('BigWorldClientScript::fini();', 'unexpectedTeardown();'))
            before = [(root / name).read_bytes() for name in names]
            with self.assertRaises(ValueError):
                patch.patch_engine(root)
            self.assertEqual(before, [(root / name).read_bytes() for name in names])

    def test_exact_stipple_resource_producer_and_effect_semantics(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Set WOT_0922_CLIENT to the exact #1513 client')
        sys.path.insert(0, str(ROOT.parents[1] / 'tools'))
        from packed_xml import read_packed_xml, TYPE_ELEMENT
        root = read_packed_xml((Path(client) / 'res/resources.xml').read_bytes())
        for name in (b'system', b'stippleMap'):
            value = next(value for key, value in root.children if key == name)
            if value.value_type == TYPE_ELEMENT:
                root = value.value
        texture = value.value.decode('ascii')
        self.assertEqual(texture, 'system/maps/stipple.dds')
        packages = Path(client) / 'res/packages'
        with zipfile.ZipFile(packages / 'misc.pkg') as archive:
            data = archive.read(texture)
            self.assertEqual(data[:4], b'DDS ')
            self.assertEqual(struct.unpack_from('<II', data, 12), (4, 4))
        with zipfile.ZipFile(packages / 'shaders.pkg') as archive:
            for variant in ('stipple', 'stipple_skinned'):
                for macros in ('10', '11'):
                    name = 'shaders/std_effects/stipple/%s.%s.dx9.fxo' % (variant, macros)
                    with zipfile.ZipFile(io.BytesIO(archive.read(name))) as effect:
                        self.assertIn(b'StippleMap', effect.read('effect'))

    def test_production_attachment_pose_lifecycle_and_reentry(self):
        if not shutil.which('c++'):
            raise unittest.SkipTest('C++ compiler unavailable: c++')
        with tempfile.TemporaryDirectory(prefix='compound-node-owner-') as temporary:
            directory = Path(temporary)
            (directory / 'probe.cpp').write_text(production_harness())
            command = ['c++', *native_test_compile_flags('c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
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
