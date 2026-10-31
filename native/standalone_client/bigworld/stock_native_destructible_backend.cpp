#include "pch.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_destructible_falling_query.hpp"
#include "pyscript/script_math.hpp"
#include "space/space_manager.hpp"

#include <cmath>
#include <algorithm>

namespace stock_native_destructible {
namespace {
BW::Matrix chunkMatrix(std::uint32_t chunk) {
    const int encoded=int(chunk);const int x=encoded>=0?encoded/256:-int((-std::int64_t(encoded)+255)/256);
    BW::Matrix result;result.setTranslate(BW::Vector3(float(x-127)*100.f,0.f,float((encoded&255)-127)*100.f));return result;
}
Frame values(const BW::Matrix & source) {
    Frame out;for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)out[row*4+column]=source.row(row)[column];return out;
}
BW::Matrix matrix(const Frame & source) {
    BW::Matrix out;for(unsigned row=0;row<4;++row)out.row(row,BW::Vector4(source[row*4],source[row*4+1],source[row*4+2],source[row*4+3]));return out;
}
const char * category(int value) {
    return value==0?"trees":value==1?"fallingAtoms":value==2?"fragiles":value==3?"structures":"";
}
}
NativeBackend::NativeBackend(BW::ClientSpacePtr space,stock_native_world::Registry & registry,
    const std::function<bool(const stock_native_world::TurretSweep &)> & turret):
    space_(space),registry_(registry),runtime_(*this),turret_(turret) {}
NativeBackend::~NativeBackend(){clear();}
bool NativeBackend::callPresentation(PyObject * function,PyObject * arguments,PresentationPhase phase) {
    if(!arguments)return false;
    if(!function||!current()){Py_DECREF(arguments);return false;}
    ++presentation_counters_.python_boundary_calls;
    if(phase==PresentationPhase::StopLifetime)++presentation_counters_.lifetime_stop_calls;
    else if(phase==PresentationPhase::RemoveLightSound)++presentation_counters_.light_sound_calls;
    else ++presentation_counters_.emit_calls;
    Py_INCREF(function);PyObject * result=PyObject_CallObject(function,arguments);Py_DECREF(arguments);Py_DECREF(function);
    if(!result){++presentation_counters_.failed_calls;if(PyErr_Occurred())PyErr_Print();return false;}
    Py_DECREF(result);return current();
}
bool NativeBackend::installPresenter(PyObject * emit,PyObject * stop,PyObject * remove) {
    if(retired_||!Py_IsInitialized())return false;
    if(!emit||!stop||!remove||!PyCallable_Check(emit)||!PyCallable_Check(stop)||!PyCallable_Check(remove))return false;
    Py_INCREF(emit);Py_INCREF(stop);Py_INCREF(remove);
    Py_XDECREF(emit_);Py_XDECREF(stop_);Py_XDECREF(remove_sound_);emit_=emit;stop_=stop;remove_sound_=remove;return true;
}
void NativeBackend::bind(const stock_native_world::Key & key,const BW::StockDestructibleBinding & binding){
    if(retired_)return;
    const auto old=bindings_.find(key);if(old==bindings_.end()||!(old->second==binding))incarnations_[key]=++next_incarnation_;
    bindings_[key]=binding;
}
void NativeBackend::installTree(const stock_native_world::Key & key,const Parameters & parameters,PyObject * fracture,PyObject * touchdown) {
    if(retired_)return;
    auto & resource=trees_[key];Py_XINCREF(fracture);Py_XINCREF(touchdown);Py_XDECREF(resource.fracture);Py_XDECREF(resource.touchdown);
    resource.parameters=parameters;resource.fracture=fracture;resource.touchdown=touchdown;
}
void NativeBackend::clear() {
    retire();
    runtime_.clear();bindings_.clear();incarnations_.clear();presentation_bindings_.clear();initial_.clear();
    if(Py_IsInitialized()) {
        for(auto & tree:trees_){Py_XDECREF(tree.second.fracture);Py_XDECREF(tree.second.touchdown);}
        Py_XDECREF(emit_);Py_XDECREF(stop_);Py_XDECREF(remove_sound_);
    }
    trees_.clear();emit_=stop_=remove_sound_=nullptr;
}
bool NativeBackend::current()const {
    return !retired_&&space_&&!space_->cleared()&&BW::SpaceManager::instance().space(space_->id())==space_;
}
bool NativeBackend::read(const Destruction & order,BW::StockDestructibleSnapshot & state,BW::StockDestructibleBinding & binding,int module)const {
    if(!current()||!BW::readStockDestructible(space_->id(),order.key.first,order.key.second,module,state,&binding))return false;
    const BW::StockDestructibleBinding * expected=nullptr;
    const auto found=bindings_.find(order.key);if(found!=bindings_.end())expected=&found->second;
    const auto temporary=presentation_bindings_.find(order.key);if(temporary!=presentation_bindings_.end())expected=&temporary->second;
    if(!expected)return false;
    // Modules may resolve another compiled object within the same retained
    // mapping. Whole-item identity must additionally match the exact endpoint.
    return module<0?binding==*expected:
        binding.owner==expected->owner&&binding.mapping==expected->mapping&&binding.objects==expected->objects;
}
SnapshotStatus NativeBackend::snapshot(const Destruction & order,Parameters & parameters) {
    BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;
    if(!read(order,state,binding))return SnapshotStatus::Missing;
    const auto admitted=registry_.instances.find(order.key);
    if(admitted==registry_.instances.end()||admitted->second.admission!=stock_native_world::Admission::Admitted||registry_.isolated(order.key))return SnapshotStatus::Invalid;
    BW::Matrix local,inverse;const auto chunk=chunkMatrix(order.key.first);
    if(!inverse.invert(chunk))return SnapshotStatus::Invalid;
    D3DXMatrixMultiply(&local,&state.world,&inverse);
    auto initial=initial_.find(order.key);if(initial==initial_.end())initial=initial_.emplace(order.key,local).first;
    if(order.kind==Kind::Tree) {
        const auto tree=trees_.find(order.key);if(tree==trees_.end())return SnapshotStatus::Invalid;
        parameters=tree->second.parameters;
    } else if(order.kind==Kind::Falling) {
        const auto & p=state.fallingParameters;
        parameters.height=p.row(0)[1];parameters.mass=p.row(0)[0];parameters.air_resist=p.row(0)[2];parameters.bury_depth=p.row(0)[3];
        parameters.stiffness=p.row(1)[0];parameters.spring_angle=p.row(1)[1];parameters.spring_resist=p.row(1)[2];
    }
    parameters.initial=values(initial->second);
    parameters.scale=initial->second.applyVector(BW::Vector3(0.f,1.f,0.f)).length();
    return SnapshotStatus::Ready;
}
bool NativeBackend::fallPitch(const Destruction & order,double yaw,double & pitch,std::uint32_t & flags) {
    BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;if(!read(order,state,binding)||!std::isfinite(yaw))return false;
    const auto chunk=chunkMatrix(order.key.first);BW::Matrix inverse,local;if(!inverse.invert(chunk))return false;
    D3DXMatrixMultiply(&local,&state.world,&inverse);
    const auto origin=local.applyToOrigin()+chunk.applyToOrigin();
    pitch=BW::queryStockDestructibleFallPitch(*space_,origin,state.height,float(yaw));flags=0;return current();
}
bool NativeBackend::position(const Body & body,double wind) {
    BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;if(!read(body.order,state,binding))return current()&&!loaded(body.order.key.first);
    auto pre=matrix(body.initial);const auto translation=pre.applyToOrigin();pre.translation(BW::Vector3(0.f,0.f,0.f));
    BW::Matrix inverse_yaw,yaw,posed,world;inverse_yaw.setRotateY(-float(body.fall_yaw));pre.postMultiply(inverse_yaw);
    yaw.setRotateY(float(body.fall_yaw));posed.setRotateX(float(body.pitch));posed.preMultiply(pre);posed.postMultiply(yaw);
    posed.translation(translation-BW::Vector3(0.f,float(body.bury_depth*std::sin(body.pitch)),0.f));
    const auto chunk=chunkMatrix(body.order.key.first);D3DXMatrixMultiply(&world,&posed,&chunk);
    std::string error;return binding.objects->transform(binding.index,world,float(wind),error)&&current();
}
bool NativeBackend::geometry(const Destruction & event,const ChildEffect & child) {
    const int module=event.kind==Kind::Structure?int(event.material-73):-1;
    BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;if(!read(event,state,binding,module))return false;
    std::string error;
    return binding.objects->destroyNative(binding.index,true,event.shot,false,error,[&](BW::uint32 index) {
        BW::uint32 chunk=0,item=0,material=0;if(!BW::resolveStockDestructibleChild(binding,index,chunk,item,material))return false;
        auto order=event;order.kind=Kind::Structure;order.key={chunk,item};order.material=material+73;
        const auto old=presentation_bindings_.find(order.key);const bool existed=old!=presentation_bindings_.end();BW::StockDestructibleBinding previous;if(existed)previous=old->second;
        presentation_bindings_[order.key]=binding;
        try {const bool result=child(order)&&current();if(existed)presentation_bindings_[order.key]=previous;else presentation_bindings_.erase(order.key);return result;}
        catch(...){if(existed)presentation_bindings_[order.key]=previous;else presentation_bindings_.erase(order.key);throw;}
    })&&current();
}
bool NativeBackend::present(const Destruction & event,int module,PresentationPhase phase) {
    if(!current()||!Py_IsInitialized()||!emit_||!stop_||!remove_sound_)return false;
    if(phase==PresentationPhase::StopLifetime)return callPresentation(stop_,Py_BuildValue("(iIIi)",int(space_->id()),event.key.first,event.key.second,module),phase)&&current();
    if(phase==PresentationPhase::RemoveLightSound)return callPresentation(remove_sound_,Py_BuildValue("(iII)",int(space_->id()),event.key.first,event.key.second),phase)&&current();
    BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;
    if(!read(event,state,binding,event.kind==Kind::Structure?module:-1))return current()&&!loaded(event.key.first);
    PyObject * variants=nullptr;const char * name="none";
    const bool falling=event.kind==Kind::Tree||event.kind==Kind::Falling;
    BW::Vector3 position,direction;double scale=state.effectScale;
    if(falling) {
        position=state.world.applyToOrigin();direction=BW::Vector3(float(std::sin(encodedYaw(event.yaw))),0.f,float(std::cos(encodedYaw(event.yaw))));
        if(event.kind==Kind::Tree) {
            const auto tree=trees_.find(event.key);if(tree==trees_.end())return false;
            variants=phase==PresentationPhase::Fracture?tree->second.fracture:phase==PresentationPhase::Touchdown?tree->second.touchdown:nullptr;
            const auto cell=chunkMatrix(event.key.first);BW::Matrix inverse,local;if(!inverse.invert(cell))return false;D3DXMatrixMultiply(&local,&state.world,&inverse);
            const double tree_scale=local.applyVector(BW::Vector3(0.f,1.f,0.f)).length();scale=1.+(tree_scale-1.)*.7;
        } else {
            const unsigned slot=phase==PresentationPhase::Fracture?1:phase==PresentationPhase::Touchdown?2:6;
            if(slot<state.effects.size())name=state.effects[slot].c_str();
        }
    } else {
        position=state.hardpointWorld.applyToOrigin();direction=state.hardpointWorld.applyVector(BW::Vector3(0.f,0.f,1.f));
        const unsigned slot=phase==PresentationPhase::Decay?3:phase==PresentationPhase::Hit?5:4;
        name=state.effects[slot].c_str();
    }
    PyObject * resource=variants?variants:PyString_FromString(name);if(variants)Py_INCREF(resource);
    if(!resource)return false;
    PyObject * arguments=Py_BuildValue("(Os(fff)(fff)dO)",resource,category(state.category),position.x,position.y,position.z,direction.x,direction.y,direction.z,scale,falling?Py_None:Py_False);
    Py_DECREF(resource);return callPresentation(emit_,arguments,phase)&&current();
}
bool NativeBackend::destroy(const Destruction & event){return runtime_.destroy(event);}
std::uint64_t NativeBackend::generation(const Destruction&event)const {
    if(!current())return 0;const auto found=incarnations_.find(event.key);return found==incarnations_.end()?0:found->second;
}
bool NativeBackend::replay(const Destruction&event,const Body*body) {
    if(body)return position(*body,std::max(1.-body->simulation_time/2.,0.));
    const int module=event.kind==Kind::Structure?int(event.material-73):-1;
    BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;if(!read(event,state,binding,module))return false;
    std::string error;return binding.objects->destroyNative(binding.index,false,event.shot,false,error,[](BW::uint32){return true;})&&current();
}
bool NativeBackend::turretSweep(const stock_native_world::TurretSweep & sweep){return current()&&turret_&&turret_(sweep)&&current();}
bool NativeBackend::loaded(std::uint32_t chunk)const {
    if(!current())return false;std::vector<BW::uint32> chunks;
    return BW::stockDestructibleChunks(space_->id(),chunks)&&std::binary_search(chunks.begin(),chunks.end(),BW::uint32(chunk));
}
bool NativeBackend::pose(stock_native_bot_foliage::Key key,const stock_native_bot_foliage::Point& center,
    const stock_native_bot_foliage::Point& half_sizes,stock_native_bot_foliage::Point& output,
    std::array<stock_native_bot_foliage::Point,3>& axes) {
    Destruction order;order.key=key;order.kind=Kind::Tree;BW::StockDestructibleSnapshot state;BW::StockDestructibleBinding binding;
    if(!read(order,state,binding))return false;BW::Matrix local,chunk;
    BW::stockDestructibleLocalMatrix(key.first,state.world,local,chunk);
    const auto native_center=local.applyPoint(BW::Vector3(float(center[0]),float(center[1]),float(center[2]))),translation=chunk.applyToOrigin();
    output={{double(native_center.x)+double(translation.x),double(native_center.y)+double(translation.y),double(native_center.z)+double(translation.z)}};
    for(unsigned i=0;i<3;++i){BW::Vector3 axis(0.f,0.f,0.f);axis[i]=float(half_sizes[i]);const auto value=local.applyVector(axis);axes[i]={{value.x,value.y,value.z}};}
    return current();
}
}
