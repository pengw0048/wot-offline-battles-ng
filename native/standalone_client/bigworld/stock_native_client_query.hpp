#pragma once
#include "stock_native_client_state.hpp"
#include "stock_native_detached_turret_runtime.hpp"
#include <memory>
struct _object;
namespace stock_native_world {struct TurretSweep;}
namespace stock_native_bot_turret {struct MovingDescriptor;}
namespace stock_native_bot_geometry {struct Vehicle;}
namespace native_battle {struct Battle;}
namespace stock_native_client_query {
struct OutlineMiss {double angle=0.;std::int64_t engine_id=0;double distance=0.;};
struct OutlineDecline {std::int64_t engine_id=0;std::string reason;};
struct OutlineFailure {std::string entity,error;};
struct OutlinePick {
    bool available=false,held_seen=false;
    std::optional<std::int64_t>chosen;
    std::optional<double>depth;
    std::string held_reason;
    std::optional<OutlineMiss>miss;
    std::optional<OutlineDecline>decline,blocked;
    std::vector<OutlineFailure>errors;
};
// The caller retains its cold-acquired query capsule. No Python DTO or
// interpreter predicate is created while selecting a cursor target.
OutlinePick pickOutline(_object* capsule,const stock_native_client_state::Point&start,
    const stock_native_client_state::Point&direction,double maximum,double fov,
    std::optional<std::int64_t>held,std::int64_t space,double server_ms,double epsilon);
// Borrowed logical Vehicle installed by the query resource lifecycle. Callers
// retain the capsule and the returned object across any stock presentation edge.
_object* registeredVehicle(_object*capsule,std::int64_t engine_id);
struct PhysicalTurret {
    stock_native_bot_turret::Owner owner;
    stock_native_detached_turret::Frame frame;
    std::vector<stock_native_detached_turret::Component> components;
};
class ProjectileTurrets {
public:
    virtual ~ProjectileTurrets()=default;
    virtual bool active()const=0;
    virtual std::size_t count()const=0;
    virtual const std::vector<PhysicalTurret>& physical()const=0;
    virtual bool clear(const stock_native_world::TurretSweep&,
        const stock_native_bot_turret::MovingDescriptor&,double server_ms)const=0;
    virtual std::optional<double> block(const stock_native_client_state::Point&,
        const stock_native_client_state::Point&,double end_server_ms,
        std::optional<double> start_server_ms={})=0;
};
// Cold acquisition retains this query owner and its detached component BSPs.
std::shared_ptr<ProjectileTurrets> projectileTurrets(native_battle::Battle& battle);
// The query lifecycle installs this geometry once per actor/descriptor owner.
// Consumers retain Actor.geometry while using the returned native resource.
const stock_native_bot_geometry::Vehicle* geometry(const stock_native_client_state::Actor&);
// Borrowed cold-install descriptor; never use it from a movement/query loop.
_object* descriptor(const stock_native_client_state::Actor&);
_object* create(_object*,_object*);
_object* ingest(_object*,_object*);
_object* candidates(_object*,_object*);
_object* segmentMayHit(_object*,_object*);
_object* outline(_object*,_object*);
_object* collisions(_object*,_object*);
_object* flags(_object*,_object*);
_object* turrets(_object*,_object*);
_object* close(_object*,_object*);
_object* statistics(_object*,_object*);
}
