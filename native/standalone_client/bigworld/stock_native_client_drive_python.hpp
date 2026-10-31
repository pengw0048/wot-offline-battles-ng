#pragma once
#include <memory>
struct _object;
namespace native_battle {struct Battle;namespace local {struct DriveParams;}}
namespace stock_native_world {struct Registry;struct Destruction;class NativeScene;class Pipeline;class WorldEvents;}
namespace stock_native_destructible {class NativeBackend;}
namespace stock_native_client_drive {
class PlayerDriver;class PlayerContacts;class PublicationLedger;
// Shared resource access for current-battle native consumers. The lifetime
// retains the single physical scene owner; scene.current() remains the fence
// after its explicit retirement. Consumers must never create another scene.
struct SceneAccess {
    std::shared_ptr<void> lifetime;
    stock_native_world::Registry* registry=nullptr;
    stock_native_world::NativeScene* scene=nullptr;
    stock_native_world::Pipeline* pipeline=nullptr;
    stock_native_destructible::NativeBackend* backend=nullptr;
    stock_native_world::WorldEvents* events=nullptr;
};
SceneAccess sceneAccess(const native_battle::Battle&);
// Call after native query resource ingress and before status consumers. The
// native owner caches descriptor lifetimes and accepted snapshot revisions.
void synchronizeStatus(const native_battle::Battle&,double now);
struct DriverAccess {
    std::shared_ptr<void> lifetime;
    PlayerDriver* driver=nullptr;
    PlayerContacts* contacts=nullptr;
    PublicationLedger* publication=nullptr;
};
DriverAccess driverAccess(native_battle::Battle&);
// Cold local entity/loadout boundary. All parameter and initial compatibility
// state reads happen here, before any native physical slice is admitted.
void installPlayer(native_battle::Battle&,_object* entity,const native_battle::local::DriveParams& exact_physics);
// Native projectile CAS already performed geometry/effects. This boundary
// mirrors accepted raw compatibility ledgers only and bumps their revisions.
void mirrorDestruction(const native_battle::Battle&,const stock_native_world::Destruction&);
}
