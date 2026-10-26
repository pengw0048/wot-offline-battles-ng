"""Exercise the production filter publication and shared collision boundary."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PUBLIC_SOURCES = ['client/py_filter.hpp', 'client/py_filter.cpp', 'client/py_entity.cpp',
                  'lib/connection/avatar_filter_helper.hpp', 'lib/connection/avatar_filter_helper.cpp',
                  'client/avatar_filter.hpp', 'client/py_avatar_filter.hpp']


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def publication_harness():
    spec = importlib.util.spec_from_file_location('entity_filter_patch', ROOT / 'bigworld/patch_stock_entity_filter.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    names = PUBLIC_SOURCES
    with tempfile.TemporaryDirectory(prefix='filter-publication-source-') as temporary:
        root = Path(temporary)
        write_public_fixture(root)
        module.patch_engine(root)
        body = block((root / names[2]).read_text(), 'void PyEntity::setFilterOnEntity()')
        create = block((root / 'client/py_filter.cpp').read_text(), 'Filter * PyFilter::getFilter()')
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <memory>
#include <utility>
#include <stdexcept>
#define MF_ASSERT assert
struct PyEntity;struct PyFilter;
void registerStockFilterLagDetection(PyFilter*,bool);
struct Filter {
 static int alive;std::weak_ptr<PyFilter> owner;
 explicit Filter(const std::shared_ptr<PyFilter>&value):owner(value){++alive;}
 ~Filter();
};
int Filter::alive=0;
struct Entity {
 bool destroyed=false,inAoI=true;unsigned installations=0;
 std::unique_ptr<Filter> filter;
 bool isDestroyed()const{return destroyed;}
 bool isInAoI()const{return inAoI;}
 void pFilter(Filter*value){++installations;filter.reset(value);}
};
using EntityPtr=std::shared_ptr<Entity>;
struct PyFilter:std::enable_shared_from_this<PyFilter> {
 bool attached=true;Filter* current=nullptr;Filter*&pAttachedFilter_=current;
 bool stockCreatingFilter_=false,empty=false;std::function<void()> before,during;
 ~PyFilter(){registerStockFilterLagDetection(this,false);}
 bool isAttached()const{return attached;}
 Filter* pAttachedFilter()const{return current;}
 Filter* getNewFilter(){if(before)before();if(empty)return nullptr;auto candidate=new Filter(shared_from_this());if(during)during();return candidate;}
 Filter* getFilter();
};
Filter::~Filter(){--alive;if(auto wrapper=owner.lock())if(wrapper->current==this)wrapper->current=nullptr;}
using PyFilterPtr=std::shared_ptr<PyFilter>;
struct PyEntity:std::enable_shared_from_this<PyEntity> {
 EntityPtr pEntity_=std::make_shared<Entity>();PyFilterPtr pPyFilter_=std::make_shared<PyFilter>();
 void setFilterOnEntity();
};
struct PyObjectPtr {std::shared_ptr<PyEntity> value;explicit PyObjectPtr(PyEntity*owner):value(owner->shared_from_this()){}};
void bindStockVehicleFilterOwner(PyFilter*,PyEntity*){}
void registerStockFilterLagDetection(PyFilter*,bool){}
int PyExc_RuntimeError=1,errors=0;void PyErr_SetString(int,const char*){++errors;}
''' + create + body + r'''
int main(){
 {
  auto owner=std::make_shared<PyEntity>();owner->setFilterOnEntity();
  assert(owner->pEntity_->installations==1);
  assert(owner->pEntity_->filter.get()==owner->pPyFilter_->current);
 }
 assert(Filter::alive==0);
 for(int change=0;change<4;++change){
  auto owner=std::make_shared<PyEntity>();auto original=owner->pPyFilter_;auto entity=owner->pEntity_;
  original->during=[&,change]{
   if(change==0){auto replacement=std::make_shared<PyFilter>();owner->pPyFilter_=replacement;owner->setFilterOnEntity();}
   if(change==1){entity->destroyed=true;owner->pEntity_.reset();}
   if(change==2){entity->inAoI=false;}
   if(change==3){original->attached=false;}
  };
  owner->setFilterOnEntity();
  assert(original->current==nullptr);
  if(change==0){assert(entity->installations==1);assert(entity->filter.get()==owner->pPyFilter_->current);}
  else assert(entity->installations==0);
 }
 assert(Filter::alive==0);
 {
  auto owner=std::make_shared<PyEntity>();auto original=owner->pPyFilter_;auto entity=owner->pEntity_;
  original->during=[&]{owner->pEntity_=std::make_shared<Entity>();};
  owner->setFilterOnEntity();assert(original->current==nullptr);
  assert(entity->installations==0 && owner->pEntity_->installations==0);
 }
 assert(Filter::alive==0);
 {
  auto owner=std::make_shared<PyEntity>();std::weak_ptr<PyEntity> identity=owner;
  auto original=owner->pPyFilter_;original->during=[&]{owner.reset();assert(!identity.expired());};
  PyEntity* raw=owner.get();raw->setFilterOnEntity();assert(identity.expired());
  original->during={};
 }
 assert(Filter::alive==0);
 {
  auto owner=std::make_shared<PyEntity>();auto first=owner->pPyFilter_;auto second=std::make_shared<PyFilter>();
  first->during=[&]{owner->pPyFilter_=second;owner->setFilterOnEntity();};
  second->during=[&]{owner->pPyFilter_=first;owner->setFilterOnEntity();};
  owner->setFilterOnEntity();assert(errors==1&&owner->pEntity_->installations==1);
  assert(owner->pEntity_->filter.get()==first->current&&second->current==nullptr);
  assert(!first->stockCreatingFilter_&&!second->stockCreatingFilter_);
  first->during={};second->during={};owner->pPyFilter_=second;owner->setFilterOnEntity();
  assert(first->current==nullptr);owner->pPyFilter_=first;owner->setFilterOnEntity();
  assert(owner->pEntity_->filter.get()==first->current&&owner->pEntity_->installations==3);
 }
 assert(Filter::alive==0);
 {
  auto owner=std::make_shared<PyEntity>();auto selected=owner->pPyFilter_;
  selected->before=[] {throw std::runtime_error("injected factory failure");};
  try{owner->setFilterOnEntity();assert(false);}catch(const std::runtime_error&){}
  assert(!selected->stockCreatingFilter_&&owner->pEntity_->installations==0);
  selected->before={};selected->empty=true;owner->setFilterOnEntity();
  assert(!selected->stockCreatingFilter_&&owner->pEntity_->installations==0);
  selected->empty=false;owner->setFilterOnEntity();assert(owner->pEntity_->installations==1);
  auto published=selected->current;selected->empty=true;owner->setFilterOnEntity();
  assert(selected->current==published&&owner->pEntity_->filter.get()==published&&owner->pEntity_->installations==1);
 }
 assert(Filter::alive==0);
}
'''


def input_control_harness():
    source = (ROOT / 'bigworld/stock_entity_filter.cpp').read_text()
    bodies = '\n'.join(block(source, signature) for signature in [
        'void StockEntityFilter::reset(', 'void StockEntityFilter::input(',
        'void StockEntityFilter::setScriptInputCallback('])
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>
#include <cstring>
using SpaceID=unsigned;using EntityID=unsigned;using int32=int;
struct Vector3 {float x=0,y=0,z=0;float length()const{return std::sqrt(x*x+y*y+z*z);}Vector3 operator-(const Vector3&o)const{return {x-o.x,y-o.y,z-o.z};}};
using Position3D=Vector3;using Direction3D=Vector3;
struct PyObject {
 static int alive;std::function<void()> destroy;std::function<bool(PyObject*)> call;
 Vector3 vector{};double time=0;int space=0,vehicle=0;std::vector<std::shared_ptr<PyObject>> arguments;
 PyObject(){++alive;}~PyObject(){--alive;if(destroy)destroy();}
};int PyObject::alive=0;
struct PyObjectPtr {
 enum Mode{STEAL_REFERENCE};std::shared_ptr<PyObject> value;
 PyObjectPtr()=default;PyObjectPtr(PyObject*object,Mode):value(object){}
 explicit PyObjectPtr(std::shared_ptr<PyObject> object):value(std::move(object)){}
 PyObject*get()const{return value.get();}explicit operator bool()const{return bool(value);}
};
PyObject*Py_None=nullptr;int errors=0;
namespace Script {PyObject*getData(const Vector3&v){auto o=new PyObject;o->vector=v;return o;}}
PyObject*Py_BuildValue(const char*format,double time,int space,int vehicle,PyObject*a,PyObject*b,PyObject*c){
 assert(std::strcmp(format,"(diiNNN)")==0);auto o=new PyObject;o->time=time;o->space=space;o->vehicle=vehicle;
 o->arguments={std::shared_ptr<PyObject>(a),std::shared_ptr<PyObject>(b),std::shared_ptr<PyObject>(c)};return o;
}
PyObject*PyObject_CallObject(PyObject*callback,PyObject*arguments){if(!callback->call||!callback->call(arguments))return nullptr;return new PyObject;}
void PyErr_Print(){++errors;}
struct StockEntityFilterSettings {bool ignoreInputs=false,ignoreNextReset=false;float maxTimeBetweenInputs=2,maxMove=1000;};
struct StockEntityFilterGlobals {static bool allowCustomResets;};bool StockEntityFilterGlobals::allowCustomResets=true;
struct History {
 struct Stored{double time=0;Vector3 position;};Stored last;Vector3 waypoint;int inputs=0,resets=0;
 const Vector3&nextWaypointPosition()const{return waypoint;}
 Stored storedInput(unsigned)const{return last;}
 void reset(double){++resets;}
 void input(double time,SpaceID,EntityID,const Vector3&v,const Vector3&,const Vector3&){++inputs;last={time,v};}
};
struct StockEntityFilter {
 struct Lifetime{bool alive=true;};std::shared_ptr<Lifetime> lifetime_=std::make_shared<Lifetime>();
 std::shared_ptr<History> helper_=std::make_shared<History>();StockEntityFilterSettings settings_;PyObjectPtr inputCallback_;
 ~StockEntityFilter(){lifetime_->alive=false;}
 void reset(double);void setScriptInputCallback(PyObjectPtr);
 void input(double,SpaceID,EntityID,const Position3D&,const Vector3&,const Direction3D&);
};
''' + bodies + r'''
int main(){
 {
  StockEntityFilter filter;filter.settings_.ignoreNextReset=true;
  filter.reset(1);assert(!filter.settings_.ignoreNextReset&&filter.helper_->resets==0);
  filter.reset(1);assert(filter.helper_->resets==1);
  auto callback=std::make_shared<PyObject>();int calls=0;
  callback->call=[&](PyObject*args){++calls;assert(args->time==7&&args->space==3&&args->vehicle==9);
   assert(args->arguments.size()==3&&args->arguments[0]->vector.x==10&&args->arguments[1]->vector.y==2&&args->arguments[2]->vector.z==3);return true;};
  filter.setScriptInputCallback(PyObjectPtr(callback));filter.settings_.ignoreInputs=true;
  filter.input(7,3,9,{10,0,0},{0,2,0},{0,0,3});assert(calls==0&&filter.helper_->inputs==0);
  filter.settings_.ignoreInputs=false;filter.input(7,3,9,{10,0,0},{0,2,0},{0,0,3});assert(calls==1&&filter.helper_->inputs==1);
  assert(filter.helper_->resets==2); // Gap reset still forwards the accepted input.
  callback->call=[](PyObject*){return false;};filter.input(7.1,3,9,{},{},{});
  assert(errors==1&&filter.helper_->inputs==2);
  StockEntityFilterGlobals::allowCustomResets=false;filter.input(20,3,9,{},{},{});
  assert(filter.helper_->resets==2&&filter.helper_->inputs==3);
  StockEntityFilterGlobals::allowCustomResets=true;filter.setScriptInputCallback({});
  filter.settings_.ignoreNextReset=true;filter.input(30,3,9,{},{},{});
  assert(!filter.settings_.ignoreNextReset&&filter.helper_->resets==2&&filter.helper_->inputs==4);
 }
 assert(PyObject::alive==0);
 {
  StockEntityFilter filter;filter.settings_.maxMove=5;filter.helper_->last.position={100,0,0};
  filter.input(1,1,1,{3,0,0},{},{});assert(filter.helper_->resets==0);
  filter.input(1.1,1,1,{6,0,0},{},{});assert(filter.helper_->resets==1);
 }
 {
  auto filter=std::make_unique<StockEntityFilter>();auto history=filter->helper_;auto lifetime=filter->lifetime_;
  auto callback=std::make_shared<PyObject>();callback->call=[&](PyObject*){filter.reset();return true;};
  filter->setScriptInputCallback(PyObjectPtr(callback));
  filter->input(100,1,1,{},{},{});assert(!lifetime->alive&&history->inputs==0&&history->resets==0);
 }
 assert(PyObject::alive==0);
 {
  auto filter=std::make_unique<StockEntityFilter>();auto lifetime=filter->lifetime_;
  auto previous=std::make_shared<PyObject>();previous->destroy=[&]{filter.reset();};
  filter->setScriptInputCallback(PyObjectPtr(previous));previous.reset();
  auto next=std::make_shared<PyObject>();
  filter->setScriptInputCallback(PyObjectPtr(next));assert(!lifetime->alive);
 }
 assert(PyObject::alive==0);
}
'''


def transfer_harness():
    source = (ROOT / 'bigworld/stock_avatar_filter_helper.cpp').read_text()
    bodies = '\n'.join(block(source, signature) for signature in [
        'void StockAvatarFilterHelper::transferInputs(',
        'void StockAvatarFilterHelper::transferInputsAsVehicle('])
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
using uint=unsigned;using SpaceID=unsigned;using EntityID=unsigned;
struct Vector3{float x=0,y=0,z=0;static Vector3 zero(){return {};}};
using Position3D=Vector3;using Direction3D=Vector3;using Quaternion=Vector3;
struct Matrix{Vector3 origin;Vector3 applyToOrigin()const{return origin;}};
int transforms=0;
struct StockAvatarFilterHelper {
 static const uint NUM_STORED_INPUTS=8;
 struct StoredInput {double time;SpaceID spaceID;EntityID vehicleID;Vector3 position,error,direction;bool onGround;};
 struct Waypoint{double time;SpaceID spaceID;EntityID vehicleID;Vector3 position;Quaternion orientation;StoredInput input;};
 StoredInput storedInputs_[8]{};unsigned current_=0,inputCount_=19;Waypoint next_{},previous_{};
 float latency_=0,idealLatency_=79;double lastOutput_=123;bool reset_=true,newInput_=false,extrapolating_=true;void*listener_=this;
 StoredInput&stored(uint i){return storedInputs_[(current_+i)&7];}
 const StoredInput&stored(uint i)const{return storedInputs_[(current_+i)&7];}
 void transferInputs(const StockAvatarFilterHelper&,const Matrix&);
 void transferInputsAsVehicle(const StockAvatarFilterHelper&,const Matrix&,SpaceID,EntityID);
};
Direction3D directionForTransferredPose(const Matrix&value){return value.origin;}
Quaternion quaternionFor(const Direction3D&value){return value;}
// These are explicit SDK-operation spies, not a replacement matrix oracle.
void transformTransferredInput(StockAvatarFilterHelper::StoredInput&value,const Matrix&offset){++transforms;value.position.x+=offset.origin.x;}
void transformTransferredWaypoint(StockAvatarFilterHelper::Waypoint&value,const Matrix&offset){++transforms;value.position.x+=offset.origin.x;transformTransferredInput(value.input,offset);}
''' + bodies + r'''
void unchangedControls(const StockAvatarFilterHelper&value,unsigned ring){
 assert(value.current_==ring&&value.inputCount_==19&&value.idealLatency_==79&&value.lastOutput_==123);
 assert(!value.newInput_&&value.extrapolating_&&value.listener_==&value);
}
int main(){
 StockAvatarFilterHelper source,destination;source.current_=5;destination.current_=3;
 source.latency_=0.75f;source.reset_=false;
 for(unsigned i=0;i<8;++i)source.stored(i)={double(i)+9,11+i,21+i,{float(i),2,3},{4,5,6},{7,8,9},true};
 source.next_={91,3,4,{10,20,30},{7,8,9},source.stored(1)};
 source.previous_={90,3,4,{11,20,30},{7,8,9},source.stored(2)};
 auto original=source;Matrix offset{{100,200,300}};
 destination.transferInputs(source,offset);unchangedControls(destination,3);assert(transforms==12);
 for(unsigned i=0;i<8;++i){const auto&v=destination.stored(i);const auto&s=source.stored(i);
  assert(v.time==s.time&&v.spaceID==s.spaceID&&v.vehicleID==s.vehicleID&&v.position.x==s.position.x+100);
  assert(v.error.x==4&&v.error.y==5&&v.onGround&&v.direction.z==9);
  assert(source.stored(i).position.x==original.stored(i).position.x);
 }
 assert(destination.next_.time==91&&destination.previous_.time==90&&destination.latency_==0.75f&&!destination.reset_);
 assert(destination.next_.input.time==10&&destination.next_.input.position.x==101);
 source.transferInputs(source,offset);unchangedControls(source,5); // Self-copy must snapshot before writes.
 for(unsigned i=0;i<8;++i)assert(source.stored(i).position.x==float(i)+100);
 destination.transferInputsAsVehicle(original,offset,171,292);unchangedControls(destination,3);
 for(unsigned i=0;i<8;++i){const auto&v=destination.stored(i);assert(v.time==original.stored(i).time);
  assert(v.spaceID==171&&v.vehicleID==292&&v.position.x==100&&v.error.x==0&&!v.onGround&&v.direction.z==300);
 }
 assert(destination.next_.time==91&&destination.previous_.time==90);
 assert(destination.next_.input.time==-2000&&destination.previous_.input.time==-2000);
 assert(destination.next_.orientation.z==300&&destination.latency_==0.75f&&!destination.reset_);
 original.transferInputsAsVehicle(original,offset,8,9);
 for(unsigned i=0;i<8;++i)assert(original.stored(i).time==double(i)+9);
}
'''


def clock_harness():
    source = (ROOT / 'bigworld/stock_vehicle_filter.cpp').read_text()
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
struct BWConnection {virtual ~BWConnection(){};virtual double lastMessageTime()const=0;};
struct ConnectionControl {BWConnection*current=nullptr;static ConnectionControl&instance(){static ConnectionControl owner;return owner;}BWConnection*pConnection()const{return current;}};
int PyExc_RuntimeError=1;int errors=0;void PyErr_SetString(int,const char*){++errors;}
''' + block(source, 'bool filterInputClock(') + r'''
struct Connection:BWConnection{double client=90,last=7,server=110;double lastMessageTime()const override{return last;}};
int main(){
 double value=123;assert(!filterInputClock(value)&&errors==1&&value==123);
 Connection connection;ConnectionControl::instance().current=&connection;
 assert(filterInputClock(value)&&value==7);
 connection.client=91;connection.server=111;assert(filterInputClock(value)&&value==7);
 connection.last=8.25;assert(filterInputClock(value)&&value==8.25);
 ConnectionControl::instance().current=nullptr;value=9;assert(!filterInputClock(value)&&value==9&&errors==2);
}
'''


def lag_harness():
    with tempfile.TemporaryDirectory(prefix='filter-lag-source-') as temporary:
        root = Path(temporary)
        write_public_fixture(root)
        patch_module().patch_engine(root)
        helper = (root / PUBLIC_SOURCES[3]).read_text()
        avatar = (root / 'client/avatar_filter.hpp').read_text()
        py_avatar = (root / 'client/py_avatar_filter.hpp').read_text()
        py_filter = (root / 'client/py_filter.hpp').read_text()
        core = 'struct Helper {bool stockExtrapolating_=false;' + block(helper, 'bool stockIsLaggingNow()') + '};'
        core += 'struct AvatarFilter {Helper helper_;' + block(avatar, 'bool stockIsLaggingNow()') + '};'
        core += 'struct PyFilter {' + block(py_filter, 'virtual bool isLaggingNow()') + '};'
        core += 'struct PyAvatarFilter:PyFilter {AvatarFilter* filter=nullptr;const AvatarFilter*pAttachedFilter()const{return filter;}'
        core += block(py_avatar, 'virtual bool isLaggingNow()') + '};'
        with tarfile.open(os.environ['WOT_BIGWORLD_SOURCE_ARCHIVE']) as archive:
            for name, kind, parent in [('py_avatar_drop_filter.hpp', 'PyAvatarDropFilter', 'PyAvatarFilter'),
                                      ('py_boids_filter.hpp', 'PyBoidsFilter', 'PyAvatarFilter'),
                                      ('py_player_avatar_filter.hpp', 'PyPlayerAvatarFilter', 'PyFilter')]:
                member = next(item for item in archive.getmembers() if item.name.endswith('/client/' + name))
                text = archive.extractfile(member).read().decode()
                assert 'class ' + kind + ' : public ' + parent in text
                core += 'struct ' + kind + ':' + parent + '{};'
    return r'''
#include <cassert>
#include <initializer_list>
''' + core + r'''
int main(){
 PyFilter bare;PyPlayerAvatarFilter player;assert(!bare.isLaggingNow()&&!player.isLaggingNow());
 PyAvatarFilter avatar;PyAvatarDropFilter drop;PyBoidsFilter boids;
 assert(avatar.isLaggingNow()&&drop.isLaggingNow()&&boids.isLaggingNow());
 AvatarFilter native;avatar.filter=&native;drop.filter=&native;boids.filter=&native;
 for(bool value:{false,true,false}){native.helper_.stockExtrapolating_=value;
  assert(avatar.isLaggingNow()==value&&drop.isLaggingNow()==value&&boids.isLaggingNow()==value);
  assert(!player.isLaggingNow());
 }
 avatar.filter=nullptr;drop.filter=nullptr;boids.filter=nullptr;
 assert(avatar.isLaggingNow()&&drop.isLaggingNow()&&boids.isLaggingNow());
}
'''


def extrapolation_proxy_harness():
    source = (ROOT / 'bigworld/stock_entity_filter.cpp').read_text()
    methods = block(source, 'Vector3 & accumulatedVector(')
    for signature in ('    PyObject * read(', '    int write('):
        method = block(source, signature).strip()
        method = method.replace('read( unsigned', 'PyExtrapolationData::read( unsigned', 1) if 'read(' in signature else method.replace('write( unsigned', 'PyExtrapolationData::write( unsigned', 1)
        methods += '\n' + method
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <functional>
#include <memory>
struct Vector3 {float x=0,y=0,z=0;};
struct PyObject {virtual ~PyObject(){};Vector3 vector;std::function<void()> convert;bool invalid=false;};
struct PyObjectPtr{std::shared_ptr<PyObject>value;explicit PyObjectPtr(std::shared_ptr<PyObject>o):value(std::move(o)){}PyObject*get()const{return value.get();}};
int PyExc_ValueError=1,errors=0;void PyErr_SetString(int,const char*){++errors;}
namespace Script {
 PyObject*getData(const Vector3&v){auto o=new PyObject;o->vector=v;return o;}
 int setData(PyObject*o,Vector3&v,const char*){if(o->convert)o->convert();if(o->invalid)return -1;v=o->vector;return 0;}
}
struct StockEntityFilterMotion {Vector3 predictedVelocity,predictedAngularVelocity,predictedAcceleration,predictedAngularAcceleration;};
struct StockEntityFilter {
 struct Lifetime{bool alive=true;};std::shared_ptr<Lifetime>life=std::make_shared<Lifetime>();
 std::shared_ptr<StockEntityFilterMotion>state=std::make_shared<StockEntityFilterMotion>();
 ~StockEntityFilter(){life->alive=false;}
 const std::shared_ptr<Lifetime>&lifetime()const{return life;}
 const std::shared_ptr<StockEntityFilterMotion>&motion()const{return state;}
};
struct PyWGEntityFilter:PyObject{StockEntityFilter*filter=nullptr;StockEntityFilter*native(){return filter;}};
struct PyExtrapolationData {
 PyObjectPtr owner_;explicit PyExtrapolationData(std::shared_ptr<PyWGEntityFilter>o):owner_(std::move(o)){}
 PyObject*read(unsigned);int write(unsigned,PyObject*);
};
''' + methods + r'''
int main(){
 auto owner=std::make_shared<PyWGEntityFilter>();std::weak_ptr<PyWGEntityFilter>weak=owner;
 auto proxy=std::make_unique<PyExtrapolationData>(owner);PyObject value;value.vector={1,2,3};
 assert(proxy->read(0)==nullptr&&errors==1);assert(proxy->write(0,&value)==-1&&errors==2);
 auto native=std::make_unique<StockEntityFilter>();owner->filter=native.get();
 for(unsigned i=0;i<4;++i){value.vector.x=float(i+1);assert(proxy->write(i,&value)==0);
  std::unique_ptr<PyObject>output(proxy->read(i));assert(output->vector.x==float(i+1));
  output->vector.x=99;assert(accumulatedVector(*native->state,i).x==float(i+1));
 }
 auto old=native->state;auto replacement=std::make_unique<StockEntityFilter>();
 value.convert=[&]{native.reset();owner->filter=replacement.get();};value.vector={20,30,40};
 assert(proxy->write(1,&value)==-1&&errors==3);
 assert(old->predictedAngularVelocity.x==2&&replacement->state->predictedAngularVelocity.x==0);
 value.convert={};value.invalid=true;assert(proxy->write(2,&value)==-1);
 assert(replacement->state->predictedAcceleration.x==0);
 owner.reset();assert(!weak.expired());proxy.reset();assert(weak.expired());
}
'''


def geometry_owner_harness():
    source = (ROOT / 'bigworld/stock_entity_filter.cpp').read_text()
    methods = '\n'.join(block(source, signature) for signature in [
        'bool StockEntityFilterGeometry::matches(', 'bool StockEntityFilterGeometry::load(',
        'void StockEntityFilterGeometry::update(', 'void StockEntityFilterGeometry::invalidate('])
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
using uint64=std::uint64_t;
struct Vector3{float x=0,y=0,z=0;void set(float a,float b,float c){x=a;y=b;z=c;}
 Vector3 operator+(const Vector3&o)const{return {x+o.x,y+o.y,z+o.z};}Vector3 operator*(float s)const{return {x*s,y*s,z*s};}};
struct Matrix{Vector3 translation;Vector3 applyPoint(const Vector3&v)const{return v+translation;}};
struct BoundingBox{Vector3 minimum,maximum;bool empty=false;bool insideOut()const{return empty;}Vector3 minBounds()const{return minimum;}Vector3 maxBounds()const{return maximum;}};
struct PyAttachment:std::enable_shared_from_this<PyAttachment>{
 BoundingBox local{{-1,-2,-3},{1,2,3}},world{{-2,-3,-4},{2,3,4}};Matrix matrix;std::function<void()> onWorld,onLocal;unsigned queries=0;
 const Matrix&worldTransform()const{return matrix;}
 void worldBoundingBox(BoundingBox&out,const Matrix&){++queries;if(onWorld)onWorld();out=world;}
};using PyObject=PyAttachment;
struct PyObjectPtr {
 std::shared_ptr<PyObject>value;PyObjectPtr()=default;PyObjectPtr(PyObject*object):value(object?object->shared_from_this():nullptr){}
 PyObject*get()const{return value.get();}explicit operator bool()const{return bool(value);}
 PyObjectPtr&operator=(PyObject*object){value=object?object->shared_from_this():nullptr;return *this;}
 bool operator!=(const PyObjectPtr&o)const{return value!=o.value;}
};
struct IEntityEmbodiment{std::shared_ptr<PyObject>object;PyObjectPtr scriptObject()const{return PyObjectPtr(object.get());}};
struct Entity{IEntityEmbodiment*embodiment=nullptr;bool destroyed=false;IEntityEmbodiment*pPrimaryEmbodiment()const{return embodiment;}bool isDestroyed()const{return destroyed;}};
bool stockVehicleModelBounds(PyObject*object,BoundingBox&out){if(!object)return false;if(object->onLocal)object->onLocal();out=object->local;return true;}
struct StockEntityFilterGeometry {
 float radius=-1,planarRadiusSquared=-1,lowerPlanarRadiusSquared=-1,verticalRadius=0,minimumY=0,maximumY=0,boundsAge=1.4f;
 Vector3 local[7],points[7],edges[7][2];PyObjectPtr model;uint64 revision=0;
 bool matches(const Entity&)const;bool load(Entity&);void update(const Matrix&,float);void invalidate();
};
''' + methods + r'''
int main(){
 StockEntityFilterGeometry geometry;Entity owner;assert(!geometry.load(owner));
 auto model=std::make_shared<PyAttachment>();IEntityEmbodiment first{model};owner.embodiment=&first;
 assert(geometry.load(owner)&&geometry.matches(owner));assert(geometry.radius>0&&geometry.lowerPlanarRadiusSquared==geometry.planarRadiusSquared);
 geometry.update(Matrix{},0.01f);assert(model->queries==1);
 assert(geometry.edges[6][0].y==-2.09f&&geometry.edges[6][1].y==2.09f);
 geometry.boundsAge=0.4f;geometry.update(Matrix{},0);assert(model->queries==2); // Promoted double threshold.
 geometry.boundsAge=0.0f;geometry.update(Matrix{},0.4f);assert(model->queries==2);
 geometry.update(Matrix{},0);assert(model->queries==3);
 auto replacement=std::make_shared<PyAttachment>();IEntityEmbodiment second{replacement};owner.embodiment=&second;
 assert(!geometry.matches(owner));geometry.invalidate();assert(geometry.radius<0&&!geometry.model);
 assert(geometry.load(owner));
 replacement->onWorld=[&]{geometry.invalidate();};geometry.update(Matrix{},0);
 assert(geometry.radius<0&&!geometry.model);replacement->onWorld={};
 assert(geometry.load(owner));replacement->world.empty=true;geometry.update(Matrix{},0);
 assert(geometry.radius<0&&!geometry.model);
 replacement->onLocal=[&]{geometry.invalidate();};assert(!geometry.load(owner));
 assert(geometry.radius<0&&!geometry.model);replacement->onLocal={};
 replacement->local.empty=true;assert(!geometry.load(owner));
 owner.destroyed=true;replacement->local.empty=false;assert(!geometry.load(owner));
}
'''


def failed_creation_rollback_harness():
    with tempfile.TemporaryDirectory(prefix='filter-rollback-source-') as temporary:
        root = Path(temporary)
        write_public_fixture(root)
        patch_module().patch_engine(root)
        source = (root / 'client/py_entity.cpp').read_text()
        setter = block(source, 'int PyEntity::pySet_filter(')
        branch = block(setter, 'if (PyErr_Occurred())')
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
struct MovementFilter{};struct PyEntity;
struct PyFilter{bool attached=true;PyEntity*owner=nullptr;MovementFilter*current=nullptr;
 bool isAttached()const{return attached;}void isAttached(bool value){attached=value;}
 MovementFilter*pAttachedFilter()const{return current;}};
using PyFilterPtr=std::shared_ptr<PyFilter>;
struct Entity{bool destroyed=false;MovementFilter*filter=nullptr;bool isDestroyed()const{return destroyed;}MovementFilter*pFilter()const{return filter;}};
using EntityPtr=std::shared_ptr<Entity>;
struct PyEntity{EntityPtr pEntity_;PyFilterPtr pPyFilter_;
 int complete(PyFilterPtr pNewFilter,PyFilterPtr previousFilter,EntityPtr previousEntity,MovementFilter*previousNative,bool previousAttached);
};
bool PyErr_Occurred(){return true;}
void bindStockVehicleFilterOwner(PyFilter*filter,PyEntity*owner){filter->owner=owner;}
''' + '\nint PyEntity::complete(PyFilterPtr pNewFilter,PyFilterPtr previousFilter,EntityPtr previousEntity,MovementFilter*previousNative,bool previousAttached){' + branch + 'return 0;}\n' + r'''
int main(){
 for(int change=0;change<8;++change){
  PyEntity owner;auto entity=std::make_shared<Entity>();auto previous=std::make_shared<PyFilter>();
  auto next=std::make_shared<PyFilter>();auto alternate=std::make_shared<PyFilter>();MovementFilter original,other;
  entity->filter=&original;previous->current=&original;previous->attached=false;
  next->owner=&owner;owner.pEntity_=entity;owner.pPyFilter_=next;
  if(change==1)owner.pEntity_=std::make_shared<Entity>();
  if(change==2)owner.pPyFilter_=alternate;
  if(change==3)entity->filter=&other;
  if(change==4)previous->current=nullptr;
  if(change==5)entity->destroyed=true;
  if(change==6){owner.pEntity_=std::make_shared<Entity>();owner.pPyFilter_=alternate;}
  assert(owner.complete(next,previous,entity,&original,change!=7)==-1);
  if(change==0||change==7){
   assert(owner.pPyFilter_==previous&&previous->owner==&owner&&previous->attached==(change!=7));
   assert(!next->attached&&next->owner==nullptr&&entity->filter==&original);
  }else{
   assert(owner.pPyFilter_==(change==2||change==6?alternate:next));
   assert(next->attached&&next->owner==&owner&&!previous->attached&&previous->owner==nullptr);
  }
 }
}
'''


def patch_module():
    spec = importlib.util.spec_from_file_location('entity_filter_patch', ROOT / 'bigworld/patch_stock_entity_filter.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_public_fixture(root):
    with tarfile.open(os.environ['WOT_BIGWORLD_SOURCE_ARCHIVE']) as archive:
        for name in PUBLIC_SOURCES:
            member = next(item for item in archive.getmembers()
                          if item.name.endswith('/programming/bigworld/' + name))
            data = archive.extractfile(member).read().decode('utf-8')
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(data)

    old_patch_module().patch_engine(root)


def old_patch_module():
    spec = importlib.util.spec_from_file_location('old_vehicle_patch', ROOT / 'bigworld/patch_stock_vehicle_filter.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def run_cpp(source):
    with tempfile.TemporaryDirectory(prefix='entity-filter-contract-') as temporary:
        root = Path(temporary)
        (root / 'probe.cpp').write_text(source)
        command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                   '-ffp-contract=off', '-fno-fast-math']
        if sys.platform != 'win32':
            command += ['-fsanitize=address,undefined']
        subprocess.run(command + [str(root / 'probe.cpp'), '-o', str(root / 'probe')], check=True)
        subprocess.run([str(root / 'probe')], check=True)


class StockEntityFilterTests(unittest.TestCase):
    def test_transfer_preserves_ring_controls_and_source_with_sdk_boundary_spies(self):
        run_cpp(transfer_harness())

    def test_sync_clock_uses_message_receipts_and_contains_missing_connection(self):
        run_cpp(clock_harness())

    def test_extrapolation_proxy_owns_wrapper_copies_values_and_rejects_reentrant_rebind(self):
        run_cpp(extrapolation_proxy_harness())

    def test_geometry_uses_actual_model_identity_and_discards_reentrant_bounds(self):
        run_cpp(geometry_owner_harness())

    def test_production_input_reset_callback_and_reentrant_retirement(self):
        run_cpp(input_control_harness())

    def test_avatar_drop_boids_inherit_actual_lag_and_player_retains_false(self):
        if not os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE'):
            self.skipTest('Set the pinned public source archive')
        run_cpp(lag_harness())

    def test_failed_creation_restores_only_unchanged_previous_published_owner(self):
        if not os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE'):
            self.skipTest('Set the pinned public source archive')
        run_cpp(failed_creation_rollback_harness())

    def test_public_patch_is_atomic_idempotent_and_keeps_interpolation(self):
        if not os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE'):
            self.skipTest('Set the pinned public source archive')
        with tempfile.TemporaryDirectory(prefix='entity-filter-patch-') as temporary:
            root = Path(temporary)
            write_public_fixture(root)
            original = {name: (root / name).read_bytes() for name in PUBLIC_SOURCES}
            module = patch_module()
            module.patch_engine(root)
            patched = {name: (root / name).read_bytes() for name in PUBLIC_SOURCES}
            old_patch_module().patch_engine(root)
            module.patch_engine(root)
            self.assertEqual(patched, {name: (root / name).read_bytes() for name in PUBLIC_SOURCES})
            entity = root / 'client/py_entity.cpp'
            entity.write_text(entity.read_text().replace(
                'bindStockVehicleFilterOwner( expected.get(), this );', 'unknown_owner_association();'))
            unknown = entity.read_bytes()
            with self.assertRaises(ValueError):
                old_patch_module().patch_engine(root)
            self.assertEqual(unknown, entity.read_bytes())
            entity.write_bytes(patched['client/py_entity.cpp'])
            helper = 'lib/connection/avatar_filter_helper.cpp'
            transformed = patched[helper].decode().replace(
                '\tstockExtrapolating_ = pHelper ? pHelper->stockExtrapolating_ : false;', '').replace(
                '\t// 007ea080 enters lag at equality too; retain the public interpolation below.\n'
                '\tstockExtrapolating_ = this->getStoredInput( 0 ).time_ <= time;\n\n', '')
            self.assertEqual(original[helper].decode().replace('init( const AvatarFilterHelper * pHelper )\n{',
                              'init( const AvatarFilterHelper * pHelper )\n{\n'), transformed)
            for name, data in original.items():
                (root / name).write_bytes(data)
            # Failure at the final source seam must leave every earlier file untouched.
            final = PUBLIC_SOURCES[-1]
            (root / final).write_text('unknown source')
            before = {name: (root / name).read_bytes() for name in PUBLIC_SOURCES}
            with self.assertRaises(ValueError):
                module.patch_engine(root)
            self.assertEqual(before, {name: (root / name).read_bytes() for name in PUBLIC_SOURCES})

    def test_publication_keeps_both_owners_and_rejects_stale_candidates(self):
        if not os.environ.get('WOT_BIGWORLD_SOURCE_ARCHIVE'):
            self.skipTest('Set the pinned public source archive')
        with tempfile.TemporaryDirectory(prefix='entity-filter-publication-') as temporary:
            directory = Path(temporary)
            (directory / 'probe.cpp').write_text(publication_harness())
            command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined']
            subprocess.run(command + [str(directory / 'probe.cpp'), '-o', str(directory / 'probe')], check=True)
            subprocess.run([str(directory / 'probe')], check=True)


if __name__ == '__main__':
    unittest.main()
