#pragma once
#include <optional>
struct _object;
namespace native_battle {struct Battle;}
namespace stock_native_client_drive {
class PlayerDriver;
struct SliceTrace;
// Owns the one local matrix/overlay, camera motion and native appearance feed.
// The first ready call cold-installs resource references; subsequent slices
// consume only the shared driver and native providers.
void selfPresentation(native_battle::Battle&,_object* entity,PlayerDriver&,
    double elapsed,const SliceTrace&,bool stopped);
void retireSelfPresentation(native_battle::Battle&);
// Relative native yaw against the same stabilized body used by the gun.
std::optional<double> selfAimYaw(native_battle::Battle&,_object* entity,const PlayerDriver&);
}
