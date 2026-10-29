#pragma once
#include "stock_native_client_drive_runtime.hpp"
#include "stock_native_client_state.hpp"
struct _object;
namespace stock_native_client_drive {
std::vector<unsigned> readPlayerHazards(_object*);
PlayerDescriptor readPlayerDescriptor(_object* battle,_object* entity,_object* exact_physics);
PlayerState readPlayerInitialState(_object* battle,const stock_native_client_state::Actor&);
Controls readPlayerInitialControls(_object* battle);
}
