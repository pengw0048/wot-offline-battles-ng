#include "stock_native_world_events.hpp"
#include "stock_native_bot_python.hpp"
#include <cassert>
using namespace stock_native_world;
namespace p=stock_native_bot_python;
namespace d=stock_native_destructible;
namespace {
PyObject*cancelled=nullptr;
PyObject*cancel(PyObject*,PyObject*args){try{auto area=p::Ref(PyImport_ImportModule("AreaDestructibles"));auto animator=p::value(area.get(),"g_destructiblesAnimator");assert(!p::truth(animator.get(),"_DestructiblesAnimator__isUpdating",true));if(PyList_Append(cancelled,args)<0)return nullptr;Py_RETURN_NONE;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
struct Backend:d::Backend {
    bool loaded=true;int effects=0,geometry_calls=0,replay_calls=0;std::uint64_t incarnation=1;
    bool current()const override{return true;}
    d::SnapshotStatus snapshot(const Destruction&,d::Parameters&p)override{p.scale=1.;p.height=6.;p.mass=100.;p.stiffness=300.;p.spring_angle=.2;p.spring_resist=50.;p.air_resist=2.;return loaded?d::SnapshotStatus::Ready:d::SnapshotStatus::Missing;}
    bool fallPitch(const Destruction&,double,double&p,std::uint32_t&f)override{p=1.5707963267948966;f=0;return true;}
    bool position(const d::Body&,double)override{return true;}
    bool geometry(const Destruction&,const d::ChildEffect&)override{++geometry_calls;return true;}
    bool present(const Destruction&,int,d::PresentationPhase)override{++effects;return true;}
    std::uint64_t generation(const Destruction&)const override{return incarnation;}
    bool replay(const Destruction&,const d::Body*)override{++replay_calls;return true;}
};
Instance instance(Kind kind,double x){Instance v;v.kind=kind;v.admission=Admission::Admitted;v.descriptor_valid=true;v.native_name_exact=true;v.filename="tree.spt";v.tree_health=10.;v.item_scale=1.;v.origin={{x,0.,0.}};Box b;b.center={{x,1.,0.}};b.axes={{{1.,0.,0.}},{{0.,1.,0.}},{{0.,0.,1.}}};v.boxes={b};v.scaled_health={{-1,1.}};return v;}
struct SceneFixture:Scene {
    Backend& backend;d::Runtime& runtime;std::uint64_t revision=1;std::map<Key,Instance> rows;
    SceneFixture(Backend&b,d::Runtime&r):backend(b),runtime(r){}
    QueryStatus segment(const Point&,const Point&,std::uint16_t,const Filter&,Hit&)override{return QueryStatus::Miss;}
    QueryStatus material(const Point&,const Point&,const Point&,MaterialHit&)override{return QueryStatus::Miss;}
    QueryStatus water(const Point&,const Point&,double&)override{return QueryStatus::Miss;}
    Admission refresh(const Key&k,Registry&r)override{const auto found=rows.find(k);if(!backend.loaded||found==rows.end())return Admission::Pending;r.put(k,found->second);return Admission::Admitted;}
    bool unresolved(const Key&,std::vector<Box>&)override{return false;}
    bool destroy(const Destruction&e)override{return runtime.destroy(e);}
    bool treeChunks(const std::vector<Box>&,std::vector<std::uint32_t>&)override{return false;}
    bool turretSweep(const TurretSweep&)override{return true;}
    TreeIdentity trustedTree(const Key&k)override{const auto found=rows.find(k);return !backend.loaded?TreeIdentity::Pending:found!=rows.end()&&found->second.kind==Kind::Tree?TreeIdentity::Tree:TreeIdentity::Other;}
    std::uint64_t streamRevision()const override{return revision;}
};
p::Ref canonical(const char*kind,int item){return p::Ref(Py_BuildValue("{s:s,s:i,s:i,s:d,s:d,s:d,s:d,s:d,s:O}","destructible_kind",kind,"chunk_id",1,"item_index",item,"x",2.,"y",1.,"z",3.,"fall_yaw",.7,"speed",2.,"is_shot",Py_False));}
PyObject* run(PyObject*,PyObject*battle){
    try {
        Registry registry;Backend backend;d::Runtime native(backend);SceneFixture scene(backend,native);WorldEvents events(registry,native,scene,41);if(!events.install(battle))return nullptr;
        Pipeline pipeline(registry,scene,0);ActorInputs actor;actor.mass=1000.;actor.speed=20.;actor.now=1.;pipeline.actor(actor);registry.unit_vehicle_mass=1000.;registry.catalog_available=true;
        scene.rows[{1,7}]=instance(Kind::Fragile,0.);scene.refresh({1,7},registry);assert(pipeline.commit({1,7,-1},{{0.,1.,0.}},20.));
        p::Ref first(events.pack(battle));assert(PyList_Size(first.get())==1);p::Ref duplicate(events.pack(battle));assert(PyObject_RichCompareBool(first.get(),duplicate.get(),Py_EQ)==1);assert(!events.ack(2)&&events.ack(1));assert(registry.journal.empty());
        backend.loaded=false;scene.rows[{1,8}]=instance(Kind::Fragile,2.);registry.putBaked({1,8},scene.rows.at({1,8}));auto queued=canonical("fragile",8);assert(events.ingress(queued.get(),2.));assert(native.waiting(1));
        scene.rows[{1,9}]=instance(Kind::Tree,3.);auto logical=canonical("tree",9);const auto count=native.counters().accepted;assert(events.ingress(logical.get(),2.));assert(native.counters().accepted==count);
        backend.loaded=true;++scene.revision;assert(events.mappingChanged(3.));assert(!native.waiting(1)&&native.counters().accepted==count);
        scene.rows[{1,10}]=instance(Kind::Tree,4.);const std::set<MaterialKey> token{{1,10,-1}};
        auto result=events.commitTrustedTrees(token,{{0.,0.,0.}},{{0.,0.,1.}},0.,2.,4.);assert(result.status==ContactStatus::Pending&&!registry.destroyed({1,10,-1}));
        p::Ref trusted(events.pack(battle));assert(PyList_Size(trusted.get())==1);result=events.commitTrustedTrees(token,{{99.,0.,0.}},{{99.,0.,0.}},1.,8.,4.1);p::Ref frozen(events.pack(battle));assert(PyObject_RichCompareBool(trusted.get(),frozen.get(),Py_EQ)==1);assert(events.ack(2));
        assert(events.commitTrustedTrees(token,{},{},0.,1.,4.2).status==ContactStatus::Crushed);auto pair=p::item(trusted.get(),0),echo=p::item(pair.get(),1);const auto before=native.counters().accepted;assert(events.ingress(echo.get(),4.3)&&native.counters().accepted==before+1);const int effects=backend.effects;assert(events.ingress(echo.get(),4.4)&&backend.effects==effects);
        scene.rows[{1,11}]=instance(Kind::Falling,0.);scene.refresh({1,11},registry);assert(pipeline.commit({1,11,-1},{{0.,1.,0.}},2.));
        auto sensor=p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.destructibles_sensor"));auto dict=p::retain(PyModule_GetDict(sensor.get()));auto instances=p::dict(),raw=p::dict();p::set(raw.get(),"kind",p::text("falling"));p::Ref key(Py_BuildValue("(ii)",1,11));if(PyDict_SetItem(instances.get(),key.get(),raw.get())<0)return nullptr;p::set(dict.get(),"g_offh_destr_instances",instances);
        registry.instances.at({1,11}).boxes[0].center[0]=16.;assert(events.synchronizeGeometry());auto boxrows=p::value(raw.get(),"boxes"),box=p::item(boxrows.get(),0),center=p::item(box.get(),0);assert(p::numbers<3>(center.get())[0]==16.);p::Ref falling(events.pack(battle));
        const int replay_before=backend.replay_calls,resource_before=backend.effects;++backend.incarnation;++scene.revision;assert(events.mappingChanged(5.));assert(backend.replay_calls>replay_before&&backend.effects==resource_before);assert(events.mappingChanged(5.1)&&backend.effects==resource_before);
        p::Ref no_x(PyDict_Copy(queued.get()));PyDict_DelItemString(no_x.get(),"x");assert(!events.ingress(no_x.get(),6.)&&PyErr_Occurred());PyErr_Clear();assert(events.ingress(queued.get(),6.));
        auto activated=events.takeActivatedTrees();assert(activated.count({1,9})&&activated.count({1,10}));return Py_BuildValue("(NNN)",first.release(),trusted.release(),falling.release());
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
}
PyObject* installOnly(PyObject*,PyObject*battle){Registry registry;Backend backend;d::Runtime native(backend);SceneFixture scene(backend,native);WorldEvents events(registry,native,scene,41);if(!events.install(battle))return nullptr;Py_RETURN_TRUE;}
PyMethodDef methods[]={{"run",run,METH_O,nullptr},{"install",installOnly,METH_O,nullptr},{"cancel",cancel,METH_VARARGS,nullptr},{nullptr,nullptr,0,nullptr}};
}
extern "C" void initworld_events_test(){auto*module=Py_InitModule("world_events_test",methods);cancelled=PyList_New(0);PyModule_AddObject(module,"cancelled",cancelled);}
