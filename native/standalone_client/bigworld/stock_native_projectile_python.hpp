#pragma once
#include <cstdint>
#include <memory>
struct _object;
namespace stock_native_world {struct Registry;class Scene;class WorldEvents;}
namespace stock_native_bot_geometry {class Vehicles;}
namespace stock_native_bot {class Runtime;class NativeServices;}
namespace stock_native_projectile {
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
    bool install(_object* battle);
    bool observe(_object* message,double now);
    bool ingress(_object* message,double now);
    bool event(_object* event,double now);
    _object* launches(_object* accepted_bot_message);
    bool launchResult(std::int64_t bot,std::int64_t sequence,bool accepted);
    // Returns (serial, kind, plain wire) rows. Only the external transport sends
    // these; acceptance is reported through acknowledge before the next frame.
    _object* advance(_object* battle,double now,bool force_progress=false);
    bool acknowledge(std::uint64_t serial,bool sent);
    _object* statistics()const;
};
}
