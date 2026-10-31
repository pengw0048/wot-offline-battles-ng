#pragma once
#include "stock_native_projectile_wire.hpp"
#include <cstdint>
#include <memory>
#include <vector>
struct _object;
namespace native_battle {struct Battle;}
namespace stock_native_world {struct Registry;class Scene;class WorldEvents;}
namespace stock_native_bot_geometry {class Vehicles;}
namespace stock_native_bot {class Runtime;class NativeServices;}
namespace stock_native_projectile {
// One send_projectile_launch message ({"type":"projectile_launch", its parameters by name}).
struct LaunchMessage {std::int64_t bot=0,sequence=0;stock_native_client_state::Value message;};
// Every entry point but install and statistics throws on a malformed input or stale owner.
class NativeProjectiles {
    struct State;
    std::unique_ptr<State> state_;
public:
    NativeProjectiles(stock_native_world::Registry&,stock_native_world::Scene&,
        stock_native_bot_geometry::Vehicles&,stock_native_world::WorldEvents&,
        stock_native_bot::Runtime&,stock_native_bot::NativeServices&);
    ~NativeProjectiles();
    NativeProjectiles(const NativeProjectiles&)=delete;
    NativeProjectiles&operator=(const NativeProjectiles&)=delete;
    // Binds the round: actor table, projectile clock and fire ledger come from the native battle.
    bool install(native_battle::Battle&);
    bool observe(const stock_native_client_state::Value& message,double now);
    bool ingress(const stock_native_client_state::Value& message,double now);
    bool event(const stock_native_client_state::Value& event,double now);
    // The next launch wave of an accepted bot_state; null continues the current wave.
    std::vector<LaunchMessage> launches(const stock_native_client_state::Value* accepted_bot_message);
    bool launchResult(std::int64_t bot,std::int64_t sequence,bool accepted);
    // Only the external transport sends these; acceptance is reported through
    // acknowledge before the next frame.
    std::vector<WireDispatch> advance(native_battle::Battle&,double now,bool force_progress=false);
    bool acknowledge(std::uint64_t serial,bool sent);
    _object* statistics()const;
};
}
