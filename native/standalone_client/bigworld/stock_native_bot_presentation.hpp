#pragma once
#include <string>
#include <vector>
namespace native_battle { struct Battle; }
namespace stock_native_bot {
class NativeServices;
class Runtime;
// Worker-only presentation batch from the native Bot actors. Matrices and stock property overlays
// stay live; all pose, velocity, interpolation and aim decisions execute natively. Returns the
// presented Bot count.
std::size_t presentNativeBotPoses(native_battle::Battle&,const Runtime&,double now,
    const NativeServices&,std::vector<std::string>& errors);
}
