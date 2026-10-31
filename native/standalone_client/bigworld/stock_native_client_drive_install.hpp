#pragma once
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_state.hpp"
struct _object;
namespace native_battle {struct Battle;}
namespace stock_native_client_drive {
std::vector<unsigned> readPlayerHazards(_object*);
PlayerDescriptor readPlayerDescriptor(const native_battle::Battle&,_object* entity,_object* exact_physics);
PlayerState readPlayerInitialState(const native_battle::Battle&,const stock_native_client_state::Actor&);
Controls readPlayerInitialControls(const native_battle::Battle&);
}
