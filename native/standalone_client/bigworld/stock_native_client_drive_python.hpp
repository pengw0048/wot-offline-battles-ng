#pragma once
#include <memory>
struct _object;
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
SceneAccess sceneAccess(_object* battle);
// Call after native query resource ingress and before status consumers. The
// native owner caches descriptor lifetimes and accepted snapshot revisions.
void synchronizeStatus(_object* battle,double now);
struct DriverAccess {
    std::shared_ptr<void> lifetime;
    PlayerDriver* driver=nullptr;
    PlayerContacts* contacts=nullptr;
    PublicationLedger* publication=nullptr;
};
DriverAccess driverAccess(_object* battle);
// Cold local entity/loadout boundary. All parameter and initial compatibility
// state reads happen here, before any native physical slice is admitted.
void installPlayer(_object* battle,_object* entity,_object* exact_physics);
// Native projectile CAS already performed geometry/effects. This boundary
// mirrors accepted raw compatibility ledgers only and bumps their revisions.
void mirrorDestruction(_object* battle,const stock_native_world::Destruction&);
}
