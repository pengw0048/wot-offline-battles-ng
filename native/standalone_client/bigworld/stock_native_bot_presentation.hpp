#pragma once
#include <string>
#include <vector>
struct _object;
namespace stock_native_bot {
class NativeServices;
// Worker-only presentation batch. Matrices and stock property overlays stay
// live; all pose, velocity, interpolation and aim decisions execute natively.
bool presentNativeBotPoses(_object* battle,_object* states,double now,
    const NativeServices&,std::vector<std::string>& errors);
}
