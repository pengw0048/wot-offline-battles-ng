#pragma once
#include <array>
namespace native_battle {struct Battle;}
namespace stock_native_client_survival {
// The visible owner publishes warning flags only. The existing worker/server
// path remains the sole producer of drowning/overturn damage and death.
std::array<int,2> levels(const native_battle::Battle&);
void retire(native_battle::Battle&);
// Advances one warning clock; true when the drowning level changed this frame.
bool drowning(native_battle::Battle&,double dt,double now);
bool overturn(native_battle::Battle&,double dt,double now);
}
