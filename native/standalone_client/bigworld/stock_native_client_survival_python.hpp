#pragma once
#include <array>
struct _object;
namespace stock_native_client_survival {
// The visible owner publishes warning flags only. The existing worker/server
// path remains the sole producer of drowning/overturn damage and death.
std::array<int,2> levels(_object* battle);
void retire(_object* battle);
_object* drowningPython(_object*,_object*);
_object* overturnPython(_object*,_object*);
_object* retirePython(_object*,_object*);
}
