#pragma once
#include <cstdint>
#include <memory>
struct _object;
namespace stock_native_bot {class Runtime;class NativeServices;}
namespace stock_native_world {struct Registry;class Scene;}
namespace stock_native_bot_geometry {class Vehicles;}
namespace stock_native_detached_turret {
class PythonOwner {
    class Impl;
    std::unique_ptr<Impl> impl_;
public:
    PythonOwner(stock_native_bot::Runtime&,stock_native_world::Registry&,
        stock_native_world::Scene&,stock_native_bot_geometry::Vehicles&,
        stock_native_bot::NativeServices&,std::uint16_t vehicle_skip_flags);
    ~PythonOwner();
    bool install(_object* battle);
    bool queue(_object* record,_object* entity,_object* state,double now);
    bool ingress(_object* message,double now);
    // None or a dictionary containing detached_turrets for the original
    // terminal send_bot_state([], detached_turrets=...) transport boundary.
    _object* advance(_object* battle,double now);
    _object* rows()const;
    _object* takeErrors();
};
}
