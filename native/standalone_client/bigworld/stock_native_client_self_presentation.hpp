#pragma once
#include <optional>
struct _object;
namespace stock_native_client_drive {
class PlayerDriver;
struct SliceTrace;
// Owns the one local matrix/overlay, camera motion and native appearance feed.
// The first ready call cold-installs resource references; subsequent slices
// consume only the shared driver and native providers.
void selfPresentation(_object* battle,_object* entity,PlayerDriver&,
    double elapsed,const SliceTrace&,bool stopped);
void retireSelfPresentation(_object* battle);
// Relative native yaw against the same stabilized body used by the gun.
std::optional<double> selfAimYaw(_object* battle,_object* entity,const PlayerDriver&);
}
