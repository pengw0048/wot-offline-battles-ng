#if !defined(WOT_CLIENT_QUERY_HOST_BUILD)
#include "pch.hpp"
#include "connection_control.hpp"
#include "entity.hpp"
#include "py_entity.hpp"
#include "pyscript/script_math.hpp"
#include "stock_battle_scene_queries.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#endif
#include "stock_native_bot_resource_lifecycle.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_battle.hpp"
#include "stock_native_battle_visibility.hpp"
#include "stock_native_bot_python.hpp"
#include "stock_native_bot_turret_obstacles.hpp"
#include "stock_native_detached_turret_snapshot.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <new>
#include <set>

namespace stock_native_client_query {
namespace p=stock_native_bot_python;
namespace state=stock_native_client_state;
namespace geo=stock_native_bot_geometry;
namespace {
using Point=state::Point;
using Bounds=state::Bounds;
constexpr const char* capsule_name="offline.client.query";
constexpr double pi=3.14159265358979323846;
// Stock outline uses this exact distance comparison, including equality.

p::Ref field(PyObject*o,const char*n){
    if(!o||o==Py_None)return p::retain(Py_None);
    if(PyDict_Check(o))return p::value(o,n);
    auto key=p::text(n);auto*v=p::directAttribute(o,key.get());
    if(!v&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();return p::retain(Py_None);}
    return p::Ref(v);
}
struct CachedGeometry {
    p::Ref descriptor;
    std::uint64_t revision=0;
    std::unique_ptr<geo::Vehicle> vehicle;
    std::vector<p::Ref> material_maps;
    explicit CachedGeometry(PyObject*d):descriptor(p::retain(d)){}
};
struct ResourceKey {
    stock_native_bot::ResourceStamp stamp;
    std::vector<p::Ref> owners;
    bool operator==(const ResourceKey&other)const{return stamp==other.stamp;}
};
ResourceKey resourceStamp(PyObject*battle,PyObject*entity,PyObject*descriptor){
    ResourceKey key;
    if(!stock_native_bot::readResourceStamp(entity,{descriptor},key.stamp,battle))throw p::Error("Native client query resource stamp is unavailable");
    auto appearance=field(entity,"appearance");
    // #1513 filter body/ground getters allocate a fresh provider per read.
    // readResourceStamp already identifies their stable filter owner.
    // Appearance stores adaptive turret/gun providers once in its constructor;
    // their changing matrix values/targets do not replace static geometry.
    key.owners.push_back(field(entity,"filter"));
    for(const char*name:{"compoundModel","turretMatrix","gunMatrix"}){
        auto owner=field(appearance.get(),name);
        key.stamp.owners.push_back(owner.get()==Py_None?nullptr:owner.get());
        // Retain comparison owners until replacement to prevent address reuse.
        key.owners.push_back(std::move(owner));
    }
    return key;
}
#if defined(WOT_CLIENT_QUERY_HOST_BUILD)
extern std::unique_ptr<geo::Vehicle> readGeometry(PyObject*,PyObject*,bool,std::uint64_t);
extern Point nativeXYZ(PyObject*);
extern bool engineOwns(std::int64_t,PyObject*);
extern std::optional<Bounds> bounds(const geo::Vehicle&,const state::Actor&);
extern std::vector<double> hits(const geo::Vehicle&,const state::Actor&,const Point&,const Point&);
extern std::optional<double> hullRadius(const geo::Vehicle&);
extern std::optional<double> scenery(std::int64_t,const Point&,const Point&);
#else
Point nativeXYZ(PyObject*o){BW::Vector3 v;if(BW::Script::setData(o,v)!=0)throw p::Error("Native client query point is invalid");return {v.x,v.y,v.z};}
bool engineOwns(std::int64_t id,PyObject*object){
    auto*c=BW::ConnectionControl::pInstance();auto*e=c?c->findEntity(static_cast<BW::EntityID>(id)):nullptr;
    if(!e||e->isDestroyed()||!e->isInWorld())return false;
    p::Ref actual(e->pPyEntity().newRef());return actual.get()==object;
}
std::unique_ptr<geo::Vehicle> readGeometry(PyObject*d,PyObject*entity,bool authority,std::uint64_t version){
    auto out=std::make_unique<geo::Vehicle>();
    if(!geo::readVehicle(d,entity,authority,version,*out))throw p::Error("Native client query geometry is unavailable");
    return out;
}
geo::Pose geometryPose(const state::Actor&a){
    geo::Pose out;out.position={a.current[0],a.current[1],a.current[2]};
    out.yaw=a.current[3];out.pitch=a.current[4];out.roll=a.current[5];
    out.turret_yaw=a.current[6]-a.current[3];out.gun_pitch=a.current[7];
    out.siege=int(a.siege_state);out.native_components=true;out.native_hydraulics=true;return out;
}
std::optional<Bounds> bounds(const geo::Vehicle&cached,const state::Actor&a){
    auto&shape=const_cast<geo::Vehicle&>(cached);shape.detached=a.detached;
    const auto b=geo::worldBounds(shape,geometryPose(a));
    return b?std::optional<Bounds>(Bounds{b->first,b->second}):std::nullopt;
}
std::vector<double> hits(const geo::Vehicle&cached,const state::Actor&a,const Point&start,const Point&end){
    auto&shape=const_cast<geo::Vehicle&>(cached);shape.detached=a.detached;
    std::vector<double> out;for(const auto&hit:geo::intersections(shape,geometryPose(a),start,end))out.push_back(hit.distance);
    return out;
}
std::optional<double> hullRadius(const geo::Vehicle&v){
    const auto&c=v.components[1];if(!c.bounds_available)return {};
    // Preserve the installed adapter's nomination cone. It is only an
    // optimization; selection still requires the exact component BSP hit.
    const double length=std::max(3.,std::abs(c.minimum[2])+std::abs(c.maximum[2]));
    const double width=std::max(2.,std::abs(c.minimum[0])+std::abs(c.maximum[0]));
    return .5*std::hypot(length,width);
}
std::optional<double> scenery(std::int64_t space,const Point&a,const Point&b){
    BW::StockSegmentHit hit;
    const BW::Vector3 start{float(a[0]),float(a[1]),float(a[2])},end{float(b[0]),float(b[1]),float(b[2])};
    const auto result=BW::collideStockSegment(static_cast<BW::SpaceID>(space),start,end,128,nullptr,hit);
    if(result==BW::STOCK_SEGMENT_FAILED)throw p::Error("Native target scenery query failed");
    if(result==BW::STOCK_SEGMENT_NO_SPACE)throw p::Error("Native target space is unavailable");
    return result==BW::STOCK_SEGMENT_HIT?std::optional<double>((hit.point-start).length()):std::nullopt;
}
#endif
Point xyz(PyObject*o){return PyTuple_Check(o)||PyList_Check(o)?p::numbers<3>(o):nativeXYZ(o);}
double length(const Point&p){return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);}
Point subtract(const Point&a,const Point&b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Point along(const Point&a,const Point&d,double n){return {a[0]+d[0]*n,a[1]+d[1]*n,a[2]+d[2]*n};}
// Slab clipping never substitutes a guessed tank radius. Bounds are the
// loaded attached component boxes transformed at the current native pose.
std::optional<double> entryDistance(const Bounds&b,const Point&a,const Point&end){
    double begin=0.,finish=1.;const auto d=subtract(end,a);
    for(unsigned i=0;i<3;++i){
        if(!std::isfinite(b.minimum[i])||!std::isfinite(b.maximum[i])||b.minimum[i]>b.maximum[i])throw p::Error("Native query bounds are invalid");
        if(d[i]==0.){if(a[i]<b.minimum[i]||a[i]>b.maximum[i])return {};continue;}
        double lo=(b.minimum[i]-a[i])/d[i],hi=(b.maximum[i]-a[i])/d[i];if(lo>hi)std::swap(lo,hi);
        begin=std::max(begin,lo);finish=std::min(finish,hi);if(begin>finish)return {};
    }
    return begin*length(d);
}
struct Entry {
    std::shared_ptr<state::Actor> actor;
    p::Ref entity,appearance,descriptor,record;
    std::uint64_t descriptor_revision=0;
    ResourceKey resource_stamp;
    std::shared_ptr<CachedGeometry> geometry;
    Entry(std::shared_ptr<state::Actor>a,PyObject*e,PyObject*ap,PyObject*d,PyObject*r):actor(std::move(a)),entity(p::retain(e)),appearance(p::retain(ap)),descriptor(p::retain(d)),record(p::retain(r)){}
};
struct Collision {double distance=0.,cosine=0.;unsigned component=0;std::uint32_t material=0;};
#if defined(WOT_CLIENT_QUERY_HOST_BUILD)
extern std::vector<Collision> collisionRows(const geo::Vehicle&,const state::Actor&,const Point&,const Point&);
#else
std::vector<Collision> collisionRows(const geo::Vehicle&cached,const state::Actor&a,const Point&start,const Point&end){
    auto&shape=const_cast<geo::Vehicle&>(cached);shape.detached=a.detached;std::vector<Collision> out;
    for(const auto&h:geo::intersections(shape,geometryPose(a),start,end))out.push_back({h.distance,h.angle_cosine,h.component,h.material_kind});return out;
}
#endif
struct Turret {Bounds bounds;double settles_at=0.;};
struct Owner {
    std::shared_ptr<state::State> state;
    p::Ref collision_type;
    std::uint64_t epoch=0;
    std::map<std::int64_t,std::unique_ptr<Entry>> entries;
    std::map<PyObject*,std::int64_t> objects;
    std::vector<Turret> turrets;
    stock_native_bot_turret::Obstacles projectile_turrets;
    std::vector<PhysicalTurret> physical_turrets;
    std::map<stock_native_bot_turret::Owner,std::shared_ptr<const void>> turret_geometry;
    std::vector<std::int64_t> selection_order,candidate_order;
    bool closed=false;
    std::uint64_t queries=0,bounds_tests=0,bsp_tests=0,cold_installs=0;
    explicit Owner(std::shared_ptr<state::State>s,PyObject*t):state(std::move(s)),collision_type(p::retain(t)),epoch(state->epoch){}
};
Owner& owner(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(!o)throw p::Error("Native client query owner is invalid");return *o;}
bool active(const Owner&o){return !o.closed&&!o.state->retired&&o.epoch==o.state->epoch;}
void destroy(PyObject*c){auto*o=static_cast<Owner*>(PyCapsule_GetPointer(c,capsule_name));if(o)delete o;else PyErr_Clear();}
bool current(const Owner&o,const Entry&e){
    if(!active(o))return false;
    const auto found=o.state->actors.find(e.actor->key);
    return found!=o.state->actors.end()&&found->second==e.actor&&!e.actor->missing&&!e.actor->tombstone&&
        e.actor->ready&&e.actor->has_current&&e.actor->descriptor_revision==e.descriptor_revision&&
        engineOwns(e.actor->engine_id,e.entity.get());
}
p::Ref failure(const std::string&key,const std::string&message){auto row=p::dict();p::set(row.get(),"entity",p::text(key));p::set(row.get(),"error",p::text(message));return row;}
std::optional<Bounds> worldBounds(Owner&o,const Entry&e){
    if(!e.geometry)return {}; ++o.bounds_tests;return bounds(*e.geometry->vehicle,*e.actor);
}
std::optional<double> nearest(Owner&o,const Entry&e,const Point&a,const Point&b){
    if(!e.geometry)return {};const auto box=worldBounds(o,e);if(box&&!entryDistance(*box,a,b))return {};
    ++o.bsp_tests;const auto values=hits(*e.geometry->vehicle,*e.actor,a,b);std::optional<double> out;
    const double maximum=length(subtract(b,a));
    for(double d:values){if(!std::isfinite(d))throw p::Error("Native target BSP returned a non-finite distance");if(d>=0.&&d<=maximum&&(!out||d<*out))out=d;}
    return out;
}
class ProjectileTurretAccess final:public ProjectileTurrets,private stock_native_bot_turret::DetachedHits {
    p::Ref capsule_;
    Owner* owner_;
    std::vector<double> segment(stock_native_bot_turret::Owner who,bool gun,const Point&a,const Point&b)override{
        if(!stock_native_client_query::active(*owner_))throw p::Error("Native projectile turret owner is stale");
        const auto found=owner_->turret_geometry.find(who);if(found==owner_->turret_geometry.end())throw p::Error("Native detached turret BSP owner is unavailable");
#if defined(WOT_CLIENT_QUERY_HOST_BUILD)
        (void)gun;(void)a;(void)b;throw p::Error("Host fixture has no detached turret BSP");
#else
        const auto*cached=static_cast<const CachedGeometry*>(found->second.get());if(!cached||!cached->vehicle)throw p::Error("Native detached turret geometry is unavailable");
        const auto&component=cached->vehicle->components[gun?3:2];if(!component.tree)throw p::Error("Native detached turret BSP is unavailable");
        const BW::Vector3 start{float(a[0]),float(a[1]),float(a[2])},end{float(b[0]),float(b[1]),float(b[2])};std::vector<double> distances;
        for(const auto&hit:BW::stockBspSegment(*component.tree,start,end))distances.push_back(hit.distance);return distances;
#endif
    }
public:
    explicit ProjectileTurretAccess(PyObject*c):capsule_(p::retain(c)),owner_(&owner(c)){}
    bool active()const override{return stock_native_client_query::active(*owner_);}
    std::size_t count()const override{return active()?owner_->projectile_turrets.active():0;}
    const std::vector<PhysicalTurret>& physical()const override{if(!active())throw p::Error("Native turret physical owner is stale");return owner_->physical_turrets;}
    bool clear(const stock_native_world::TurretSweep&sweep,const stock_native_bot_turret::MovingDescriptor&body,double server_ms)const override{if(!active())throw p::Error("Native client turret owner is stale");return !owner_->projectile_turrets.blocks(sweep,body,server_ms);}
    std::optional<double> block(const Point&a,const Point&b,double end,std::optional<double>start)override{
        if(!stock_native_client_query::active(*owner_))throw p::Error("Native projectile turret owner is stale");
        return owner_->projectile_turrets.block_distance(a,b,end,*this,start);
    }
};
void projectileTurret(Owner&o,PyObject*raw,stock_native_bot_turret::Obstacles&out,std::map<stock_native_bot_turret::Owner,std::shared_ptr<const void>>&resources,std::vector<PhysicalTurret>&physical){
    auto row=field(raw,"row");if(row.get()==Py_None)return;auto flight=field(row.get(),"flight"),kind=field(row.get(),"actor_kind");stock_native_bot_turret::Record record;
    record.owner={p::string(kind.get())=="player",p::integral(row.get(),"actor_id")};record.motion_sequence=p::integral(row.get(),"motion_seq");record.created_time_ms=p::numeric(row.get(),"created_time_ms");record.duration=p::numeric(flight.get(),"duration");record.landed=p::truth(flight.get(),"landed");record.has_body=PyDict_Check(flight.get())&&PyDict_GetItemString(flight.get(),"body");
    auto rest=field(flight.get(),"rest"),attitude=field(row.get(),"attitude"),spin=field(row.get(),"spin"),settled=field(flight.get(),"rest_attitude");record.rest=xyz(rest.get());record.attitude=p::numbers<3>(attitude.get());record.spin=p::numbers<3>(spin.get());if(settled.get()!=Py_None)record.rest_attitude=p::numbers<3>(settled.get());
    auto support=field(row.get(),"support_key");if(record.motion_sequence>0&&support.get()!=Py_None){const auto key=p::string(support.get());const auto colon=key.find(':');if(colon==std::string::npos)throw p::Error("Native turret support identity is invalid");record.support=stock_native_bot_turret::Owner{key.substr(0,colon)=="player",std::stoll(key.substr(colon+1))};}
    stock_native_bot_turret::DetachedDescriptor descriptor;bool turret=false,gun=false;auto components=field(raw,"components");
    for(Py_ssize_t i=0;i<p::size(components.get());++i){auto component=p::item(components.get(),i),name=p::item(component.get(),0),offset=p::item(component.get(),2),bounds=p::item(component.get(),3),low=p::item(bounds.get(),0),high=p::item(bounds.get(),1);const auto type=p::string(name.get());auto*target=type=="turret"?&descriptor.turret:type=="gun"?&descriptor.gun:nullptr;if(!target)continue;target->offset=xyz(offset.get());target->bounds={xyz(low.get()),xyz(high.get())};turret=turret||type=="turret";gun=gun||type=="gun";}
    if(!turret||!gun)throw p::Error("Native detached turret components are incomplete");
    const auto key=std::string(record.owner.human?"player:":"bot:")+std::to_string(record.owner.id);const auto*a=o.state->find(key);const auto old=o.turret_geometry.find(record.owner);if(old!=o.turret_geometry.end())resources[record.owner]=old->second;else if(a&&a->geometry)resources[record.owner]=a->geometry;
    out.put(record,descriptor);
    if(record.has_body&&a&&a->ready&&!a->missing&&!a->tombstone){stock_native_detached_turret::Row accepted;if(!stock_native_detached_turret::readRow(row.get(),accepted)||!accepted.flight.body)throw p::Error("Native detached turret physical frame is unavailable");PhysicalTurret body;body.owner=record.owner;body.frame=*accepted.flight.body;
        for(Py_ssize_t i=0;i<p::size(components.get());++i){auto part=p::item(components.get(),i),name=p::item(part.get(),0),resource=p::item(part.get(),1);const auto type=p::string(name.get());if(type!="turret"&&type!="gun")continue;stock_native_detached_turret::Component value;value.geometry=type=="turret"?descriptor.turret:descriptor.gun;value.mass=p::numeric(resource.get(),"weight");if(value.mass<=0.)throw p::Error("Native detached turret component mass is invalid");body.components.push_back(std::move(value));}physical.push_back(std::move(body));}
}
p::Ref tuple2(const p::Ref&a,const p::Ref&b){return p::Ref(PyTuple_Pack(2,a.get(),b.get()));}
PyObject* fail(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}
}
const geo::Vehicle* geometry(const state::Actor&a){
    const auto*g=static_cast<const CachedGeometry*>(a.geometry.get());return g&&g->revision==a.descriptor_revision?g->vehicle.get():nullptr;
}
PyObject* descriptor(const state::Actor&a){
    const auto*g=static_cast<const CachedGeometry*>(a.geometry.get());return g&&g->revision==a.descriptor_revision?g->descriptor.get():nullptr;
}
std::shared_ptr<ProjectileTurrets> projectileTurrets(native_battle::Battle&b){auto*handle=native_battle::visibility::queryOwner(b);if(!handle)throw p::Error("Native client query owner is unavailable");return std::make_shared<ProjectileTurretAccess>(handle);}
PyObject* create(PyObject*,PyObject*args){PyObject*battle,*collision_type=Py_None;if(!PyArg_ParseTuple(args,"O|O",&battle,&collision_type))return nullptr;try{
    auto shared=state::fromBattle(battle);if(!shared||shared->retired)throw p::Error("Native client state is unavailable");
    auto o=std::make_unique<Owner>(std::move(shared),collision_type);p::Ref capsule(PyCapsule_New(o.get(),capsule_name,destroy));o.release();return capsule.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* ingest(PyObject*,PyObject*args){PyObject*c,*battle;if(!PyArg_ParseTuple(args,"OO",&c,&battle))return nullptr;try{
    auto&o=owner(c);auto errors=p::list();if(o.closed||o.state->retired)return errors.release();
    if(o.state!=state::fromBattle(battle))throw p::Error("Native client query state identity changed");
    if(o.epoch!=o.state->epoch){o.entries.clear();o.objects.clear();o.turrets.clear();o.projectile_turrets.clear();o.physical_turrets.clear();o.turret_geometry.clear();o.selection_order.clear();o.candidate_order.clear();o.epoch=o.state->epoch;}
    auto factory=field(battle,"_remote_factory"),vehicles=field(factory.get(),"_vehicles");
    auto runtime=field(battle,"_runtime"),bw=field(runtime.get(),"bigworld"),records=field(battle,"_records");
    std::set<std::int64_t> seen;
    for(const auto&item:o.state->actors){auto a=item.second;
        if(a->missing||a->tombstone)continue;
        auto actor_key=p::text(a->key);
        auto*record=PyDict_Check(records.get())?PyDict_GetItem(records.get(),actor_key.get()):nullptr;
        // Stock marks the local record ready before camera alignment donates
        // the first typed pose. Resource installation is a cold lifecycle
        // transaction; actual geometry queries still require current().
        if(record){a->ready=p::truth(record,"ready",a->ready);a->tombstone=p::truth(record,"tombstone");const auto engine=p::integral(record,"engine_id",a->engine_id);if(engine>0)a->engine_id=engine;}
        if(a->tombstone||!a->ready||a->engine_id<=0)continue;
        const auto id=a->engine_id;seen.insert(id);try{
            auto id_object=p::integer(id);PyObject*raw=PyDict_Check(vehicles.get())?PyDict_GetItem(vehicles.get(),id_object.get()):nullptr;
            p::Ref entity(raw?p::retain(raw):p::call(bw.get(),"entity",p::Ref(PyTuple_Pack(1,id_object.get())).get()));
            if(entity.get()==Py_None||!engineOwns(id,entity.get())){o.entries.erase(id);a->geometry.reset();continue;}
            auto d=field(entity.get(),"typeDescriptor"),ap=field(entity.get(),"appearance");
            if(!record)throw p::Error("Native query lifecycle record is missing");
            auto resource_stamp=resourceStamp(battle,entity.get(),d.get());
            auto found=o.entries.find(id);
            if(found!=o.entries.end()&&found->second->actor==a&&found->second->entity.get()==entity.get()&&
                found->second->descriptor.get()==d.get()&&found->second->appearance.get()==ap.get()&&found->second->record.get()==record&&found->second->resource_stamp==resource_stamp&&found->second->descriptor_revision==a->descriptor_revision&&found->second->geometry)continue;
            // Erase the previous resource before attempting a replacement.
            // A pending compound may recover on the next lifecycle ingress.
            o.entries.erase(id);a->geometry.reset();
            auto entry=std::make_unique<Entry>(a,entity.get(),ap.get(),d.get(),record);entry->descriptor_revision=a->descriptor_revision;entry->resource_stamp=std::move(resource_stamp);
            auto cached=std::make_shared<CachedGeometry>(d.get());cached->revision=a->descriptor_revision;
            try{cached->vehicle=readGeometry(d.get(),entity.get(),a->local,a->descriptor_revision);
                for(const char*name:{"chassis","hull","turret","gun"}){auto component=field(d.get(),name);cached->material_maps.push_back(field(component.get(),"materials"));}
            }catch(const std::exception&error){PyErr_Clear();cached.reset();p::append(errors.get(),failure(a->key,error.what()));}
            if(!active(o))break;
            entry->geometry=cached;a->geometry=cached;o.entries[id]=std::move(entry);if(cached)++o.cold_installs;
        }catch(const std::exception&e){PyErr_Clear();o.entries.erase(id);a->geometry.reset();p::append(errors.get(),failure(a->key,e.what()));}
    }
    for(auto i=o.entries.begin();i!=o.entries.end();)if(!seen.count(i->first))i=o.entries.erase(i);else ++i;
    o.objects.clear();for(const auto&item:o.entries)o.objects[item.second->entity.get()]=item.first;
    // Preserve the original CPython record/arena iteration order for equal
    // exact-hit distances, rather than introducing an actor-ID tie breaker.
    o.selection_order.clear();Py_ssize_t cursor=0;PyObject*key,*record;
    if(PyDict_Check(records.get()))while(PyDict_Next(records.get(),&cursor,&key,&record)){
        const auto id=p::integral(record,"engine_id");if(o.entries.count(id))o.selection_order.push_back(id);
    }
    auto avatar=field(battle,"_avatar"),arena=field(avatar.get(),"arena"),roster=field(arena.get(),"vehicles");
    o.candidate_order.clear();cursor=0;
    if(PyDict_Check(roster.get()))while(PyDict_Next(roster.get(),&cursor,&key,&record)){const auto id=p::wide(key);if(o.entries.count(id))o.candidate_order.push_back(id);}
    else o.candidate_order=o.selection_order;
    return errors.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* candidates(PyObject*,PyObject*args){PyObject*c,*except_ids,*start,*end,*extras;
    if(!PyArg_ParseTuple(args,"OOOOO",&c,&except_ids,&start,&end,&extras))return nullptr;try{
    auto&o=owner(c);if(!active(o))return p::retain(Py_None).release();++o.queries;
    const bool segmented=start!=Py_None&&end!=Py_None;Point a{},b{};if(segmented){a=xyz(start);b=xyz(end);}
    std::set<std::int64_t> omitted;p::Ref iterator(PyObject_GetIter(except_ids));
    while(PyObject*raw=PyIter_Next(iterator.get())){p::Ref value(raw);omitted.insert(p::wide(value.get()));}if(PyErr_Occurred())throw p::Error("Native client query exclusions are invalid");
    auto out=p::list();
    for(const auto id:o.candidate_order){const auto item=o.entries.find(id);if(item==o.entries.end())continue;const auto&e=*item->second;if(omitted.count(id)||!current(o,e)||(!e.actor->visible&&e.actor->alive))continue;
        try{if(segmented){const auto box=worldBounds(o,e);if(box&&!entryDistance(*box,a,b))continue;}p::append(out.get(),e.entity);}
        catch(const std::exception&){PyErr_Clear();/* A broadphase failure cannot reject a safe exact test. */p::append(out.get(),e.entity);}
    }
    // ProjectileAwareEntities are separate stock objects, not actor records.
    // Preserve their exact filter ABI and the absence of an exceptIDs gate.
    p::Ref extra_iter(PyObject_GetIter(extras));while(PyObject*raw=PyIter_Next(extra_iter.get())){p::Ref entity(raw);try{
        if(segmented){auto filter=field(entity.get(),"filter"),method=field(filter.get(),"segmentMayHitEntity");if(method.get()!=Py_None){p::Ref call_args(PyTuple_Pack(3,start,end,Py_True));p::Ref result(PyObject_CallObject(method.get(),call_args.get()));if(!p::flag(result.get()))continue;}}
        if(!active(o))break;p::append(out.get(),entity);
    }catch(const std::exception&){PyErr_Clear();}}
    if(PyErr_Occurred())throw p::Error("Native client projectile-aware entities are invalid");return out.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* segmentMayHit(PyObject*,PyObject*args){PyObject*c,*entity,*start,*end;if(!PyArg_ParseTuple(args,"OOOO",&c,&entity,&start,&end))return nullptr;try{
    auto&o=owner(c);if(!active(o))return p::retain(Py_None).release();const auto found=o.objects.find(entity);if(found==o.objects.end())return p::retain(Py_None).release();
    const auto item=o.entries.find(found->second);if(item==o.entries.end()||!current(o,*item->second))return p::boolean(false).release();
    const auto&e=*item->second;if(!e.actor->visible&&e.actor->alive)return p::boolean(false).release();
    try{const auto box=worldBounds(o,e);return p::boolean(!box||bool(entryDistance(*box,xyz(start),xyz(end)))).release();}
    catch(const std::exception&){PyErr_Clear();return p::boolean(true).release();}
}catch(const std::exception&e){return fail(e);}}
PyObject* collisions(PyObject*,PyObject*args){PyObject*c,*entity,*start,*end;if(!PyArg_ParseTuple(args,"OOOO",&c,&entity,&start,&end))return nullptr;try{
    auto&o=owner(c);if(!active(o))return p::retain(Py_None).release();const auto found=o.objects.find(entity);if(found==o.objects.end())return p::retain(Py_None).release();
    const auto item=o.entries.find(found->second);if(item==o.entries.end()||!current(o,*item->second)||!item->second->geometry)return p::retain(Py_None).release();
    const auto&e=*item->second;const auto a=xyz(start),b=xyz(end);auto out=p::list();const auto box=worldBounds(o,e);if(box&&!entryDistance(*box,a,b))return out.release();
    if(o.collision_type.get()==Py_None)throw p::Error("Native collision publication type is unavailable");
    ++o.bsp_tests;for(const auto&hit:collisionRows(*e.geometry->vehicle,*e.actor,a,b)){
        if(hit.component>=e.geometry->material_maps.size())throw p::Error("Native collision component index is invalid");
        auto key=p::integer(hit.material);auto*map=e.geometry->material_maps[hit.component].get();
        auto*material=PyDict_Check(map)?PyDict_GetItem(map,key.get()):nullptr;
        const auto*shape=e.geometry->vehicle.get();
#if defined(WOT_CLIENT_QUERY_HOST_BUILD)
        extern const char* componentName(const geo::Vehicle&,unsigned);
        auto name=p::text(componentName(*shape,hit.component));
#else
        auto name=p::text(shape->components[hit.component].name);
#endif
        auto distance=p::number(hit.distance),cosine=p::number(hit.cosine);
        p::Ref row(PyObject_CallFunctionObjArgs(o.collision_type.get(),distance.get(),cosine.get(),material?material:Py_None,name.get(),nullptr));
        if(!active(o))break;p::append(out.get(),row);
    }return out.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* flags(PyObject*,PyObject*args){PyObject*c,*record;if(!PyArg_ParseTuple(args,"OO",&c,&record))return nullptr;try{
    auto&o=owner(c);if(!active(o))return p::retain(Py_None).release();const auto id=p::integral(record,"engine_id");const auto found=o.entries.find(id);
    if(found!=o.entries.end()&&found->second->record.get()==record){auto&a=*found->second->actor;a.visible=p::truth(record,"spot_visible",true);a.ready=p::truth(record,"ready");a.tombstone=p::truth(record,"tombstone");}
    return p::retain(Py_None).release();
}catch(const std::exception&e){return fail(e);}}
PyObject* turrets(PyObject*,PyObject*args){PyObject*c,*rows,*order=Py_None;if(!PyArg_ParseTuple(args,"OO|O",&c,&rows,&order))return nullptr;try{
    auto&o=owner(c);auto errors=p::list();if(!active(o))return errors.release();std::vector<Turret> accepted;stock_native_bot_turret::Obstacles precise;std::map<stock_native_bot_turret::Owner,std::shared_ptr<const void>> resources;std::vector<PhysicalTurret> physical;
    if(!PyDict_Check(rows))throw p::Error("Native target turret table is invalid");Py_ssize_t cursor=0;PyObject*key,*raw;
    while(PyDict_Next(rows,&cursor,&key,&raw))try{projectileTurret(o,raw,precise,resources,physical);auto box=field(raw,"target_bounds");if(box.get()==Py_None)continue;
        auto low=p::item(box.get(),0),high=p::item(box.get(),1);Turret t;t.bounds={xyz(low.get()),xyz(high.get())};t.settles_at=p::numeric(raw,"settles_at_ms");
        entryDistance(t.bounds,t.bounds.minimum,t.bounds.maximum);accepted.push_back(t);
    }catch(const std::exception&e){PyErr_Clear();p::append(errors.get(),failure(p::string(key),e.what()));}
    if(PyDict_Check(order)){std::vector<PhysicalTurret> ordered;Py_ssize_t cursor=0;PyObject*key,*value;while(PyDict_Next(order,&cursor,&key,&value)){const auto name=p::string(key);const auto found=std::find_if(physical.begin(),physical.end(),[&](const PhysicalTurret&t){return name==std::string(t.owner.human?"player:":"bot:")+std::to_string(t.owner.id);});if(found!=physical.end())ordered.push_back(std::move(*found));}physical=std::move(ordered);}
    o.turrets=std::move(accepted);o.projectile_turrets=std::move(precise);o.turret_geometry=std::move(resources);o.physical_turrets=std::move(physical);return errors.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* registeredVehicle(PyObject*c,std::int64_t engine_id){auto&o=owner(c);if(!active(o))return nullptr;const auto found=o.entries.find(engine_id);if(found==o.entries.end())return nullptr;const auto actor=o.state->actors.find(found->second->actor->key);return actor!=o.state->actors.end()&&actor->second==found->second->actor?found->second->entity.get():nullptr;}
OutlinePick pickOutline(PyObject*c,const Point&a,const Point&d,double maximum,double fov,std::optional<std::int64_t>held_id,std::int64_t space,double server_ms,double epsilon){
    auto&o=owner(c);OutlinePick out;if(!active(o))return out;out.available=true;++o.queries;const auto end=along(a,d,maximum);
    if(!std::isfinite(maximum)||maximum<0.||!std::isfinite(fov)||!std::isfinite(server_ms)||!std::isfinite(epsilon)||epsilon<0.)throw p::Error("Native target query parameters are invalid");
    for(const auto id:o.selection_order){const auto item=o.entries.find(id);if(item==o.entries.end())continue;const auto&e=*item->second;const auto&actor=*e.actor;if(actor.local)continue;const bool holding=held_id&&*held_id==actor.engine_id;out.held_seen=out.held_seen||holding;
        try{std::string reason;if(!current(o,e))reason="is not ready";else if(!actor.visible)reason="is not spotted";else if(!actor.alive||actor.dead)reason="is destroyed";
            const Point position{actor.current[0],actor.current[1],actor.current[2]};const auto offset=subtract(position,a);const double distance=length(offset);
            if(reason.empty()&&distance>maximum)reason="is past range";
            if(!reason.empty()){if(holding)out.held_reason=reason;if(!out.decline)out.decline=OutlineDecline{actor.engine_id,reason};continue;}
            double bearing=0.;if(distance>0.)bearing=std::acos(std::max(-1.,std::min(1.,(offset[0]*d[0]+offset[1]*d[1]+offset[2]*d[2])/distance)))*180./pi;
            const auto radius=e.geometry?hullRadius(*e.geometry->vehicle):std::nullopt;
            const double angle=radius?std::max(0.,bearing-std::atan2(*radius,distance)*180./pi):0.;
            const auto hit=angle<=fov?nearest(o,e,a,end):std::nullopt;
            if(!hit){if(holding)out.held_reason="is not under the cursor";if(!out.miss||angle<out.miss->angle)out.miss=OutlineMiss{angle,actor.engine_id,distance};continue;}
            if(!out.depth||*hit<*out.depth){out.depth=*hit;out.chosen=actor.engine_id;}
        }catch(const std::exception&error){PyErr_Clear();out.errors.push_back({actor.key,error.what()});}
    }
    if(out.chosen&&out.depth){const auto target_end=along(a,d,*out.depth);std::string reason;
        std::optional<double> world;
        try{world=scenery(space,a,target_end);}catch(const std::exception&error){PyErr_Clear();out.errors.push_back({"scenery",error.what()});reason="has no scenery query";}
        if(world&&*world+epsilon<*out.depth)reason="is behind scenery";
        if(reason.empty())for(const auto&item:o.entries){const auto&e=*item.second;if(e.actor->local||e.actor->alive||!current(o,e))continue;try{const auto hit=nearest(o,e,a,target_end);if(hit&&*hit+epsilon<*out.depth){reason="is behind a wreck";break;}}catch(const std::exception&error){PyErr_Clear();out.errors.push_back({e.actor->key,error.what()});}}
        if(reason.empty())for(const auto&t:o.turrets){if(server_ms<t.settles_at)continue;const auto hit=entryDistance(t.bounds,a,target_end);if(hit&&*hit+epsilon<*out.depth){reason="is behind a thrown turret";break;}}
        if(!reason.empty()){out.blocked=OutlineDecline{*out.chosen,reason};out.decline=out.blocked;if(held_id&&*held_id==*out.chosen)out.held_reason=reason;out.chosen.reset();out.depth.reset();}
    }return out;
}
PyObject* outline(PyObject*,PyObject*args){PyObject*c,*start,*direction,*held;double maximum,fov,epsilon,server_ms;long space;
    if(!PyArg_ParseTuple(args,"OOOddOldd",&c,&start,&direction,&maximum,&fov,&held,&space,&server_ms,&epsilon))return nullptr;try{
    const auto pick=pickOutline(c,xyz(start),xyz(direction),maximum,fov,held==Py_None?std::nullopt:std::optional<std::int64_t>(p::wide(held)),space,server_ms,epsilon);if(!pick.available)return p::retain(Py_None).release();
    auto out=p::dict();p::set(out.get(),"chosen",pick.chosen?p::integer(*pick.chosen):p::retain(Py_None));p::set(out.get(),"depth",pick.depth?p::number(*pick.depth):p::retain(Py_None));
    p::set(out.get(),"held_seen",p::boolean(pick.held_seen));p::set(out.get(),"held_reason",pick.held_reason.empty()?p::retain(Py_None):p::text(pick.held_reason));
    const auto decline=[](const std::optional<OutlineDecline>&v){return v?tuple2(p::integer(v->engine_id),p::text(v->reason)):p::retain(Py_None);};
    auto miss=p::retain(Py_None);if(pick.miss){auto angle=p::number(pick.miss->angle),id=p::integer(pick.miss->engine_id),distance=p::number(pick.miss->distance);miss.~Ref();new(&miss)p::Ref(PyTuple_Pack(3,angle.get(),id.get(),distance.get()));}
    auto errors=p::list();for(const auto&error:pick.errors)p::append(errors.get(),failure(error.entity,error.error));p::set(out.get(),"miss",miss);p::set(out.get(),"decline",decline(pick.decline));p::set(out.get(),"blocked",decline(pick.blocked));p::set(out.get(),"errors",errors);return out.release();
}catch(const std::exception&e){return fail(e);}}

PyObject* statistics(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{
    const auto&o=owner(c);auto out=p::dict();
    for(const auto&row:std::initializer_list<std::pair<const char*,std::uint64_t>>{{"cold_installs",o.cold_installs},{"entries",o.entries.size()},{"queries",o.queries},{"bounds_tests",o.bounds_tests},{"bsp_tests",o.bsp_tests}})p::set(out.get(),row.first,p::integer(row.second));
    p::set(out.get(),"closed",p::boolean(o.closed));return out.release();
}catch(const std::exception&e){return fail(e);}}
PyObject* close(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{auto&o=owner(c);o.closed=true;
    for(auto&entry:o.entries)if(entry.second->actor->geometry.get()==entry.second->geometry.get())entry.second->actor->geometry.reset();
    o.entries.clear();o.objects.clear();o.turrets.clear();o.projectile_turrets.clear();o.physical_turrets.clear();o.turret_geometry.clear();o.selection_order.clear();o.candidate_order.clear();return p::retain(Py_None).release();
}catch(const std::exception&e){return fail(e);}}

// Battle-owned query ingress: vehicle readiness, tombstones and engine ids come from the native vehicle table.
PyObject* createFor(native_battle::Battle& b){
    p::Ref module(PyImport_ImportModule("gui.mods.offline_lan_0922.entities.remote_vehicle"));
    auto collision_type=field(module.get(),"_SegmentCollisionResultExt");
    auto o=std::make_unique<Owner>(b.vehicles,collision_type.get());p::Ref capsule(PyCapsule_New(o.get(),capsule_name,destroy));o.release();return capsule.release();
}
std::vector<OutlineFailure> ingestFor(PyObject*c,native_battle::Battle& b){
    auto&o=owner(c);std::vector<OutlineFailure> errors;if(o.closed||o.state->retired)return errors;
    if(o.state!=b.vehicles)throw p::Error("Native client query state identity changed");
    if(o.epoch!=o.state->epoch){o.entries.clear();o.objects.clear();o.turrets.clear();o.projectile_turrets.clear();o.physical_turrets.clear();o.turret_geometry.clear();o.selection_order.clear();o.candidate_order.clear();o.epoch=o.state->epoch;}
    std::set<std::int64_t> seen;
    for(const auto&item:o.state->actors){auto a=item.second;
        if(a->missing||a->tombstone||!a->ready||a->engine_id<=0)continue;
        const auto id=a->engine_id;seen.insert(id);try{
            auto entity=b.entity(id);
            if(entity.get()==Py_None||!engineOwns(id,entity.get())){o.entries.erase(id);a->geometry.reset();continue;}
            auto d=field(entity.get(),"typeDescriptor"),ap=field(entity.get(),"appearance");
            auto resource_stamp=resourceStamp(b.engine.shell.get(),entity.get(),d.get());
            auto found=o.entries.find(id);
            if(found!=o.entries.end()&&found->second->actor==a&&found->second->entity.get()==entity.get()&&
                found->second->descriptor.get()==d.get()&&found->second->appearance.get()==ap.get()&&found->second->resource_stamp==resource_stamp&&found->second->descriptor_revision==a->descriptor_revision&&found->second->geometry)continue;
            o.entries.erase(id);a->geometry.reset();
            auto entry=std::make_unique<Entry>(a,entity.get(),ap.get(),d.get(),Py_None);entry->descriptor_revision=a->descriptor_revision;entry->resource_stamp=std::move(resource_stamp);
            auto cached=std::make_shared<CachedGeometry>(d.get());cached->revision=a->descriptor_revision;
            try{cached->vehicle=readGeometry(d.get(),entity.get(),a->local,a->descriptor_revision);
                for(const char*name:{"chassis","hull","turret","gun"}){auto component=field(d.get(),name);cached->material_maps.push_back(field(component.get(),"materials"));}
            }catch(const std::exception&error){PyErr_Clear();cached.reset();errors.push_back({a->key,error.what()});}
            if(!active(o))break;
            entry->geometry=cached;a->geometry=cached;o.entries[id]=std::move(entry);if(cached)++o.cold_installs;
        }catch(const std::exception&e){PyErr_Clear();o.entries.erase(id);a->geometry.reset();errors.push_back({a->key,e.what()});}
    }
    for(auto i=o.entries.begin();i!=o.entries.end();)if(!seen.count(i->first))i=o.entries.erase(i);else ++i;
    o.objects.clear();for(const auto&item:o.entries)o.objects[item.second->entity.get()]=item.first;
    o.selection_order.clear();for(const auto&item:o.state->actors)if(o.entries.count(item.second->engine_id))o.selection_order.push_back(item.second->engine_id);
    auto arena=field(b.engine.avatar.get(),"arena"),roster=field(arena.get(),"vehicles");
    o.candidate_order.clear();Py_ssize_t cursor=0;PyObject*key,*value;
    if(PyDict_Check(roster.get()))while(PyDict_Next(roster.get(),&cursor,&key,&value)){const auto id=p::wide(key);if(o.entries.count(id))o.candidate_order.push_back(id);}
    else o.candidate_order=o.selection_order;
    return errors;
}
}
