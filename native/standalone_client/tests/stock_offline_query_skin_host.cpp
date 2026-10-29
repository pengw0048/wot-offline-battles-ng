#include "stock_offline_query_skin.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace stock_offline_query;
namespace
{
Point point()
{
    Point value{};
    for (auto & component : value) std::cin >> component;
    return value;
}
std::size_t count() { std::size_t value; std::cin >> value; return value; }
Key key() { std::uint32_t chunk, item; std::cin >> chunk >> item; return Key(chunk, item); }
void instances(std::map<Key, SkinInstance> & output)
{
    const auto size = count();
    for (std::size_t i = 0; i < size; ++i)
    {
        const auto identity = key();
        int kind; std::cin >> kind;
        SkinInstance instance; instance.kind = static_cast<SkinKind>(kind);
        const auto boxes = count();
        for (std::size_t j = 0; j < boxes; ++j)
        {
            Box box; box.center = point();
            const auto axes = count();
            for (std::size_t k = 0; k < axes; ++k) box.axes.push_back(point());
            std::cin >> box.material;
            instance.boxes.push_back(std::move(box));
        }
        output.emplace(identity, std::move(instance));
    }
}
void keys(std::set<Key> & output)
{
    const auto size = count();
    for (std::size_t i = 0; i < size; ++i) output.insert(key());
}
void chunks(std::set<std::uint32_t> & output)
{
    const auto size = count();
    for (std::size_t i = 0; i < size; ++i) { std::uint32_t chunk; std::cin >> chunk; output.insert(chunk); }
}
void bins(std::map<Cell, std::set<Key>> & output)
{
    const auto size = count();
    for (std::size_t i = 0; i < size; ++i)
    {
        int x, z; std::cin >> x >> z;
        keys(output[Cell(x, z)]);
    }
}
void materials(std::set<MaterialKey> & output)
{
    const auto size = count();
    for (std::size_t i = 0; i < size; ++i)
    {
        const auto identity = key();
        std::int64_t material; std::cin >> material;
        output.emplace(identity.first, identity.second, material);
    }
}
double distance(void *, const Point & a, const Point & b)
{
    const float x = float(a[0]) - float(b[0]);
    const float y = float(a[1]) - float(b[1]);
    const float z = float(a[2]) - float(b[2]);
    return std::sqrt(x * x + y * y + z * z);
}
Point bound(void *, const Point & start, const Point & end, double factor)
{
    Point result{};
    for (unsigned i = 0; i < 3; ++i)
    {
        const float delta = float(end[i]) - float(start[i]);
        const float scaled = delta * float(factor);
        result[i] = float(start[i]) + scaled;
    }
    return result;
}
}

int main()
{
    std::cout << std::setprecision(17);
    const auto cases = count();
    for (std::size_t c = 0; c < cases; ++c)
    {
        SkinTuning tuning; std::cin >> tuning.ray_epsilon >> tuning.point_padding >> tuning.bin_metres;
        const Point hit = point(), start = point(), end = point();
        const bool hasNormal = count() != 0;
        const Point normal = point();
        std::vector<Surface> surfaces;
        const auto size = count();
        for (std::size_t i = 0; i < size; ++i)
        {
            Surface surface;
            std::cin >> surface.material >> surface.flags >> surface.item >> surface.chunk;
            surfaces.push_back(surface);
        }
        SkinView view;
        instances(view.instances); instances(view.baked_instances);
        keys(view.tree_instances); keys(view.isolated_slots); keys(view.excluded_instances);
        chunks(view.isolated_chunks); chunks(view.layout_repairs);
        const auto generations = count();
        for (std::size_t i = 0; i < generations; ++i)
        {
            std::uint32_t chunk; std::int64_t generation; std::cin >> chunk >> generation;
            view.layout_generations[chunk] = generation;
        }
        bins(view.contact_bins); bins(view.baked_shot_bins);
        materials(view.destroyed); materials(view.speculative);
        if (!std::cin) throw std::runtime_error("Malformed skin test input");
        const SkinMath math{nullptr, distance, bound};
        const auto result = motionSkin(view, hit, start, end, hasNormal ? &normal : nullptr,
            surfaces, tuning, math);
        std::cout << result.found << ' ' << result.distance << ' ' << result.excluded.size();
        for (const auto & surface : result.excluded)
            std::cout << ' ' << surface.material << ' ' << surface.flags << ' ' << surface.item << ' ' << surface.chunk;
        std::cout << '\n';
    }
}
