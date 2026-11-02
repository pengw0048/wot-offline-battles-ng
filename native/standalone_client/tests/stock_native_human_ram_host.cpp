#include "stock_native_human_ram.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_battle_subsystems.hpp"
#include <Python.h>
namespace h=stock_native_human_ram;
namespace p=stock_native_bot_python;
namespace cs=stock_native_client_state;
namespace b=native_battle;
namespace native_battle {
// This ingress-only host never allocates these unrelated battle subsystems.
struct VisibilityCore {};struct LocalCriticalOwner {};struct RemoteVehicle {};struct ReplicaBinding {};
struct FallenFoliage {};struct DestructibleCatalog {};class ReloadSound {};
Visibility::~Visibility()=default;Critical::~Critical()=default;Remote::~Remote()=default;
World::~World()=default;LocalGun::~LocalGun()=default;Projectiles::~Projectiles()=default;
Battle::Battle():vehicles(std::make_shared<cs::State>()){} Battle::~Battle()=default;
Battle* active=nullptr;PyObject* entities=nullptr;
Battle* current(){return active;}
Actor* Battle::vehicle(const std::string&key)const{return vehicles->find(key);}
namespace remote {
// Only the native entity lookup boundary is supplied by the host. The actual
// readNativeEntity/readNativeEntityField guards remain production code.
p::Ref get(const Battle&,std::int64_t id){
    auto key=p::integer(id);auto* entity=entities?PyDict_GetItem(entities,key.get()):nullptr;
    return p::retain(entity?entity:Py_None);
}
Overlay* overlay(PyObject*,bool){return nullptr;}
}
}
struct Fixture:h::World {
    int mode=0;unsigned queries=0;
    bool ready(h::ID)override{return mode!=1;}
    std::optional<h::Plates> armor(const h::Body&,const h::Body&,const h::XZ&)override{++queries;if(mode==2)return {};if(mode==3)throw std::runtime_error("Fixture local geometry failure");h::Plates result;result.armor={{40.,80.}};if(mode!=4)result.tracks[0]={"leftTrackHealth"};return result;}
};
cs::Value nativeAdapterResults(b::Battle&,int,std::vector<std::string>&,unsigned&);
PyObject* run(PyObject*,PyObject*args){
    PyObject* source;int mode;if(!PyArg_ParseTuple(args,"Oi",&source,&mode))return nullptr;
    try{
        b::Battle battle;battle.worker=p::flag(p::value(source,"_worker_mode").get());
        battle.bots=std::make_unique<b::BotAuthority>();
        battle.bots->last_snapshot=cs::read(p::value(source,"_last_snapshot").get());
        auto records=p::value(source,"_records"),factory=p::value(source,"_remote_factory");
        auto entities=p::value(factory.get(),"_vehicles");
        Py_ssize_t cursor=0;PyObject* key;PyObject* value;
        while(PyDict_Next(records.get(),&cursor,&key,&value)){
            auto actor=std::make_shared<cs::Actor>();actor->key=p::string(key);
            actor->kind=p::string(p::value(value,"kind").get());
            actor->id=p::wide(p::value(value,"network_id").get());
            actor->engine_id=p::wide(p::value(value,"engine_id").get());
            actor->ready=p::flag(p::value(value,"ready").get());
            actor->tombstone=p::flag(p::value(value,"tombstone").get());
            actor->state=cs::read(p::value(value,"state").get());
            battle.vehicles->actors.emplace(actor->key,std::move(actor));
        }
        struct Active {
            Active(b::Battle&battle,PyObject*entities){b::active=&battle;b::entities=entities;}
            ~Active(){b::active=nullptr;b::entities=nullptr;}
        } active(battle,entities.get());
        Fixture world;world.mode=mode;std::vector<std::string> errors;
        unsigned queries=0;
        const auto result=mode>=5?nativeAdapterResults(battle,mode,errors,queries):h::results(battle,world,&errors);
        p::Ref rows(cs::write(result));auto messages=p::strings(errors);
        return Py_BuildValue("(NNI)",rows.release(),messages.release(),mode>=5?queries:world.queries);
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}
}
struct Services {
    struct Record{bool record_ready=true,available=true;
        struct Target{struct Pose{std::optional<std::array<double,8>>hydraulic_body;}pose;}target;
    }record;
    unsigned queries=0;
    struct Armor{std::array<double,2>value;std::array<std::vector<std::string>,2>tracks;};
    const Record*find(offline_simulation::ActorKey){return &record;}
    std::optional<Armor>armor(const h::Body&a,const h::Body&b,const std::array<double,3>&normal){++queries;if(a.id!=1000001||b.id!=1000002||normal[0]!=-1.)throw std::runtime_error("Native human identity mapping failed");return Armor{{{40.,80.}},{}};}
};
struct Vehicles {struct Vehicle{bool ready=true,has_siege=false;}first,second;const Vehicle*find(offline_simulation::ActorKey key){return key.id==1?&first:&second;}};
cs::Value nativeAdapterResults(b::Battle&battle,int mode,std::vector<std::string>&errors,unsigned&queries){
    Services services;Vehicles vehicles;
    vehicles.first.has_siege=mode==5||mode==7||mode==9;
    vehicles.second.has_siege=mode==6||mode==7||mode==10;
    // A valid latest receipt deliberately agrees with the historical baseline
    // in modes 9/10; equality still cannot establish the missing sample identity.
    if(mode>=9)services.record.target.pose.hydraulic_body=std::array<double,8>{mode==10?.6:-.6,0.,0.,0.,0.,0.,0.,0.};
    h::NativeWorld<Services,Vehicles> world(services,vehicles);
    if(!world.ready(1)||!world.ready(2))throw std::runtime_error("Hydraulic query containment changed resource readiness");
    auto result=h::results(battle,world,&errors);queries=services.queries;return result;
}
PyObject* adapter(PyObject*,PyObject*){try{Services services;Vehicles vehicles;h::NativeWorld<Services,Vehicles>world(services,vehicles);if(!world.ready(1))throw std::runtime_error("Native human world readiness failed");services.record.record_ready=false;if(world.ready(1))throw std::runtime_error("Native human pending readiness failed");h::Body a,b;a.id=1;b.id=2;const auto result=world.armor(a,b,{{-1.,0.}});return Py_BuildValue("(dd)",result->armor[0],result->armor[1]);}catch(const std::exception&e){PyErr_SetString(PyExc_ValueError,e.what());return nullptr;}}
PyMethodDef methods[]={{"run",run,METH_VARARGS,nullptr},{"adapter",adapter,METH_NOARGS,nullptr},{nullptr,nullptr,0,nullptr}};
PyMODINIT_FUNC inithuman_ram_test(){Py_InitModule("human_ram_test",methods);}
