#include "stock_offline_query_skin.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_offline_query
{
namespace
{
namespace geometry = offline_destructible;

bool isolated(const SkinView & view, const Key & key)
{
    return view.isolated_chunks.count(key.first) ||
        view.isolated_slots.count(key) || view.excluded_instances.count(key);
}

bool broken(const SkinView & view, const Key & key, std::int64_t material)
{
    const MaterialKey exact(key.first, key.second, material);
    return view.destroyed.count(exact) ||
        view.destroyed.count(MaterialKey(key.first, key.second, -1)) ||
        view.speculative.count(exact);
}

bool hasMaterial(const SkinInstance & instance, std::int64_t material)
{
    for (const auto & box : instance.boxes)
        if (box.material == material) return true;
    return false;
}

Cell cell(const Point & point, double metres)
{
    const double x = std::floor(point[0] / metres);
    const double z = std::floor(point[2] / metres);
    if (!std::isfinite(x) || !std::isfinite(z) ||
        x < std::numeric_limits<int>::min() || x > std::numeric_limits<int>::max() ||
        z < std::numeric_limits<int>::min() || z > std::numeric_limits<int>::max())
        throw std::out_of_range("Native motion skin bin is outside the typed domain");
    return Cell(static_cast<int>(x), static_cast<int>(z));
}

void appendCell(const std::map<Cell, std::set<Key>> & bins, const Cell & at,
    std::set<Key> & result)
{
    const auto found = bins.find(at);
    if (found != bins.end()) result.insert(found->second.begin(), found->second.end());
}

void appendBounds(const std::map<Cell, std::set<Key>> & bins,
    const Point & first, const Point & last, double metres, std::set<Key> & result)
{
    const Cell a = cell(first, metres), b = cell(last, metres);
    // The law takes a set union. Visit populated X rows in the rectangle,
    // without scanning the rest of the map or iterating empty distant cells.
    const int left = std::min(a.first, b.first), right = std::max(a.first, b.first);
    const int back = std::min(a.second, b.second), front = std::max(a.second, b.second);
    auto entry = bins.lower_bound(Cell(left, back));
    while (entry != bins.end() && entry->first.first <= right)
    {
        const int x = entry->first.first;
        if (entry->first.second < back)
        {
            entry = bins.lower_bound(Cell(x, back));
            continue;
        }
        if (entry->first.second > front)
        {
            if (x == std::numeric_limits<int>::max()) break;
            entry = bins.lower_bound(Cell(x + 1, back));
            continue;
        }
        result.insert(entry->second.begin(), entry->second.end());
        ++entry;
    }
}

class Witness
{
    const Point & point_;
    const Point & start_;
    const Point & end_;
    const Point * normal_;
    double padding_;
    std::map<const Box *, geometry::MotionWitness> rows_;
    std::map<const SkinInstance *, std::pair<bool, Box>> envelopes_;
public:
    Witness(const Point & point, const Point & start, const Point & end,
        const Point * normal, double padding) :
        point_(point), start_(start), end_(end), normal_(normal), padding_(padding) {}

    const geometry::MotionWitness & at(const Box & box)
    {
        const auto existing = rows_.find(&box);
        if (existing != rows_.end()) return existing->second;
        const auto row = geometry::motion_witnesses(std::vector<Box>(1, box),
            point_, start_, end_, padding_, normal_);
        return rows_.emplace(&box, row.front()).first->second;
    }

    const Box * envelope(const SkinInstance & instance)
    {
        auto existing = envelopes_.find(&instance);
        if (existing == envelopes_.end())
        {
            Box box;
            const bool valid = geometry::motion_envelope(instance.boxes, box);
            existing = envelopes_.emplace(&instance,
                std::make_pair(valid, std::move(box))).first;
        }
        return existing->second.first ? &existing->second.second : nullptr;
    }
};

struct Owner
{
    Key key;
    const SkinInstance * instance;
    geometry::Interval interval;
};

SkinResult componentSkin(const SkinView & view, const Point & point,
    const Point & start, const Point & end, const std::set<Surface> & surfaces,
    const SkinTuning & tuning, const SkinMath & math, Witness & witness)
{
    SkinResult result;
    const double length = math.distance(math.context, end, start);
    if (length <= tuning.ray_epsilon) return result;
    const double hitDistance = math.distance(math.context, point, start);
    double limit = 1.;
    for (const auto & surface : surfaces)
    {
        if (surface.material < 71 || surface.material > 86 || surface.flags != 0)
            continue;
        const Key key = surface.key();
        const auto found = view.instances.find(key);
        if (found == view.instances.end() || found->second.kind != SkinKind::Structure ||
            isolated(view, key) || view.layout_repairs.count(key.first)) continue;
        const auto & instance = found->second;
        if (!hasMaterial(instance, surface.material)) continue;
        std::vector<const Box *> containing;
        for (const auto & box : instance.boxes)
            if (witness.at(box).probe.inside) containing.push_back(&box);
        if (containing.empty()) continue;
        bool accepted = true;
        double localLimit = 1.;
        for (const auto * box : containing)
        {
            const auto & interval = witness.at(*box).probe.segment;
            if (!broken(view, key, box->material) || std::int64_t(box->material) == std::int64_t(surface.material) ||
                !interval.valid) { accepted = false; break; }
            localLimit = std::min(localLimit, interval.leave);
        }
        if (!accepted) continue;
        for (const auto & box : instance.boxes)
        {
            if (broken(view, key, box.material)) continue;
            const auto & interval = witness.at(box).probe.segment;
            if (interval.valid && interval.leave * length >= hitDistance)
                localLimit = std::min(localLimit,
                    std::max(0., interval.entry - 2. * tuning.ray_epsilon / length));
        }
        if (localLimit * length + 1.e-7 < hitDistance) continue;
        limit = std::min(limit, localLimit);
        result.excluded.insert(surface);
    }
    result.found = !result.excluded.empty();
    if (result.found) result.distance = limit * length;
    return result;
}

bool componentWitness(Witness & witness, const SkinInstance & instance,
    const Surface & surface, bool projected)
{
    for (const auto & box : instance.boxes)
    {
        if (instance.kind == SkinKind::Structure && std::int64_t(box.material) != std::int64_t(surface.material))
            continue;
        const auto & row = witness.at(box);
        if (row.probe.inside || ((projected || row.side) && row.footprint)) return true;
    }
    return false;
}
} // namespace

bool anonymousOriginal(const SkinView & view, const Surface & surface)
{
    return 71 <= surface.material && surface.material <= 86 && (surface.flags & 0x80) &&
        !view.instances.count(surface.key()) && !view.baked_instances.count(surface.key()) &&
        !view.tree_instances.count(surface.key());
}

SkinResult motionSkin(const SkinView & view, const Point & point,
    const Point & start, const Point & end, const Point * normal,
    const std::vector<Surface> & candidates, const SkinTuning & tuning, const SkinMath & math)
{
    if (!math.distance || !math.bound || !(tuning.ray_epsilon > 0.) ||
        !(tuning.point_padding >= 0.) || !(tuning.bin_metres > 0.))
        throw std::invalid_argument("Invalid native motion skin contract");
    const std::set<Surface> surfaces(candidates.begin(), candidates.end());
    Witness witness(point, start, end, normal, tuning.point_padding);
    SkinResult result = componentSkin(view, point, start, end, surfaces, tuning, math, witness);
    if (result.found) return result;
    std::set<Surface> aliases, remapped;
    for (const auto & surface : surfaces)
    {
        if (anonymousOriginal(view, surface)) aliases.insert(surface);
        const auto generation = view.layout_generations.find(surface.chunk);
        if (71 <= surface.material && surface.material <= 86 && surface.flags == 0 &&
            generation != view.layout_generations.end() && generation->second > 0 &&
            !view.layout_repairs.count(surface.chunk)) remapped.insert(surface);
    }
    if (aliases.empty() && remapped.empty()) return SkinResult();
    std::set<Key> members;
    appendCell(view.contact_bins, cell(point, tuning.bin_metres), members);
    std::vector<Owner> owners;
    for (const auto & key : members)
    {
        const auto found = view.instances.find(key);
        if (found == view.instances.end() || isolated(view, key)) continue;
        const auto & instance = found->second;
        if (instance.kind != SkinKind::Structure && instance.kind != SkinKind::Fragile &&
            instance.kind != SkinKind::Falling) continue;
        const auto * envelope = witness.envelope(instance);
        if (!envelope) continue;
        const auto & row = witness.at(*envelope);
        if (row.probe.inside && row.probe.segment.valid)
            owners.push_back(Owner{key, &instance, row.probe.segment});
    }
    for (const auto & surface : remapped)
    {
        const Key wire = surface.key();
        bool actualOwner = false;
        for (const auto & owner : owners) actualOwner = actualOwner || owner.key == wire;
        if (actualOwner || view.tree_instances.count(wire)) continue;
        auto registered = view.instances.find(wire);
        const SkinInstance * instance = registered == view.instances.end() ? nullptr : &registered->second;
        if (!instance)
        {
            const auto baked = view.baked_instances.find(wire);
            if (baked != view.baked_instances.end()) instance = &baked->second;
        }
        if (instance)
        {
            const auto * envelope = witness.envelope(*instance);
            if (envelope && witness.at(*envelope).probe.inside) continue;
        }
        bool inside = false;
        for (const auto & owner : owners)
            if (owner.key.first == wire.first)
                for (const auto & box : owner.instance->boxes)
                    inside = inside || witness.at(box).probe.inside;
        if (inside) aliases.insert(surface);
    }
    if (aliases.empty()) return SkinResult();
    bool needsProjection = false;
    for (const auto & surface : aliases)
    {
        bool matches = false;
        for (const auto & owner : owners)
            matches = matches || componentWitness(witness, *owner.instance, surface, false);
        needsProjection = needsProjection || !matches;
    }
    bool projected = false;
    const double dx = end[0] - start[0], dz = end[2] - start[2];
    if (needsProjection && normal && dx * dx + dz * dz > 1.e-12)
    {
        std::set<Key> possible = members;
        appendCell(view.baked_shot_bins, cell(point, tuning.bin_metres), possible);
        std::vector<Owner> projectedOwners;
        bool hasSideFace = false;
        for (const auto & key : possible)
        {
            const auto live = view.instances.find(key);
            const auto baked = view.baked_instances.find(key);
            const SkinInstance * instance = live != view.instances.end() ? &live->second :
                baked != view.baked_instances.end() ? &baked->second : nullptr;
            if (!instance) continue;
            const auto * envelope = witness.envelope(*instance);
            if (!envelope || !witness.at(*envelope).footprint) continue;
            if (live == view.instances.end() || isolated(view, key)) return SkinResult();
            const auto & row = witness.at(*envelope);
            hasSideFace = hasSideFace || row.side;
            if (row.probe.projected.valid)
                projectedOwners.push_back(Owner{key, instance, row.probe.projected});
        }
        if (hasSideFace) { projected = true; owners = std::move(projectedOwners); }
    }
    if (owners.empty()) return SkinResult();
    result = SkinResult();
    for (const auto & surface : aliases)
    {
        std::vector<const Owner *> componentOwners;
        for (const auto & owner : owners)
            if (componentWitness(witness, *owner.instance, surface, projected))
                componentOwners.push_back(&owner);
        if (componentOwners.empty())
            for (const auto & owner : owners) componentOwners.push_back(&owner);
        bool accepted = true;
        for (const auto * owner : componentOwners)
        {
            const std::int64_t material = owner->instance->kind == SkinKind::Structure ?
                std::int64_t(surface.material) : -1;
            if (!hasMaterial(*owner->instance, material) || !broken(view, owner->key, material))
            { accepted = false; break; }
        }
        if (accepted) result.excluded.insert(surface);
    }
    if (result.excluded.empty()) return SkinResult();
    double limit = 1.;
    for (const auto & owner : owners) limit = std::min(limit, owner.interval.leave);
    const Point bound = math.bound(math.context, start, end, limit);
    std::set<Key> neighbours;
    appendBounds(view.contact_bins, start, bound, tuning.bin_metres, neighbours);
    if (projected) appendBounds(view.baked_shot_bins, start, bound, tuning.bin_metres, neighbours);
    const double length = math.distance(math.context, end, start);
    const double hitDistance = math.distance(math.context, point, start);
    for (const auto & key : neighbours)
    {
        const auto live = view.instances.find(key);
        const auto baked = view.baked_instances.find(key);
        const SkinInstance * other = live != view.instances.end() ? &live->second :
            projected && baked != view.baked_instances.end() ? &baked->second : nullptr;
        if (!other) continue;
        std::set<std::int64_t> materials;
        if (other->kind == SkinKind::Structure)
            for (const auto & surface : result.excluded) materials.insert(surface.material);
        else materials.insert(-1);
        for (const auto material : materials)
        {
            if (broken(view, key, material)) continue;
            for (const auto & box : other->boxes)
            {
                if (material != -1 && box.material != material) continue;
                const auto & row = witness.at(box);
                const auto & interval = projected ? row.probe.projected : row.probe.segment;
                if (interval.valid && interval.leave * length >= hitDistance)
                    limit = std::min(limit,
                        std::max(0., interval.entry - 2. * tuning.ray_epsilon / length));
            }
        }
    }
    const double distance = limit * length;
    if (distance + 1.e-7 < hitDistance) return SkinResult();
    result.found = true;
    result.distance = distance;
    return result;
}
}
