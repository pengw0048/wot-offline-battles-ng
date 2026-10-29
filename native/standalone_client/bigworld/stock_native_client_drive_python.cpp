#include "pch.hpp"
#include "stock_native_client_drive_python.hpp"
#include "stock_native_client_drive.hpp"
#include "stock_native_client_drive_geometry.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_destructible_backend.hpp"
#include "stock_native_world_snapshot.hpp"
#include "space/space_manager.hpp"
#include "pyscript/script.hpp"
#include <memory>
#include <algorithm>

namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
namespace world=stock_native_world;
const char*const capsule_name="offline_client_drive_native.owner";
void require(bool value){if(!value)throw p::Error("Native player drive installation failed");}
void attribute(PyObject*object,const char*name,const p::Ref&value){if(PyObject_SetAttrString(object,name,value.get())<0)throw p::Error("Native player drive mirror assignment failed");}
std::optional<Point> optionalPoint(PyObject*object,const char*name){auto v=p::value(object,name);return v.get()==Py_None?std::nullopt:std::optional<Point>(p::numbers<3>(v.get()));}
std::optional<std::array<double,2>> gradient(PyObject*object,const char*name){auto v=p::value(object,name);return v.get()==Py_None?std::nullopt:std::optional<std::array<double,2>>(p::numbers<2>(v.get()));}
std::optional<step::SupportPlane> plane(PyObject*object,const char*name){auto v=p::value(object,name);if(v.get()==Py_None)return {};
    step::SupportPlane out;out.gradient_x=p::numeric(v.get(),"gradient_x");out.gradient_z=p::numeric(v.get(),"gradient_z");
    out.center_x=p::numeric(v.get(),"center_x");out.center_y=p::numeric(v.get(),"center_y");out.center_z=p::numeric(v.get(),"center_z");
    auto n=p::value(v.get(),"normal");out.normal=p::numbers<3>(n.get());return out;}
std::vector<std::optional<step::GroundMemory>> memories(PyObject*object,const char*name){auto v=p::value(object,name);std::vector<std::optional<step::GroundMemory>> out;
    if(v.get()==Py_None)return out;for(Py_ssize_t i=0;i<p::size(v.get());++i){auto row=p::item(v.get(),i);if(row.get()==Py_None){out.push_back({});continue;}
        const auto size=p::size(row.get());if(size!=6&&size!=7)throw p::Error("Native player contact memory width is invalid");
        step::GroundMemory m;double*fields[]={&m.origin_x,&m.origin_z,&m.height,&m.last_x,&m.last_z,&m.travelled};
        for(unsigned j=0;j<6;++j){auto field=p::item(row.get(),j);*fields[j]=p::scalar(field.get());}
        if(size==7){auto flag=p::item(row.get(),6);m.last_query_missed=p::flag(flag.get());}out.push_back(m);}return out;}
p::Ref memoryValue(const std::vector<std::optional<step::GroundMemory>>&rows){auto out=p::list();for(const auto&row:rows){if(!row){p::append(out.get(),p::retain(Py_None));continue;}
    p::Ref value(Py_BuildValue("(ddddddO)",row->origin_x,row->origin_z,row->height,row->last_x,row->last_z,row->travelled,row->last_query_missed?Py_True:Py_False));p::append(out.get(),value);}return out;}
step::SuspensionDescriptor descriptor(PyObject*params){step::SuspensionDescriptor out;
    const char*names[]={"mass","pitch_inertia","roll_inertia","fixed_step","constraint_iterations"};
    for(unsigned i=0;i<5;++i)out.parameters[i]=p::numeric(params,names[i]);
    p::Ref physics(PyImport_ImportModule("gui.mods.offline_lan_0922.vehicle_physics"));
    const char*tuning[]={"GRAVITY","CONTACT_PENETRATION","AIRBORNE_ANGULAR_DAMPING","AIRBORNE_ANGULAR_SPEED_LIMIT","FREEZE_ACCEL_EPSILON","FREEZE_VEL_EPSILON","FREEZE_ANG_ACCEL_EPSILON","FREEZE_ANG_VEL_EPSILON","SERVER_PHYSICS_MAX_SUBSTEPS"};
    for(unsigned i=0;i<9;++i)out.tuning[i]=p::numeric(physics.get(),tuning[i]);out.maximum_steps=int(out.tuning[8]);
    out.width=p::numeric(params,"width");out.clearance=p::numeric(params,"clearance");out.rest_length=p::numeric(params,"rest_length");
    out.contact_memory_distance=p::numeric(params,"contact_memory_distance");out.footprint_half_width=p::numeric(params,"footprint_half_width");
    auto read=[&](const char*name,std::vector<step::SuspensionContact>&rows,bool spring){auto raw=p::value(params,name);if(raw.get()==Py_None)return;
        for(Py_ssize_t i=0;i<p::size(raw.get());++i){auto v=p::item(raw.get(),i);step::SuspensionContact c;
            c.x=p::numeric(v.get(),"x");c.y=p::numeric(v.get(),"y");c.z=p::numeric(v.get(),"z");auto side=p::value(v.get(),"side"),kind=p::value(v.get(),"kind");
            const auto text=p::string(side.get());c.side=text=="left"?-1:text=="right"?1:0;c.spring=spring;c.rigid=p::string(kind.get())=="rigid";c.track=p::string(kind.get())=="track";
            if(spring){c.stiffness=p::numeric(v.get(),"stiffness");c.damping=p::numeric(v.get(),"damping");c.compression=p::numeric(v.get(),"static_compression");
                c.maximum=p::numeric(v.get(),"max_compression");c.force=p::numeric(v.get(),"max_force");c.rest_length=p::numeric(v.get(),"rest_length");
                const double footprint=p::numeric(params,"footprint_half_length",.3);c.footprint_front=p::numeric(v.get(),"footprint_front",footprint);c.footprint_rear=p::numeric(v.get(),"footprint_rear",footprint);
            }else c.penetration=p::numeric(v.get(),"penetration",.1);rows.push_back(c);}};
    read("springs",out.springs,true);read("pseudo_contacts",out.pseudo,false);read("rigid_contacts",out.rigid,false);
    out.turret_origin=optionalPoint(params,"turret_origin");auto turrets=p::value(params,"turret_contacts");if(turrets.get()!=Py_None)for(Py_ssize_t i=0;i<p::size(turrets.get());++i){auto v=p::item(turrets.get(),i);out.turret_contacts.push_back(p::numbers<3>(v.get()));}
    return out;
}
struct NativeOwner {
    BW::ClientSpacePtr space;world::Registry registry;stock_native_destructible::NativeBackend backend;
    world::NativeScene scene;world::Pipeline pipeline;SceneGround ground;LocalGeometry local_geometry;
    std::unique_ptr<p::Ref> sensor,authority;
    std::unique_ptr<Owner> drive;std::unique_ptr<p::Ref> params;std::uint64_t generation=0;bool retired=false;
    bool ledger_installed=false;std::array<std::int64_t,2> ledger_revision{};std::uint64_t ledger_refreshes=0,ledger_reuses=0;
    NativeOwner(BW::ClientSpacePtr s,std::uint16_t flags,std::uint64_t gen):space(s),backend(s,registry,[](const world::TurretSweep&){return true;}),
        scene(s,registry,backend),pipeline(registry,scene,flags),ground(pipeline),generation(gen){}
    bool current(PyObject*battle)const{return !retired&&scene.current()&&p::integral(battle,"_generation")==std::int64_t(generation);}
    void retire(){retired=true;scene.retire();backend.retire();}
    void syncLedger(){
        // The existing visible adapter owns destruction and its presentation.
        // Copy only its accepted raw ledger at a slice boundary. No per-ray
        // producer, predicate, authority method or animator runs in Python.
        const std::array<std::int64_t,2> revision{{p::integral(authority->get(),"_native_client_drive_ledger_revision",-1),p::integral(sensor->get(),"_native_client_drive_ledger_revision",-1)}};
        if(revision[0]<0||revision[1]<0)throw p::Error("Native player ledger mutation counters were not installed");
        if(ledger_installed&&revision==ledger_revision){++ledger_reuses;return;}
        std::set<world::MaterialKey> broken,speculative;std::set<world::Key> trees;
        auto state=p::value(authority->get(),"_state"),chunks=p::value(state.get(),"chunks");
        if(!PyDict_Check(chunks.get()))throw p::Error("Native player accepted world ledger is unavailable");
        Py_ssize_t cursor=0;PyObject*chunk=nullptr,*row=nullptr;
        auto token=[&](PyObject*raw,std::int64_t chunk_id,bool with_chunk){
            const auto size=p::size(raw);if(size!=(with_chunk?3:2))throw p::Error("Native player world ledger width is invalid");
            auto first=p::item(raw,0),item=p::item(raw,with_chunk?1:0),material=p::item(raw,with_chunk?2:1);
            const auto c=with_chunk?p::wide(first.get()):chunk_id,i=p::wide(item.get());
            if(c<0||std::uint64_t(c)>0xffffffffULL||i<0||i>0xffff)throw p::Error("Native player world ledger identity is invalid");
            return world::MaterialKey{std::uint32_t(c),std::uint32_t(i),material.get()==Py_None?-1:p::wide(material.get())};};
        auto visit=[&](PyObject*raw,const auto&callback){if(!PySet_Check(raw)&&!PyFrozenSet_Check(raw))throw p::Error("Native player world ledger requires a raw set");
            p::Ref iterator(PyObject_GetIter(raw));while(PyObject*item=PyIter_Next(iterator.get())){p::Ref value(item);callback(value.get());}if(PyErr_Occurred())throw p::Error("Native player world ledger iteration failed");};
        while(PyDict_Next(chunks.get(),&cursor,&chunk,&row)){const auto id=p::wide(chunk);auto keys=p::value(row,"keys");if(keys.get()!=Py_None)visit(keys.get(),[&](PyObject*v){broken.insert(token(v,id,false));});}
        auto pending=p::value(sensor->get(),"g_offh_destr_speculative");if(pending.get()!=Py_None)visit(pending.get(),[&](PyObject*v){speculative.insert(token(v,0,true));});
        auto tree_state=p::value(sensor->get(),"g_offh_tree_state");
        auto committed=[&](){auto value=p::value(tree_state.get(),"native_committed");return value.get()==Py_None?p::value(tree_state.get(),"felled"):p::retain(value.get());}();
        if(committed.get()!=Py_None)visit(committed.get(),[&](PyObject*v){const auto key=visibleTreeKey(v);if(key&&!registry.isolated(*key)){trees.insert(*key);broken.insert({key->first,key->second,-1});}});
        if(registry.skin.destroyed!=broken||registry.skin.speculative!=speculative){registry.skin.destroyed=std::move(broken);registry.skin.speculative=std::move(speculative);++registry.generation;}
        registry.skin.tree_instances.insert(trees.begin(),trees.end());
        ledger_revision=revision;ledger_installed=true;++ledger_refreshes;
    }
    void frame(double now){if(!std::isfinite(now))throw p::Error("Native player world clock is invalid");syncLedger();}
    void installParams(PyObject*value,PyObject*battle){if(params&&params->get()==value)return;
        drive.reset(new Owner(descriptor(value)));drive->memory({memories(battle,"_local_spring_ground_memory"),memories(battle,"_local_pseudo_ground_memory")});
        params.reset(new p::Ref(p::retain(value)));}
};
struct Holder {std::shared_ptr<NativeOwner> owner;};
Holder* holder(PyObject*object){auto*value=static_cast<Holder*>(PyCapsule_GetPointer(object,capsule_name));if(!value)throw p::Error("Invalid native player drive handle");return value;}
std::shared_ptr<NativeOwner> current(PyObject*object,PyObject*battle){auto owner=holder(object)->owner;if(!owner||!owner->current(battle))throw p::Error("Native player drive owner is retired");return owner;}
void destroy(PyObject*object){auto*value=static_cast<Holder*>(PyCapsule_GetPointer(object,capsule_name));if(value){if(value->owner)value->owner->retire();delete value;}else PyErr_Clear();}
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
PyObject* create(PyObject*,PyObject*args){try{PyObject*battle=nullptr;if(!PyArg_ParseTuple(args,"O",&battle))return nullptr;
    auto avatar=p::value(battle,"_avatar");const auto id=p::integral(avatar.get(),"spaceID");if(id<=0||id>2147483647)throw p::Error("Native player space identity is invalid");
    auto space=BW::SpaceManager::instance().space(BW::SpaceID(id));if(!space||space->cleared())throw p::Error("Native player space is unavailable");
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.battle_runtime"));const auto flags=p::integral(module.get(),"VEHICLE_SKIP_FLAGS");if(flags<0||flags>65535)throw p::Error("Native player collision flags are invalid");
    std::unique_ptr<Holder> handle(new Holder);handle->owner.reset(new NativeOwner(space,std::uint16_t(flags),p::integral(battle,"_generation")));auto&owner=*handle->owner;
    owner.sensor.reset(new p::Ref(p::value(battle,"_destructibles")));
    owner.authority.reset(new p::Ref(p::Ref(PyImport_ImportModule("gui.mods.offline_lan_0922.destructibles_authority"))));
    p::Ref builder(PyImport_ImportModule("gui.mods.offline_lan_0922.stock_native_world_snapshot"));
    auto sensor=p::value(battle,"_destructibles");p::Ref build_args(PyTuple_Pack(1,sensor.get()));auto dto=p::call(builder.get(),"build",build_args.get());
    owner.local_geometry.seedIsolation(owner.sensor->get());
    require(world::installWorldSnapshot(dto.get(),owner.registry,owner.scene,owner.backend));owner.syncLedger();
    p::Ref capsule(PyCapsule_New(handle.get(),capsule_name,destroy));handle.release();return capsule.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* close(PyObject*,PyObject*args){try{PyObject*object=nullptr;if(!PyArg_ParseTuple(args,"O",&object))return nullptr;auto*h=holder(object);if(h->owner)h->owner->retire();h->owner.reset();Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject* restore(PyObject*,PyObject*args){try{PyObject*object=nullptr,*battle=nullptr;if(!PyArg_ParseTuple(args,"OO",&object,&battle))return nullptr;auto owner=current(object,battle);
    if(owner->drive)owner->drive->memory({memories(battle,"_local_spring_ground_memory"),memories(battle,"_local_pseudo_ground_memory")});Py_RETURN_NONE;}catch(const std::exception&e){return fail(e);}}
PyObject* frame(PyObject*,PyObject*args){try{PyObject*object=nullptr,*battle=nullptr;double now=0.;if(!PyArg_ParseTuple(args,"OOd",&object,&battle,&now))return nullptr;auto owner=current(object,battle);owner->frame(now);Py_RETURN_TRUE;}catch(const std::exception&e){return fail(e);}}
PyObject* ingress(PyObject*,PyObject*args){try{PyObject*object=nullptr,*battle=nullptr,*event=nullptr;double now=0.;if(!PyArg_ParseTuple(args,"OOOd",&object,&battle,&event,&now))return nullptr;auto owner=current(object,battle);(void)event;(void)now;owner->syncLedger();Py_RETURN_TRUE;}catch(const std::exception&e){return fail(e);}}
p::Ref planeValue(const Output&out){if(!out.state.ground_plane)return p::retain(Py_None);const auto&plane=*out.state.ground_plane;auto value=p::dict();
    p::set(value.get(),"center_x",p::number(plane.center_x));p::set(value.get(),"center_y",p::number(plane.center_y));p::set(value.get(),"center_z",p::number(plane.center_z));
    p::set(value.get(),"gradient_x",p::number(plane.gradient_x));p::set(value.get(),"gradient_z",p::number(plane.gradient_z));p::set(value.get(),"normal",p::tupleNumbers(plane.normal));
    p::set(value.get(),"max_residual",p::number(out.plane_residual));p::set(value.get(),"contact_count",p::integer(out.plane_contacts));
    const double sn=std::sin(out.state.yaw),cs=std::cos(out.state.yaw),f=plane.gradient_x*sn+plane.gradient_z*cs,r=plane.gradient_x*cs-plane.gradient_z*sn;
    const double tangent=std::hypot(plane.gradient_x,plane.gradient_z);p::set(value.get(),"pitch",p::number(-std::atan2(f,1.)));p::set(value.get(),"roll",p::number(std::atan2(r,std::sqrt(1.+f*f))));
    p::set(value.get(),"slope_tangent",p::number(tangent));p::set(value.get(),"up_cosine",p::number(1./std::sqrt(1.+tangent*tangent)));
    p::set(value.get(),"downhill",p::tupleNumbers(Point{tangent>.001?-plane.gradient_x/tangent:0.,0.,tangent>.001?-plane.gradient_z/tangent:0.}));return value;}
PyObject* advance(PyObject*,PyObject*args){try{PyObject*object=nullptr,*battle=nullptr,*params=nullptr,*position=nullptr;double yaw=0.,dt=0.,height=0.,speed=0.,turret=0.;
    if(!PyArg_ParseTuple(args,"OOOOddddd",&object,&battle,&params,&position,&yaw,&dt,&height,&speed,&turret))return nullptr;auto owner=current(object,battle);owner->syncLedger();owner->installParams(params,battle);
    Input input;auto&s=input.state;s.position=p::numbers<3>(position);s.yaw=yaw;
#define READ(member,name) s.member=p::numeric(battle,name)
    READ(pitch,"_local_pitch");s.terrain_pitch=s.pitch;READ(roll,"_local_roll");READ(vertical_speed,"_local_vertical_speed");
    READ(pitch_speed,"_local_suspension_pitch_velocity");READ(roll_speed,"_local_suspension_roll_velocity");READ(support_vertical_speed,"_local_suspension_support_vertical_speed");
    READ(speed,"_local_speed");READ(turn_speed,"_local_turn_speed");
#undef READ
    s.airborne=p::truth(battle,"_local_airborne");s.grounded_once=p::truth(battle,"_local_fall_armed");s.left_flying=p::truth(battle,"_local_left_flying");s.right_flying=p::truth(battle,"_local_right_flying");
    s.ground_plane=plane(battle,"_local_ground_plane");s.support_gradient=gradient(battle,"_local_suspension_support_gradient");
    input.tick_position=optionalPoint(battle,"_local_support_tick_pose").value_or(s.position);input.motion_position=optionalPoint(battle,"_local_support_motion_pose");
    if(!input.motion_position)input.motion_position=optionalPoint(battle,"_local_support_tick_pose");input.elapsed=dt;input.turret_yaw=turret;input.height_delta=height;input.speed_delta=speed;
    owner->local_geometry.sync(owner->sensor->get(),owner->registry,owner->scene,owner->drive->region(input));
    const auto out=owner->drive->advance(input,owner->ground);const auto&v=out.state;
#define WRITE(member,name) attribute(battle,name,p::number(v.member))
    WRITE(vertical_speed,"_local_vertical_speed");WRITE(pitch,"_local_pitch");WRITE(roll,"_local_roll");WRITE(pitch_speed,"_local_suspension_pitch_velocity");WRITE(roll_speed,"_local_suspension_roll_velocity");WRITE(support_vertical_speed,"_local_suspension_support_vertical_speed");
#undef WRITE
    attribute(battle,"_local_airborne",p::boolean(v.airborne));attribute(battle,"_local_fall_armed",p::boolean(v.grounded_once));
    attribute(battle,"_local_left_flying",p::boolean(v.left_flying));attribute(battle,"_local_right_flying",p::boolean(v.right_flying));
    attribute(battle,"_local_suspension_support_gradient",v.support_gradient?p::tupleNumbers(*v.support_gradient):p::retain(Py_None));
    attribute(battle,"_local_support_rise_blocked",p::boolean(out.outcome.blocked));
    attribute(battle,"_local_spring_ground_memory",memoryValue(owner->drive->memory().springs));attribute(battle,"_local_pseudo_ground_memory",memoryValue(owner->drive->memory().pseudo));
    p::Ref probes(PyTuple_New(Py_ssize_t(out.probes.size())));Py_ssize_t probe_index=0;
    for(const auto&probe:out.probes){p::Ref layers(PyTuple_New(Py_ssize_t(probe.layers.size())));Py_ssize_t layer_index=0;
        for(const auto&layer:probe.layers){const char*verdict=layer.verdict==GroundVerdict::OutsideBand?"outside_band":layer.verdict==GroundVerdict::Wall?"underside_or_wall":layer.verdict==GroundVerdict::AboveFlat?"above_flat_limit":"support";
            PyTuple_SET_ITEM(layers.get(),layer_index++,Py_BuildValue("(dds)",layer.height,layer.up,verdict));}
        auto direct=probe.direct?p::number(*probe.direct):p::retain(Py_None),retained=probe.retained?p::number(*probe.retained):p::retain(Py_None),flat=probe.flat?p::number(*probe.flat):p::retain(Py_None);
        PyTuple_SET_ITEM(probes.get(),probe_index++,Py_BuildValue("(ddddNNNN)",probe.x,probe.z,probe.low,probe.high,direct.release(),retained.release(),flat.release(),layers.release()));}
    attribute(battle,"_local_suspension_probe_trace",probes);
    if(!out.outcome.blocked&&!out.outcome.pending_spawn){auto value=planeValue(out);attribute(battle,"_local_ground_plane",value);
        bool contacting=false;for(const auto&c:out.contacts)if(c.spring&&c.ground)contacting=true;
        attribute(battle,"_local_surface_up_cosine",contacting&&!v.airborne?p::number(std::max(-1.,std::min(1.,std::cos(v.pitch)*std::cos(v.roll)))):p::retain(Py_None));
        const double tangent=v.ground_plane?std::hypot(v.ground_plane->gradient_x,v.ground_plane->gradient_z):0.;
        attribute(battle,"_local_slope_tangent",p::number(tangent));attribute(battle,"_local_downhill",p::tupleNumbers(Point{tangent>.001?-v.ground_plane->gradient_x/tangent:0.,0.,tangent>.001?-v.ground_plane->gradient_z/tangent:0.}));
    }
    if(!s.airborne&&v.airborne){attribute(battle,"_local_turn_speed",p::number(0.));attribute(battle,"_local_drive_turn",p::number(0.));attribute(battle,"_local_drive_throttle",p::number(0.));attribute(battle,"_local_direction_command",p::number(0.));attribute(battle,"_local_service_brake",p::boolean(false));}
    auto witnesses=p::list();for(const auto&q:out.outcome.landing_witnesses)p::append(witnesses.get(),p::tupleNumbers(q));
    p::Ref result(PyTuple_New(4));PyTuple_SET_ITEM(result.get(),0,p::tupleNumbers(v.position).release());
    PyTuple_SET_ITEM(result.get(),1,(out.outcome.landing_impact?p::number(*out.outcome.landing_impact):p::retain(Py_None)).release());
    PyTuple_SET_ITEM(result.get(),2,witnesses.release());PyTuple_SET_ITEM(result.get(),3,p::tupleNumbers(out.outcome.landing_normal).release());return result.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* statistics(PyObject*,PyObject*args){try{PyObject*object=nullptr;if(!PyArg_ParseTuple(args,"O",&object))return nullptr;auto owner=holder(object)->owner;auto out=p::dict();
    const auto stats=owner&&owner->drive?owner->drive->statistics():Statistics{};p::set(out.get(),"steps",p::integer(stats.steps));p::set(out.get(),"columns",p::integer(stats.columns));p::set(out.get(),"python_business_callbacks",p::integer(stats.python_business_callbacks));
    p::set(out.get(),"geometry_refreshes",p::integer(owner?owner->local_geometry.refreshes():0));p::set(out.get(),"geometry_reuses",p::integer(owner?owner->local_geometry.reuses():0));
    p::set(out.get(),"ledger_refreshes",p::integer(owner?owner->ledger_refreshes:0));p::set(out.get(),"ledger_reuses",p::integer(owner?owner->ledger_reuses:0));return out.release();}catch(const std::exception&e){return fail(e);}}
PyMethodDef methods[]={{"create",create,METH_VARARGS,"Install the visible player's native world owner."},{"close",close,METH_VARARGS,"Retire native visible player resources."},
    {"advance",advance,METH_VARARGS,"Sample and integrate the exact local suspension slice."},{"restore",restore,METH_VARARGS,"Restore contact memory after a rejected complete pose."},
    {"frame",frame,METH_VARARGS,"Advance native streamed destructible resources."},{"ingress",ingress,METH_VARARGS,"Copy the visible owner accepted destruction ledger."},
    {"statistics",statistics,METH_VARARGS,"Read native local suspension counters."},{nullptr,nullptr,0,nullptr}};
class Module final:public BW::Script::InitTimeJob {public:Module():BW::Script::InitTimeJob(1){}void init()override{if(!Py_InitModule("offline_client_drive_native",methods)){PyErr_Print();CRITICAL_MSG("Native player drive module initialization failed\n");}}};Module module;
}
}
