#include "pch.hpp"
#include "stock_offline_query_bridge.hpp"
#include "stock_offline_query_skin.hpp"
#include "stock_battle_scene_queries.hpp"
#include "stock_rotation_stage_timing.hpp"
#include "pyscript/script_math.hpp"

#include <algorithm>
#include <climits>
#include <exception>
#include <pythread.h>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace stock_offline_query {
namespace timing = stock_rotation_timing;
namespace {
struct Unsupported { const char * reason; explicit Unsupported(const char * r="unsupported_schema"):reason(r){} };
struct Error {};
Result countedRayResult(Result result) noexcept {
    switch(result) {
    case Unbound: timing::count(timing::Counter::NativeUnbound); break;
    case Miss: timing::count(timing::Counter::NativeMiss); break;
    case Found: timing::count(timing::Counter::NativeFound); break;
    case Failed: timing::count(timing::Counter::NativeFailed); break;
    }
    return result;
}
class Ref {
    PyObject * p_;
public:
    Ref():p_(NULL) {}
    explicit Ref(PyObject * p):p_(p) {}
    static Ref borrow(PyObject * p) { Py_XINCREF(p); return Ref(p); }
    Ref(const Ref & r):p_(r.p_) { Py_XINCREF(p_); }
    Ref(Ref && r) noexcept:p_(r.p_) { r.p_=NULL; }
    ~Ref() { Py_XDECREF(p_); }
    Ref & operator=(Ref r) { std::swap(p_,r.p_); return *this; }
    PyObject * get() const { return p_; }
    PyObject * release() { auto p=p_;p_=NULL;return p; }
};
PyObject * required(PyObject * p) { if(!p)throw Error(); return p; }
PyObject * dict(PyObject * p) { if(!p || !PyDict_CheckExact(p))throw Unsupported();return p; }
PyObject * field(PyObject * p,const char * name) { return PyDict_GetItemString(dict(p),name); }
Ref attr(PyObject * p,const char * name) { return Ref(required(PyObject_GetAttrString(p,name))); }
PyObject * tuple(PyObject * p,Py_ssize_t n) {
    if(!p || !PyTuple_CheckExact(p) || PyTuple_GET_SIZE(p)!=n)throw Unsupported();return p;
}
PyObject * at(PyObject * p,Py_ssize_t i) {return PyTuple_GET_ITEM(p,i);}
bool integer(PyObject * p) {return p && (PyInt_CheckExact(p) || PyLong_CheckExact(p));}
std::int64_t number(PyObject * p) {
    if(!integer(p))throw Unsupported();
    const auto n=PyLong_CheckExact(p)?PyLong_AsLongLong(p):PyInt_AS_LONG(p);
    if(PyErr_Occurred())throw Error();return n;
}
std::uint32_t u32(PyObject * p) {
    const auto n=number(p);
    if(n<0 || static_cast<std::uint64_t>(n)>0xffffffffULL)throw Unsupported();
    return static_cast<std::uint32_t>(n);
}
double scalar(PyObject * p) {
    if(!p)throw Unsupported();
    if(PyFloat_CheckExact(p))return PyFloat_AS_DOUBLE(p);
    return static_cast<double>(number(p));
}
PyObject * uintObject(std::uint32_t v) {
    return v<=0x7fffffffu?PyInt_FromLong(static_cast<long>(v)):PyLong_FromUnsignedLong(v);
}
bool sequence(PyObject * p) {return p && (PyTuple_CheckExact(p) || PyList_CheckExact(p));}
Py_ssize_t size(PyObject * p) {
    if(PyTuple_CheckExact(p))return PyTuple_GET_SIZE(p);
    if(PyList_CheckExact(p))return PyList_GET_SIZE(p);
    throw Unsupported();
}
PyObject * item(PyObject * p,Py_ssize_t i) {return PyTuple_CheckExact(p)?PyTuple_GET_ITEM(p,i):PyList_GET_ITEM(p,i);}
template<class F> void each(PyObject * p,F fn) {
    if(!p || p==Py_None)return;
    if(sequence(p)) {for(Py_ssize_t i=0;i<size(p);++i)fn(item(p,i));return;}
    if(PyDict_CheckExact(p)){Py_ssize_t pos=0;PyObject * key,* value;while(PyDict_Next(p,&pos,&key,&value))fn(key);return;}
    if(!(Py_TYPE(p)==&PySet_Type) && !PyFrozenSet_CheckExact(p))throw Unsupported();
    Ref iterator(required(PyObject_GetIter(p)));
    while(PyObject * value=PyIter_Next(iterator.get())) {Ref owned(value);fn(value);}
    if(PyErr_Occurred())throw Error();
}
template<class F> void mapping(PyObject * p,F fn) {
    if(!p || p==Py_None)return;
    dict(p);Py_ssize_t pos=0;PyObject * key,* value;
    while(PyDict_Next(p,&pos,&key,&value))fn(key,value);
}
Key key(PyObject * p) {tuple(p,2);return Key(u32(at(p,0)),u32(at(p,1)));}
Cell cell(PyObject * p) {
    tuple(p,2);const auto a=number(at(p,0)),b=number(at(p,1));
    if(a<INT_MIN || a>INT_MAX || b<INT_MIN || b>INT_MAX)throw Unsupported();
    return Cell(static_cast<int>(a),static_cast<int>(b));
}
std::int64_t material(PyObject * p) {return p==Py_None?-1:number(p);}
std::set<Key> keys(PyObject * p) {std::set<Key> out;each(p,[&](PyObject * v){out.insert(key(v));});return out;}
std::set<MaterialKey> materials(PyObject * p) {
    std::set<MaterialKey> out;each(p,[&](PyObject * v){tuple(v,3);out.emplace(u32(at(v,0)),u32(at(v,1)),material(at(v,2)));});return out;
}
Point point(PyObject * p) {
    if(!sequence(p) || size(p)!=3)throw Unsupported();
    return Point{{scalar(item(p,0)),scalar(item(p,1)),scalar(item(p,2))}};
}
Box box(PyObject * p) {
    if(!PyTuple_CheckExact(p) || size(p)<3)throw Unsupported();
    Box b;b.center=point(at(p,0));auto axes=at(p,1);
    if(!PyTuple_CheckExact(axes) || size(axes)!=3)throw Unsupported();
    for(Py_ssize_t i=0;i<3;++i)b.axes.push_back(point(at(axes,i)));
    const auto m=material(at(p,2));if(m<INT_MIN || m>INT_MAX)throw Unsupported();b.material=static_cast<int>(m);return b;
}
bool textEquals(PyObject * value,const char * text) {
    if(!value)return false;
    if(PyString_CheckExact(value))return std::string(PyString_AS_STRING(value))==text;
    if(!PyUnicode_CheckExact(value))throw Unsupported();
    const auto count=PyUnicode_GET_SIZE(value);const auto data=PyUnicode_AS_UNICODE(value);
    std::size_t i=0;for(;text[i];++i)if(i>=static_cast<std::size_t>(count) || data[i]!=static_cast<unsigned char>(text[i]))return false;
    return i==static_cast<std::size_t>(count);
}
SkinInstance instance(PyObject * p) {
    dict(p);SkinInstance out;auto name=field(p,"kind");
    if(!name)throw Unsupported();
    out.kind=textEquals(name,"structure")?SkinKind::Structure:textEquals(name,"fragile")?SkinKind::Fragile:textEquals(name,"falling")?SkinKind::Falling:SkinKind::Other;
    auto boxes=field(p,"boxes");if(!sequence(boxes))throw Unsupported();
    for(Py_ssize_t i=0;i<size(boxes);++i)out.boxes.push_back(box(item(boxes,i)));
    return out;
}
BW::Vector3 vector(const double * p) {return BW::Vector3(static_cast<float>(p[0]),static_cast<float>(p[1]),static_cast<float>(p[2]));}
BW::Vector3 vector(const Point & p) {return vector(p.data());}
void copy(const BW::Vector3 & p,double * out) {out[0]=p.x;out[1]=p.y;out[2]=p.z;}
Point point(const BW::Vector3 & p) {return Point{{p.x,p.y,p.z}};}
double distance(void *,const Point & a,const Point & b) {return (vector(b)-vector(a)).length();}
Point bound(void *,const Point & a,const Point & b,double f) {return point(vector(a)+(vector(b)-vector(a))*static_cast<float>(f));}
Cell bin(double x,double z,double metres) {
    const double a=std::floor(x/metres),b=std::floor(z/metres);
    if(!std::isfinite(a) || !std::isfinite(b) || a<INT_MIN || a>INT_MAX || b<INT_MIN || b>INT_MAX)throw Unsupported();
    return Cell(static_cast<int>(a),static_cast<int>(b));
}
Ref surfaceTuple(const Surface & s,bool trace=false,bool keep=false) {
    Ref out(required(PyTuple_New(trace?5:4)));
    PyTuple_SET_ITEM(out.get(),0,required(uintObject(s.material)));
    PyTuple_SET_ITEM(out.get(),1,required(uintObject(s.flags)));
    PyTuple_SET_ITEM(out.get(),2,required(uintObject(s.item)));
    PyTuple_SET_ITEM(out.get(),3,required(uintObject(s.chunk)));
    if(trace)PyTuple_SET_ITEM(out.get(),4,required(PyBool_FromLong(keep?1:0)));
    return out;
}
struct Filter {
    Ref owner,treesObject,diagnostic,trace;
    bool frozen=false,enabled=false;
    std::set<Key> members,trees;
    std::set<MaterialKey> broken;
    std::vector<std::pair<Surface,bool>> traceRows;
};
struct Kept {Ref callback;Surface surface;};
struct Traced {Ref list;Surface surface;bool keep;};
struct RayEvent {bool ground;Point start,end;double seconds;};
void status(PyObject * query,const char * mode,const char * reason) {
    timing::Scope timingScope(timing::Stage::Marshal);
    // One bounded observation per synchronous binding, never a per-ray log.
    Ref value(Py_BuildValue("(ss)",mode,reason));
    if(value.get()) {
        timing::Scope callbackTiming(timing::Stage::Business);
        if(PyObject_SetAttrString(query,"_native_query_status",value.get())<0)PyErr_Clear();
    }
    Ref diagnostic(PyObject_GetAttrString(query,"_native_query_diagnostic"));
    if(!diagnostic.get()){PyErr_Clear();return;}
    if(diagnostic.get()==Py_None)return;
    const std::string name=std::string("native_query_")+mode+"_"+reason;
    timing::Scope callbackTiming(timing::Stage::Business);
    Ref count(PyObject_CallMethod(diagnostic.get(),const_cast<char *>("count"),const_cast<char *>("s"),name.c_str()));
    if(!count.get())PyErr_Clear();
}
struct GroundKey {
    std::int64_t space,revision,global,local,treesLength;
    Cell cell;
    std::uintptr_t trees;
    bool globalRevision;
    bool operator<(const GroundKey & other)const {
        return std::tie(space,revision,global,local,treesLength,cell,trees,globalRevision)<
            std::tie(other.space,other.revision,other.global,other.local,other.treesLength,other.cell,other.trees,other.globalRevision);
    }
};
}

struct Context {
    Ref query,capabilities,binding,sensor,authority,owner,avatar,runtime,bots;
    Ref generation,round,spaceObject,nativeRay,vectorType,groundBinding,diagnostic,groundOwner,groundProvider,groundCallable;
    std::int64_t space=0,thread=0;
    unsigned skip=0,budget=0;
    double epsilon=0.;
    SkinTuning tuning{};
    bool usable=true,viewReady=false,skinReady=false,ground=false;
    SkinView view;
    Ref instanceDictionary,bakedDictionary;
    std::map<Key,PyObject *> instanceRows,bakedRows;
    std::map<PyObject *,Filter> filters;
    std::map<GroundKey,Filter> groundFilters;
    std::map<Cell,std::set<Key>> groundMembers;
    std::map<Cell,std::int64_t> cellGenerations;
    std::set<Cell> dirtyCells;
    Ref groundTrees;
    std::int64_t groundSpace=0,spatialRevision=0,publishedRevision=0,globalGeneration=0;
    bool groundReady=false,globalDirty=false;
    Filter groundScratch;
    std::set<Key> replacements;
    std::set<MaterialKey> filterBroken;
    std::uint64_t skips=0;
    std::vector<Kept> kept;
    std::vector<Traced> traces;
    std::vector<RayEvent> rays;
    std::map<std::string,std::uint64_t> counters;
    struct Mark {
        std::uint64_t skips;
        std::size_t kept,traces,rays;
        std::map<std::string,std::uint64_t> counters;
    };
    Mark mark{};
    bool outerRay=false;
    void markRay() {mark={skips,kept.size(),traces.size(),rays.size(),counters};}
    void restoreRay() {skips=mark.skips;kept.resize(mark.kept);traces.resize(mark.traces);rays.resize(mark.rays);counters=mark.counters;}


    Context(PyObject * caps,PyObject * q,PyObject * data):
        query(Ref::borrow(q)),capabilities(Ref::borrow(caps)),binding(Ref::borrow(data)),
        sensor(Ref::borrow(at(data,1))),authority(Ref::borrow(at(data,2))),
        owner(attr(q,"owner")),avatar(attr(q,"_avatar")),runtime(attr(q,"_runtime")),bots(attr(q,"_bots")),
        generation(attr(q,"_generation")),round(attr(q,"_round")),spaceObject(attr(q,"_space")),
        nativeRay(attr(q,"_native_ray")),vectorType(attr(q,"_vector")),
        groundBinding(attr(q,"_native_query_ground")),diagnostic(attr(q,"_native_query_diagnostic")) {
        auto plainToken=[](PyObject * value){return value==Py_None || integer(value) || value==Py_True || value==Py_False || PyFloat_CheckExact(value) || PyString_CheckExact(value) || PyUnicode_CheckExact(value);};
        if(!plainToken(generation.get()) || !plainToken(round.get()))throw Unsupported("custom_owner_token");
        space=number(spaceObject.get());thread=number(attr(q,"_thread").get());
        const auto flags=number(attr(q,"_skip_flags").get());if(flags<0 || flags>65535)throw Unsupported();skip=static_cast<unsigned>(flags);
        tuple(at(caps,17),2);const auto b=number(at(at(caps,17),0));
        epsilon=scalar(at(at(caps,17),1));if(b<0 || b>1024 || epsilon<=0.)throw Unsupported();budget=static_cast<unsigned>(b);
        tuning={scalar(field(sensor.get(),"_SHOT_RAY_EPSILON")),scalar(field(sensor.get(),"_CATALOG_POINT_EPSILON")),scalar(field(sensor.get(),"_DESTRUCTIBLE_BIN_METRES"))};
        if(tuning.bin_metres!=8. || !BW::stockSegmentQueryCallable(nativeRay.get()))throw Unsupported("custom_native_ray");
        Ref nativeVector(required(BW::Script::getData(BW::Vector3(0.f,0.f,0.f))));
        if(reinterpret_cast<PyObject *>(Py_TYPE(nativeVector.get()))!=vectorType.get())throw Unsupported("custom_vector_type");
        validate();live();
        if(diagnostic.get()!=Py_None)counters["native_query_context"]=1;
        ground=groundBinding.get()!=Py_None;
        groundOwner=attr(q,"_native_query_ground_owner");groundProvider=attr(q,"_native_query_ground_provider");groundCallable=attr(q,"_native_query_ground_callable");
        if(ground){tuple(groundBinding.get(),2);if(at(groundBinding.get(),0)!=field(sensor.get(),"_DEFAULT_GROUND_FILTER"))throw Unsupported();
            if(!PyFunction_Check(groundCallable.get()))throw Unsupported("custom_ground_batch");
            bool canonical=false;auto codes=at(binding.get(),8);for(Py_ssize_t i=0;i<size(codes);++i){auto row=tuple(at(codes,i),2);if(number(at(row,0))==3 && at(row,1)==PyFunction_GET_CODE(groundCallable.get()))canonical=true;}
            if(!canonical)throw Unsupported("custom_ground_batch");
        }
    }
    void validate() {
        timing::Scope timingScope(timing::Stage::BindingValidation);
        if(field(sensor.get(),"_DIAGNOSTICS_ENABLED")==Py_True) {
            auto pending=field(sensor.get(),"g_offh_destr_pending");
            if(pending && PyObject_IsTrue(pending))throw Unsupported();
        }
        if(groundOwner.get() && groundOwner.get()!=Py_None && attr(groundOwner.get(),"ground_collision_filter").get()!=groundProvider.get())ground=false;
        if(attr(query.get(),"_native_query_binding").get()!=binding.get())throw Unsupported("changed_query_binding");
        if(attr(query.get(),"capabilities").get()!=capabilities.get() || at(binding.get(),0)!=capabilities.get())throw Unsupported();
        auto laws=at(binding.get(),3);if(!PyTuple_CheckExact(laws))throw Unsupported();
        for(Py_ssize_t i=0;i<size(laws);++i){auto row=tuple(at(laws,i),4);dict(at(row,0));
            if(PyDict_GetItem(at(row,0),at(row,1))!=at(row,2) || (PyFunction_Check(at(row,2)) && PyFunction_GET_CODE(at(row,2))!=at(row,3)))throw Unsupported("changed_sensor_law");}
        auto privateValues=tuple(at(binding.get(),7),4);
        static const char * captures[]={"_vector","_xyz","_native_ray","_skip_flags"};
        for(unsigned i=0;i<4;++i)if(attr(query.get(),captures[i]).get()!=at(privateValues,i))throw Unsupported("changed_query_capture");
        auto engine=dict(at(binding.get(),4));auto cls=field(engine,"EngineQuery");
        auto methods=tuple(at(binding.get(),5),13);
        static const char * names[]={"_ray","_skin","_live","_segment","_scale","_normalise","_filter","_alias","_can_recast","_skipped","_invoke","_recover_ground","_remember"};
        auto methodCodes=tuple(field(engine,"_NATIVE_QUERY_METHOD_CODES"),13);
        for(unsigned i=0;i<13;++i){auto method=attr(cls,names[i]);PyObject * function=PyMethod_Check(method.get())?PyMethod_GET_FUNCTION(method.get()):method.get();if(function!=at(methods,i) || PyFunction_GET_CODE(function)!=at(methodCodes,i))throw Unsupported("changed_query_method");}
        auto geometryModule=field(sensor.get(),"_native_geometry"),geometryMethods=field(sensor.get(),"_native_query_geometry_methods");
        tuple(geometryMethods,3);static const char * geometryNames[]={"box_probe","motion_witnesses","box_envelope"};
        auto geometryCodes=tuple(field(sensor.get(),"_native_query_geometry_codes"),3);
        for(unsigned i=0;i<3;++i){auto function=attr(geometryModule,geometryNames[i]);if(function.get()!=at(geometryMethods,i) || !PyFunction_Check(function.get()) || PyFunction_GET_CODE(function.get())!=at(geometryCodes,i))throw Unsupported("custom_geometry_helper");}
        auto witness=field(sensor.get(),"_MotionSkinWitnesses1513"),witnessMethods=field(sensor.get(),"_native_query_witness_methods");
        tuple(witnessMethods,7);static const char * witnessNames[]={"__init__","prime","inside","interval","footprint","projected","side"};
        auto witnessCodes=tuple(field(sensor.get(),"_native_query_witness_codes"),7);
        for(unsigned i=0;i<7;++i){auto method=attr(witness,witnessNames[i]);auto function=PyMethod_Check(method.get())?PyMethod_GET_FUNCTION(method.get()):method.get();if(function!=at(witnessMethods,i) || PyFunction_GET_CODE(function)!=at(witnessCodes,i))throw Unsupported("custom_skin_witness");}
        if(PyMethod_GET_FUNCTION(at(capabilities.get(),7))!=at(methods,0) || PyMethod_GET_FUNCTION(at(capabilities.get(),8))!=at(methods,1))throw Unsupported();
        auto math=attr(runtime.get(),"math"),bw=attr(runtime.get(),"bigworld");
        if(attr(math.get(),"Vector3").get()!=vectorType.get() || attr(bw.get(),"wg_collideSegment").get()!=nativeRay.get())throw Unsupported();
    }
    void live() {
        timing::Scope timingScope(timing::Stage::LifeGuard);
        bool equal=true;
        auto eq=[&](PyObject * a,PyObject * b){if(a==b)return true;const int result=PyObject_RichCompareBool(a,b,Py_EQ);if(result<0)throw Error();return result!=0;};
        equal=PyThread_get_thread_ident()==thread && attr(owner.get(),"_avatar").get()==avatar.get() && attr(owner.get(),"_runtime").get()==runtime.get();
        if(equal)equal=eq(attr(avatar.get(),"spaceID").get(),spaceObject.get());
        Ref currentGeneration(PyObject_GetAttrString(owner.get(),"_generation"));if(!currentGeneration.get() && PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();currentGeneration=Ref::borrow(Py_None);}
        Ref currentBots(PyObject_GetAttrString(owner.get(),"_bots"));if(!currentBots.get() && PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();currentBots=Ref::borrow(Py_None);}
        Ref currentRound(PyObject_GetAttrString(bots.get(),"round_id"));if(!currentRound.get() && PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();currentRound=Ref::borrow(Py_None);}
        if(!currentGeneration.get() || !currentBots.get() || !currentRound.get())throw Error();
        if(!equal || !eq(currentGeneration.get(),generation.get()) || currentBots.get()!=bots.get() || !eq(currentRound.get(),round.get())){
            PyErr_SetString(PyExc_RuntimeError,"Native engine query owner changed");throw Error();}
    }
    void ledger() {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        if(viewReady)return;
        view.destroyed.clear();filterBroken.clear();replacements.clear();
        auto state=field(authority.get(),"_state");auto chunks=state?field(state,"chunks"):NULL;
        auto cache=field(sensor.get(),"g_offh_destr_broken_cache");
        mapping(chunks,[&](PyObject * chunk,PyObject * value){const auto id=u32(chunk);auto raw=field(value,"keys");std::size_t count=0;
            std::set<MaterialKey> rawKeys;each(raw,[&](PyObject * row){tuple(row,2);rawKeys.emplace(id,u32(at(row,0)),material(at(row,1)));++count;});
            view.destroyed.insert(rawKeys.begin(),rawKeys.end());
            PyObject * cached=cache?PyDict_GetItem(dict(cache),chunk):NULL;
            if(cached && PyTuple_CheckExact(cached) && size(cached)==2 && number(at(cached,0))==static_cast<std::int64_t>(count)){
                mapping(at(cached,1),[&](PyObject * idx,PyObject * ms){each(ms,[&](PyObject * m){filterBroken.emplace(id,u32(idx),material(m));});});
            }else filterBroken.insert(rawKeys.begin(),rawKeys.end());});
        view.speculative=materials(field(sensor.get(),"g_offh_destr_speculative"));
        each(field(sensor.get(),"g_offh_destr_native_fragile_replacements"),[&](PyObject * row){tuple(row,3);auto runtimeSpace=field(sensor.get(),"g_offh_destr_runtime_space");
            if(runtimeSpace && number(at(row,0))==number(runtimeSpace))replacements.emplace(u32(at(row,1)),u32(at(row,2)));});
        viewReady=true;
    }
    void identities() {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        if(skinReady)return;ledger();
        const bool measured=diagnostic.get()!=Py_None;
        const auto started=measured?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
        view.instances.clear();view.baked_instances.clear();view.tree_instances.clear();view.contact_bins.clear();view.baked_shot_bins.clear();view.excluded_instances.clear();view.layout_repairs.clear();view.layout_generations.clear();
        instanceRows.clear();bakedRows.clear();
        view.isolated_slots=keys(field(sensor.get(),"g_offh_destr_isolated_slots"));view.isolated_chunks.clear();
        each(field(sensor.get(),"g_offh_destr_isolated_chunks"),[&](PyObject * v){view.isolated_chunks.insert(u32(v));});
        auto catalog=field(sensor.get(),"_destructible_catalog");if(catalog==Py_None)catalog=NULL;
        auto instances=field(sensor.get(),"g_offh_destr_instances");instanceDictionary=Ref::borrow(instances);
        mapping(instances,[&](PyObject * k,PyObject * v){instanceRows.emplace(key(k),v);});
        if(catalog){
            auto baked=field(catalog,"baked_instances");bakedDictionary=Ref::borrow(baked);
            mapping(baked,[&](PyObject * k,PyObject * v){bakedRows.emplace(key(k),v);});
            mapping(field(catalog,"tree_instances"),[&](PyObject * k,PyObject *){view.tree_instances.insert(key(k));});
            view.excluded_instances=keys(field(catalog,"excluded_instances"));
            each(field(catalog,"layout_repairs"),[&](PyObject * v){view.layout_repairs.insert(u32(v));});
            mapping(field(catalog,"layout_generations"),[&](PyObject * k,PyObject * v){view.layout_generations.emplace(u32(k),number(v));});
        }
        skinReady=true;
        if(measured){counters["native_query_identity_rows"]+=instanceRows.size()+bakedRows.size()+view.tree_instances.size();counters["native_query_identity_microseconds"]+=static_cast<std::uint64_t>(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count());}
    }
    void geometry(const Point & hit,const Point & start,const Point & end,const std::vector<Surface> & surfaces) {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        identities();
        const double x0=std::min(hit[0],std::min(start[0],end[0])),x1=std::max(hit[0],std::max(start[0],end[0]));
        const double z0=std::min(hit[2],std::min(start[2],end[2])),z1=std::max(hit[2],std::max(start[2],end[2]));
        const auto lower=bin(x0,z0,tuning.bin_metres),upper=bin(x1,z1,tuning.bin_metres);
        std::set<Key> wanted;
        auto select=[&](PyObject * bins,std::map<Cell,std::set<Key>> & out){mapping(bins,[&](PyObject * k,PyObject * v){const auto c=cell(k);
            if(c.first<lower.first || c.first>upper.first || c.second<lower.second || c.second>upper.second)return;
            auto found=out.find(c);if(found==out.end())found=out.emplace(c,keys(v)).first;
            wanted.insert(found->second.begin(),found->second.end());});};
        select(field(sensor.get(),"g_offh_destr_contact_bins"),view.contact_bins);
        auto catalog=field(sensor.get(),"_destructible_catalog");
        if(catalog && catalog!=Py_None)select(field(catalog,"baked_shot_bins"),view.baked_shot_bins);
        for(const auto & surface:surfaces)wanted.insert(surface.key());
        // Skin only reads these candidates and bin members. Keep full identity
        // membership in the row indexes, without allocating empty instances for
        // every unrelated catalog key. Presence in the sparse maps is also the
        // loaded marker; all candidate surfaces are included before motionSkin.
        for(const auto & id:wanted){auto live=instanceRows.find(id);if(live!=instanceRows.end() && !view.instances.count(id))view.instances.emplace(id,instance(live->second));
            auto baked=bakedRows.find(id);if(baked!=bakedRows.end() && !view.baked_instances.count(id))view.baked_instances.emplace(id,instance(baked->second));}
        if(diagnostic.get()!=Py_None)++counters["native_query_skin"];
    }
    Filter parseFilter(PyObject * object) {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        Filter out;out.owner=Ref::borrow(object);
        if(object==Py_None)return out;
        Ref tag(PyObject_GetAttrString(object,"_native_query_filter"));
        if(!tag.get()) {if(PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();throw Unsupported();}throw Error();}
        if(!PyTuple_CheckExact(tag.get()) || !PyFunction_Check(object))throw Unsupported("custom_surface_filter");
        const auto mode=number(at(tag.get(),0));
        bool canonical=false;auto registry=at(binding.get(),8);
        for(Py_ssize_t i=0;i<size(registry);++i){auto row=tuple(at(registry,i),2);if(number(at(row,0))==mode && at(row,1)==PyFunction_GET_CODE(object)){canonical=true;break;}}
        if(!canonical)throw Unsupported("custom_surface_filter_code");
        auto captured=[&](const char * name,PyObject * expected){
            auto code=reinterpret_cast<PyCodeObject *>(PyFunction_GET_CODE(object));
            auto closure=PyFunction_GET_CLOSURE(object);if(!closure)throw Unsupported("missing_filter_closure");
            for(Py_ssize_t i=0;i<PyTuple_GET_SIZE(code->co_freevars);++i){auto fieldName=PyTuple_GET_ITEM(code->co_freevars,i);if(PyString_Check(fieldName) && std::string(PyString_AS_STRING(fieldName))==name){if(PyCell_GET(PyTuple_GET_ITEM(closure,i))!=expected)throw Unsupported("changed_filter_capture");return;}}
            throw Unsupported("missing_filter_capture");
        };
        if(size(tag.get())==3 && mode==2){
            captured("collision_filter",at(tag.get(),1));captured("candidates",at(tag.get(),2));
            out=parseFilter(at(tag.get(),1));out.owner=Ref::borrow(object);out.trace=Ref::borrow(at(tag.get(),2));
            if(!PyList_CheckExact(out.trace.get()))throw Unsupported();
            for(Py_ssize_t i=0;i<PyList_GET_SIZE(out.trace.get());++i){auto row=tuple(PyList_GET_ITEM(out.trace.get(),i),5);
                const Surface surface={u32(at(row,0)),u32(at(row,1)),u32(at(row,2)),u32(at(row,3))};
                if(at(row,4)!=Py_True && at(row,4)!=Py_False)throw Unsupported();
                out.traceRows.emplace_back(surface,at(row,4)==Py_True);
            }
            return out;
        }
        tuple(tag.get(),5);if(mode!=0 && mode!=1)throw Unsupported();out.frozen=mode==1;
        captured(out.frozen?"broken":"member_ids",at(tag.get(),1));captured("accepted_trees",at(tag.get(),2));
        if(!out.frozen){captured("authority",at(tag.get(),3));captured("keep_native_surface",at(tag.get(),4));}
        if(!out.frozen){
            auto diagnostic=at(tag.get(),4);bool canonicalDiagnostic=false;
            auto codes=field(sensor.get(),"_native_query_keep_codes");
            if(PyFunction_Check(diagnostic) && PyFunction_GET_GLOBALS(diagnostic)==sensor.get() && codes && PyTuple_CheckExact(codes))
                for(Py_ssize_t i=0;i<size(codes);++i)if(at(codes,i)==PyFunction_GET_CODE(diagnostic))canonicalDiagnostic=true;
            if(!canonicalDiagnostic)throw Unsupported("custom_keep_native_surface");
        }
        if(out.frozen)out.broken=materials(at(tag.get(),1));else out.members=keys(at(tag.get(),1));
        out.treesObject=Ref::borrow(at(tag.get(),2));out.trees=keys(out.treesObject.get());
        if(!out.frozen && at(tag.get(),3)!=Py_None){auto module=attr(at(tag.get(),3),"__dict__");if(module.get()!=authority.get())throw Unsupported();}
        out.diagnostic=Ref::borrow(at(tag.get(),4));out.enabled=true;return out;
    }
    Filter & getFilter(PyObject * object) {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        auto found=filters.find(object);if(found!=filters.end())return found->second;
        Filter value=parseFilter(object);return filters.emplace(object,std::move(value)).first->second;
    }
    GroundKey groundKey(const Cell & c) {
        const bool unknown=spatialRevision!=publishedRevision;
        const auto found=cellGenerations.find(c);
        return GroundKey{groundSpace,unknown?spatialRevision:0,unknown?0:globalGeneration,
            unknown?0:(found==cellGenerations.end()?0:found->second),
            groundTrees.get()?static_cast<std::int64_t>(PySet_Size(groundTrees.get())):0,
            c,reinterpret_cast<std::uintptr_t>(groundTrees.get()),unknown};
    }
    void prepareGround() {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        if(groundReady)return;
        groundMembers.clear();cellGenerations.clear();dirtyCells.clear();
        mapping(field(sensor.get(),"g_offh_destr_contact_bins"),[&](PyObject * k,PyObject * v){groundMembers.emplace(cell(k),keys(v));});
        mapping(field(sensor.get(),"g_offh_destr_spatial_cell_generations"),[&](PyObject * k,PyObject * v){cellGenerations.emplace(cell(k),number(v));});
        auto getNumber=[&](const char * name,std::int64_t fallback){auto value=field(sensor.get(),name);return value?number(value):fallback;};
        groundSpace=getNumber("g_offh_destr_runtime_space",0);spatialRevision=getNumber("g_offh_destr_spatial_revision",0);
        publishedRevision=getNumber("g_offh_destr_spatial_published_revision",-1);globalGeneration=getNumber("g_offh_destr_spatial_global_generation",0);
        auto batch=field(sensor.get(),"g_offh_destr_spatial_mutation_batch");globalDirty=false;
        if(batch){auto global=field(batch,"global");if(global){if(global!=Py_True && global!=Py_False && !integer(global))throw Unsupported();globalDirty=global==Py_True || (integer(global)&&number(global)!=0);}
            each(field(batch,"bin_keys"),[&](PyObject * k){dirtyCells.insert(cell(k));});}
        auto state=field(sensor.get(),"g_offh_tree_state");PyObject * trees=NULL;
        if(state && PyDict_CheckExact(state)){trees=field(state,"native_committed");if(!trees)trees=field(state,"felled");}
        groundTrees=Ref();if(trees && (Py_TYPE(trees)==&PySet_Type || PyFrozenSet_CheckExact(trees)))groundTrees=Ref::borrow(trees);
        // Seed the native cache from the factory's existing Python cache.
        // A matching key must retain its already captured members even if an
        // unrelated writer replaced a bin without publishing a new revision.
        auto cache=at(groundBinding.get(),1);
        mapping(cache,[&](PyObject * keyObject,PyObject * filterObject){
            tuple(keyObject,5);auto revision=tuple(at(keyObject,1),2);const Cell c=cell(at(keyObject,2));
            GroundKey cacheKey{};cacheKey.space=number(at(keyObject,0));cacheKey.cell=c;
            cacheKey.trees=static_cast<std::uintptr_t>(number(at(keyObject,3)));cacheKey.treesLength=number(at(keyObject,4));
            if(PyString_CheckExact(at(revision,0))){if(std::string(PyString_AS_STRING(at(revision,0)))!="global_revision")throw Unsupported();cacheKey.globalRevision=true;cacheKey.revision=number(at(revision,1));}
            else {cacheKey.globalRevision=false;cacheKey.global=number(at(revision,0));auto cells=tuple(at(revision,1),1);auto row=tuple(at(cells,0),2);if(cell(at(row,0))!=c)throw Unsupported();cacheKey.local=number(at(row,1));}
            if(groundFilters.count(cacheKey)==0)groundFilters.emplace(cacheKey,parseFilter(filterObject));
        });
        // Existing sweep-local closures retain their original member set;
        // accepted trees remain a captured, live object even at equal length.
        for(auto & cached:groundFilters)if(cached.second.treesObject.get())cached.second.trees=keys(cached.second.treesObject.get());
        groundReady=true;
    }
    Filter & getGround(const double * start) {
        timing::Scope timingScope(timing::Stage::FilterSkin);
        prepareGround();const auto c=bin(start[0],start[2],tuning.bin_metres);const auto cacheKey=groundKey(c);
        const bool dirty=globalDirty || dirtyCells.count(c)!=0;
        if(dirty && diagnostic.get()!=Py_None)++counters["destructible_ground_filter_batch_dirty"];
        auto found=groundFilters.find(cacheKey);
        if(!dirty && found!=groundFilters.end()){if(diagnostic.get()!=Py_None)++counters["destructible_ground_filter_batch_hits"];return found->second;}
        if(diagnostic.get()!=Py_None)++counters["destructible_ground_filter_batch_misses"];
        Filter value;const auto members=groundMembers.find(c);if(members!=groundMembers.end())value.members=members->second;
        value.treesObject=groundTrees;if(groundTrees.get())value.trees=keys(groundTrees.get());
        value.enabled=!value.members.empty() || !value.trees.empty();
        if(dirty){groundScratch=std::move(value);return groundScratch;}
        if(groundFilters.size()>=64)groundFilters.clear();
        return groundFilters.emplace(cacheKey,std::move(value)).first->second;
    }
    bool keep(Filter & filter,const Surface & surface,const std::set<Surface> & excluded,std::vector<Surface> & candidates,bool recast) {
        const auto identity=surface.key();bool accepted=true;
        if(filter.enabled && !(surface.material>=87 && surface.material<=100)){
            if(filter.frozen){accepted=filter.trees.count(identity)==0 && filter.broken.count(MaterialKey(identity.first,identity.second,surface.material))==0 && filter.broken.count(MaterialKey(identity.first,identity.second,-1))==0;
                if(!accepted && filter.broken.count(MaterialKey(identity.first,identity.second,-1)) && !(surface.material>=71 && surface.material<=86) && replacements.count(identity))accepted=true;
            }else if(filter.trees.count(identity))accepted=false;
            else if(filter.members.count(identity)){
                const bool whole=filterBroken.count(MaterialKey(identity.first,identity.second,-1))!=0;
                accepted=!whole && filterBroken.count(MaterialKey(identity.first,identity.second,surface.material))==0 && view.speculative.count(MaterialKey(identity.first,identity.second,surface.material))==0 && view.speculative.count(MaterialKey(identity.first,identity.second,-1))==0;
                if(whole && !(surface.material>=71 && surface.material<=86) && replacements.count(identity))accepted=true;
            }
        }
        if(!accepted)++skips;

        if(filter.trace.get()){
            bool duplicate=false;
            for(const auto & row:filter.traceRows)if(row.first==surface && row.second==accepted){duplicate=true;break;}
            for(const auto & row:traces)if(row.list.get()==filter.trace.get() && row.surface==surface && row.keep==accepted){duplicate=true;break;}
            if(!duplicate){std::size_t count=0;for(const auto & row:traces)if(row.list.get()==filter.trace.get())++count;
                if(count+filter.traceRows.size()<16)traces.push_back({filter.trace,surface,accepted});}
        }
        if(!recast)return accepted;
        // Alias classification is evaluated even for a rejected live surface.
        bool alias=false;
        if(surface.material>=71 && surface.material<=86 && (surface.flags&0x80)){
            identities();alias=!instanceRows.count(identity) && !bakedRows.count(identity) && !view.tree_instances.count(identity);
        }
        bool result=accepted && !excluded.count(surface);
        if(result && alias)for(const auto & other:excluded)if(other.material==surface.material && other.flags==surface.flags){result=false;break;}
        if(result && candidates.size()<16 && std::find(candidates.begin(),candidates.end(),surface)==candidates.end())candidates.push_back(surface);
        return result;
    }
    bool remember(bool groundQuery) {
        timing::Scope timingScope(timing::Stage::Business);
        if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,"Native engine query failed");
        const bool recovered=groundQuery && (PyErr_ExceptionMatches(PyExc_AttributeError)||PyErr_ExceptionMatches(PyExc_IndexError)||PyErr_ExceptionMatches(PyExc_TypeError)||PyErr_ExceptionMatches(PyExc_ValueError));
        if(recovered){PyErr_Clear();return true;}
        PyObject * type=NULL,* value=NULL,* tb=NULL;PyErr_Fetch(&type,&value,&tb);PyErr_NormalizeException(&type,&value,&tb);Ref t(type),v(value),b(tb);
        if(!value){PyErr_NoMemory();return false;}
        auto current=attr(query.get(),"_error");if(current.get()==Py_None && PyObject_SetAttrString(query.get(),"_error",value)<0)return false;
        return false;
    }
    Result collide(Filter & filter,bool groundQuery,const double * a,const double * b,Hit & output) {
        struct Segment{BW::Vector3 start,end;std::set<Surface> excluded;};
        std::vector<Segment> segments;segments.push_back({vector(a),vector(b),{}});unsigned remaining=budget;
        const bool canRecast=field(sensor.get(),"_destructible_catalog")!=Py_None && field(sensor.get(),"_destructible_catalog")!=NULL;
        while(!segments.empty()){
            Segment segment=std::move(segments.back());segments.pop_back();std::vector<Surface> candidates;
            BW::StockNativeSegmentFilter callback;
            std::exception_ptr callbackError;
            if(filter.enabled)callback=[&](BW::uint32 mat,BW::uint32 flags,BW::uint64 id){
                try{const Surface s={mat,flags,static_cast<std::uint32_t>((id>>32)&0xffff),static_cast<std::uint32_t>(id)};return keep(filter,s,segment.excluded,candidates,canRecast);}
                catch(...){if(!callbackError)callbackError=std::current_exception();return false;}};
            BW::StockSegmentHit found;live();
            const auto before=diagnostic.get()!=Py_None?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point();
            const auto result=BW::collideStockSegmentFiltered(static_cast<BW::SpaceID>(space),segment.start,segment.end,static_cast<BW::WorldTriangle::Flags>(skip),callback,found);
            if(diagnostic.get()!=Py_None){const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count();rays.push_back({groundQuery,point(segment.start),point(segment.end),elapsed});++counters["native_query_ray"];}
            live();if(callbackError)std::rethrow_exception(callbackError);
            if(result==BW::STOCK_SEGMENT_NO_SPACE){timing::Scope callbackTiming(timing::Stage::Business);PyErr_Format(PyExc_ValueError,"No space ID %d",static_cast<int>(space));PyErr_PrintEx(0);continue;}
            if(result==BW::STOCK_SEGMENT_FAILED)throw Error();
            if(result!=BW::STOCK_SEGMENT_HIT)continue;
            SkinResult skin;
            if(filter.enabled && canRecast){bool relevant=false;for(const auto & s:candidates)if(s.material>=71 && s.material<=86 && (s.flags==0 || (s.flags&0x80))){relevant=true;break;}
                if(relevant){timing::Scope skinTiming(timing::Stage::FilterSkin);geometry(point(found.point),point(segment.start),point(segment.end),candidates);const auto normal=point(found.normal);skin=motionSkin(view,point(found.point),point(segment.start),point(segment.end),&normal,candidates,tuning,SkinMath{NULL,distance,bound});}}
            if(!skin.found || !remaining){copy(found.point,output.point);copy(found.normal,output.normal);copy(found.translation,output.translation);output.material=found.materialKind;output.item=found.itemIndex;output.chunk=found.chunkID;return Found;}
            BW::Vector3 direction=segment.end-segment.start;const double length=direction.length();
            if(length<=epsilon){copy(found.point,output.point);copy(found.normal,output.normal);copy(found.translation,output.translation);output.material=found.materialKind;output.item=found.itemIndex;output.chunk=found.chunkID;return Found;}
            direction.normalise();BW::Vector3 bounded=segment.end;
            if(skin.distance<length)bounded=segment.start+direction*static_cast<float>(skin.distance);
            --remaining;
            if(skin.distance+epsilon<length)segments.push_back({bounded+direction*static_cast<float>(epsilon),segment.end,segment.excluded});
            segment.excluded.insert(skin.excluded.begin(),skin.excluded.end());segments.push_back({segment.start,bounded,std::move(segment.excluded)});++skips;
        }
        return Miss;
    }
    bool flush() {
        timing::Scope timingScope(timing::Stage::Marshal);
        if(!skips && kept.empty() && traces.empty() && rays.empty() && counters.empty())return true;
        try{
            Ref keptRows(required(PyTuple_New(static_cast<Py_ssize_t>(kept.size()))));
            for(std::size_t i=0;i<kept.size();++i){Ref row(required(PyTuple_New(2)));PyTuple_SET_ITEM(row.get(),0,Ref::borrow(kept[i].callback.get()).release());PyTuple_SET_ITEM(row.get(),1,surfaceTuple(kept[i].surface).release());PyTuple_SET_ITEM(keptRows.get(),static_cast<Py_ssize_t>(i),row.release());}
            Ref traceRows(required(PyTuple_New(static_cast<Py_ssize_t>(traces.size()))));
            for(std::size_t i=0;i<traces.size();++i){Ref row(required(PyTuple_New(2)));PyTuple_SET_ITEM(row.get(),0,Ref::borrow(traces[i].list.get()).release());PyTuple_SET_ITEM(row.get(),1,surfaceTuple(traces[i].surface,true,traces[i].keep).release());PyTuple_SET_ITEM(traceRows.get(),static_cast<Py_ssize_t>(i),row.release());}
            Ref rayRows(required(PyTuple_New(static_cast<Py_ssize_t>(rays.size()))));
            for(std::size_t i=0;i<rays.size();++i){auto & event=rays[i];Ref row(required(PyTuple_New(5)));auto label=event.ground?Ref(required(PyString_FromString("native.motion.ground"))):attr(query.get(),"_ray_label");PyTuple_SET_ITEM(row.get(),0,label.release());PyTuple_SET_ITEM(row.get(),1,Ref::borrow(spaceObject.get()).release());
                for(unsigned j=0;j<2;++j){const auto & p=j?event.end:event.start;PyTuple_SET_ITEM(row.get(),j+2,required(Py_BuildValue("(ddd)",p[0],p[1],p[2])));}PyTuple_SET_ITEM(row.get(),4,required(PyFloat_FromDouble(event.seconds)));PyTuple_SET_ITEM(rayRows.get(),static_cast<Py_ssize_t>(i),row.release());}
            Ref countRows(required(PyTuple_New(static_cast<Py_ssize_t>(counters.size()))));std::size_t index=0;
            for(const auto & c:counters){PyTuple_SET_ITEM(countRows.get(),static_cast<Py_ssize_t>(index++),required(Py_BuildValue("(sK)",c.first.c_str(),c.second)));}
            Ref args(required(PyTuple_New(6)));PyTuple_SET_ITEM(args.get(),0,Ref::borrow(query.get()).release());PyTuple_SET_ITEM(args.get(),1,required(PyLong_FromUnsignedLongLong(skips)));PyTuple_SET_ITEM(args.get(),2,keptRows.release());PyTuple_SET_ITEM(args.get(),3,traceRows.release());PyTuple_SET_ITEM(args.get(),4,rayRows.release());PyTuple_SET_ITEM(args.get(),5,countRows.release());
            skips=0;kept.clear();traces.clear();rays.clear();counters.clear();
            Ref result;
            {
                timing::Scope callbackTiming(timing::Stage::Business);
                timing::count(timing::Counter::CallbackFlush);
                result=Ref(PyObject_CallObject(at(binding.get(),6),args.get()));
            }
            if(!result.get())throw Error();live();return true;
        }catch(...){remember(false);return false;}
    }
};

Context * begin(PyObject * capabilities) {
    timing::Scope timingScope(timing::Stage::ContextSetup);
    timing::count(timing::Counter::ContextAttempt);
    PyObject * query=NULL;
    try{
        tuple(capabilities,21);auto method=at(capabilities,7);if(!PyMethod_Check(method)){timing::count(timing::Counter::ContextUnbound);return NULL;}
        query=PyMethod_GET_SELF(method);if(!query){timing::count(timing::Counter::ContextUnbound);return NULL;}
        auto binding=attr(query,"_native_query_binding");if(binding.get()==Py_None){status(query,"generic","custom_binding");timing::count(timing::Counter::ContextUnbound);return NULL;}tuple(binding.get(),9);
        std::unique_ptr<Context> result(new Context(capabilities,query,binding.get()));
        status(query,"native","canonical");timing::count(timing::Counter::ContextAccepted);return result.release();
    }catch(const Unsupported & error){PyErr_Clear();if(query)status(query,"generic",error.reason);timing::count(timing::Counter::ContextUnbound);return NULL;}
    catch(...){PyErr_Clear();if(query)status(query,"generic","binding_unavailable");timing::count(timing::Counter::ContextUnbound);return NULL;}
}
void end(Context * c) {timing::Scope timingScope(timing::Stage::ContextTeardown);if(c){c->flush();delete c;}}
bool beforeEffect(Context * c) {return !c || c->flush();}
bool afterEffect(Context * c) {
    if(!c)return true;
    try{c->live();c->validate();c->viewReady=false;c->skinReady=false;c->groundReady=false;c->filters.clear();return true;}
    catch(const Unsupported & error){c->usable=false;status(c->query.get(),"generic",error.reason);return true;}
    catch(...){c->remember(false);return false;}
}
bool afterDeparture(Context * c) {
    if(!c)return true;
    if(!afterEffect(c))return false;
    if(!c->usable) {
        PyErr_SetString(PyExc_RuntimeError,"Native rotation binding changed after departure; no replay is permitted");
        c->remember(false);return false;
    }
    // beforeEffect committed and cleared the earlier observations.
    c->markRay();return true;
}
bool groundAvailable(Context * c) {return c && c->usable && c->ground;}
void beginRay(Context * c) {if(c){c->markRay();c->outerRay=true;}}
void finishRay(Context * c,bool committed) {if(c){if(!committed)c->restoreRay();c->outerRay=false;}}
void rounded(const double * p,double * out) {copy(vector(p),out);}
void departureStep(const double * end,const double * hit,double epsilon,double * out,double & length) {
    auto remaining=vector(end)-vector(hit);length=remaining.length();
    if(length>epsilon)copy(vector(hit)+remaining*static_cast<float>(epsilon/length),out);
}
Result ray(Context * c,PyObject * filter,bool groundQuery,const double * start,const double * endPoint,Hit & hit) {
    timing::count(timing::Counter::NativeRay);
    if(!c || !c->usable)return countedRayResult(Unbound);
    if(!c->outerRay)c->markRay();
    try{
        c->live();c->ledger();Filter * selected;
        if(!filter){if(!c->ground)return countedRayResult(Unbound);if(c->diagnostic.get()!=Py_None)++c->counters["destructible_ground_filter_queries"];selected=&c->getGround(start);}
        else selected=&c->getFilter(filter);
        return countedRayResult(c->collide(*selected,groundQuery,start,endPoint,hit));
    }catch(const Unsupported & error){
        // A typed traversal cannot call Python or perform gameplay effects.
        // Discard its staged observations and retry only this query through
        // the generic capabilities. Earlier effect operations are not replayed.
        c->restoreRay();c->usable=false;status(c->query.get(),"generic",error.reason);return countedRayResult(Unbound);
    }catch(const std::bad_alloc &){PyErr_NoMemory();return countedRayResult(c->remember(groundQuery)?Miss:Failed);}
    catch(...){return countedRayResult(c->remember(groundQuery)?Miss:Failed);}
}
PyObject * materialize(const Hit & hit) {
    timing::Scope timingScope(timing::Stage::Marshal);
    timing::count(timing::Counter::Materialize);
    try{
        Ref result(required(PyTuple_New(6)));
        PyTuple_SET_ITEM(result.get(),0,required(BW::Script::getData(vector(hit.point))));
        PyTuple_SET_ITEM(result.get(),1,required(BW::Script::getData(vector(hit.normal))));
        PyTuple_SET_ITEM(result.get(),2,required(uintObject(hit.material)));
        PyTuple_SET_ITEM(result.get(),3,required(BW::Script::getData(vector(hit.translation))));
        PyTuple_SET_ITEM(result.get(),4,required(uintObject(hit.item)));
        PyTuple_SET_ITEM(result.get(),5,required(uintObject(hit.chunk)));
        return result.release();
    }catch(...){if(!PyErr_Occurred())PyErr_NoMemory();return NULL;}
}
}
