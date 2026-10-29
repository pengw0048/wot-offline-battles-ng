#pragma once
#include "stock_native_bot_world_adapter.hpp"
#include "stock_native_destructible_backend.hpp"

struct _object;
namespace stock_native_world {
// Parse the complete plain DTO before publishing any state. This is an
// installation boundary for a fresh native space owner, never a step callback.
// False leaves a Python error set; discard the fresh owner on native failure.
bool installWorldSnapshot(_object * dto,Registry &,NativeScene &,
    stock_native_destructible::NativeBackend &);
}
