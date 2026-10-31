#pragma once
// The hidden worker's detached-turret owner over the native battle: launch proposals, accepted rows
// (World::turrets) and rigid turret bodies. Stock entities and descriptors stay Python objects.
#include "stock_native_bot_python.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace native_battle {struct Battle;}
namespace stock_native_client_state {struct Value;struct Actor;}
namespace stock_native_bot {class Runtime;}
namespace stock_native_world {struct Registry;class Scene;}
namespace stock_native_bot_geometry {class Vehicles;}
namespace stock_native_detached_turret {
class NativeOwner {
    class Impl;
    std::unique_ptr<Impl> impl_;
public:
    NativeOwner(stock_native_bot::Runtime&,stock_native_world::Registry&,
        stock_native_world::Scene&,stock_native_bot_geometry::Vehicles&,std::uint16_t vehicle_skip_flags);
    ~NativeOwner();
    // Binds the round and adopts the rows World::turrets accepted before this owner existed.
    void install(native_battle::Battle&);
    bool queue(native_battle::Battle&,const stock_native_client_state::Actor& vehicle,PyObject* entity,
        const stock_native_client_state::Value* state,double now);
    // Accepts newer server rows into World::turrets; false when the message carries none for this round.
    bool ingress(native_battle::Battle&,const stock_native_client_state::Value& message);
    // The rows of the terminal send_bot_state([], detached_turrets=...) when it is due, else None.
    stock_native_bot_python::Ref advance(native_battle::Battle&,double now);
    // Accepted rows with pending proposals over them, packed for the LAN wire.
    stock_native_bot_python::Ref rows()const;
    std::vector<std::string> takeErrors();
};
}
