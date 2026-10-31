#pragma once
#include <Python.h>
#include <optional>
namespace native_battle {struct Battle;}
namespace stock_native_client_input {
// Final checkpoint immediately before every admitted movement or fire intent.
// Native time and current shared driver/gun owners determine the complete DTO.
bool sendCurrent(native_battle::Battle&,std::optional<bool> siege={});
bool sendCurrentAt(native_battle::Battle&,double now,std::optional<bool> siege={});
// Preserve committed world-contact barriers before any later pose input.
bool flushBarrierInputsAt(native_battle::Battle&,double now);
// Admit at most one landing after its current-pose input has been queued.
bool flushLandingAt(native_battle::Battle&,double now);
}
