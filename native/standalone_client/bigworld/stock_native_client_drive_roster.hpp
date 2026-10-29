#pragma once
#include "offline_contact_roster.h"
#include <optional>
namespace stock_native_client_contact_roster {
// The visible player resolves only pairs containing its own body. Keep the
// fixed release's four sequential normal/angular passes and reciprocal output;
// the worker's unanchored cohort resolver also solves remote/remote pairs.
std::vector<offline_contact::Row> resolve(const std::vector<offline_contact::Body>&,
    const std::vector<std::int64_t>&,double,
    const std::vector<std::pair<std::int64_t,std::int64_t>>&,std::int64_t anchor);
struct HeldFace {std::array<double,2>normal{},point{};double depth=0.;};
std::optional<std::array<double,2>> overlapPoint(const offline_contact::Body&,const offline_contact::Body&);
std::optional<HeldFace> heldFace(const offline_contact::Body&,const offline_contact::Body&,double omega);
std::vector<offline_contact::Row> traverseOnly(const std::vector<offline_contact::Body>&,double,std::int64_t anchor);
}
