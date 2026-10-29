// Execute the production type/acquire/sort bodies with CPU-only load boundaries.
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <string>
#include <vector>

static bool countAllocations = false;
static std::size_t allocations = 0;
// Keep replacement hooks opaque so GCC does not diagnose an inlined free as
// mismatched with the corresponding standard operator new call.
[[gnu::noinline]] void * operator new(std::size_t size)
{
    if (countAllocations) ++allocations;
    if (void * value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void * value) noexcept { std::free(value); }
[[gnu::noinline]] void operator delete(void * value, std::size_t) noexcept { std::free(value); }
[[gnu::noinline]] void * operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
[[gnu::noinline]] void operator delete(void * value, const std::nothrow_t &) noexcept { std::free(value); }

using uint32 = std::uint32_t;
namespace wot { struct StockTreeGeometry {}; struct StockTreeMaterials {}; struct StockTreeLodConfiguration {}; }
struct TreeWindOwner {};
struct BSPTree {};
struct TreeGpuPart { void release() {} };
@TYPE@

namespace BW { using string = std::string; using StringRef = std::string; }
BW::string engineString(const std::string & value) { return value; }
namespace BWResource { BW::StringRef getFilename(const BW::string & path) { return path.substr(path.find_last_of('/') + 1); } }
namespace wot { bool stockTreeIsBush(const std::vector<std::string> &, const std::string &) { return false; } }
static unsigned loads = 0;
static bool loadSucceeds = true;
bool loadTreeType(TreeType & type, int)
{
    ++loads;
    // The cache must already be complete before a load can publish the type.
    assert(type.sortKey == type.resource + "/" + std::to_string(type.seed));
    return loadSucceeds;
}
struct Owner
{
    bool active = true;
    std::mutex mutex;
    std::map<std::string, std::shared_ptr<TreeType>> types;
    std::deque<std::shared_ptr<TreeType>> pending;
    int defaultWind = 0;
    std::vector<std::string> bushes;
    bool fail(const std::string &) { return false; }
    @ACQUIRE@
};
struct TreeInstance { std::shared_ptr<TreeType> type; unsigned id; };
struct TreeDrawItem { TreeInstance * instance; };
void sortItems(std::vector<TreeDrawItem> & items)
{
    @SORT@
}
void oldSort(std::vector<TreeDrawItem> & items)
{
    std::stable_sort(items.begin(), items.end(), [](const TreeDrawItem & a, const TreeDrawItem & b) {
        const auto & x = *a.instance->type;
        const auto & y = *b.instance->type;
        return x.resource + "/" + std::to_string(x.seed) < y.resource + "/" + std::to_string(y.seed);
    });
}
std::size_t sortAllocations(std::vector<TreeDrawItem> & items, bool old)
{
    allocations = 0;
    countAllocations = true;
    if (old) oldSort(items); else sortItems(items);
    countAllocations = false;
    return allocations;
}
int main()
{
    Owner owner;
    const std::string resource = "vegetation/trees/long_resource_name/oak.spt";
    auto first = owner.acquire(resource, 10);
    assert(first && first->sortKey == resource + "/10");
    assert(owner.acquire(resource, 10) == first && loads == 1 && owner.pending.size() == 1);
    owner.active = false;
    assert(!owner.acquire(resource, 10) && loads == 1);
    owner.active = true;
    loadSucceeds = false;
    assert(!owner.acquire(resource, 2) && loads == 2 && owner.types.size() == 1 && owner.pending.size() == 1);
    loadSucceeds = true;
    auto retry = owner.acquire(resource, 2);
    assert(retry && retry->sortKey == resource + "/2" && loads == 3);
    owner.types.clear();
    owner.pending.clear();
    auto rebuilt = owner.acquire(resource, 10);
    assert(rebuilt != first && rebuilt->sortKey == first->sortKey && loads == 4);

    const std::vector<std::string> resources = {
        "", "a", "a/", "a/0", "a/0.spt", "a.spt", "a.spt/extra",
        "vegetation/trees/oak.spt", "vegetation/trees/oak.spt2",
        "vegetation/trees/\xe6\xa0\x91.spt", std::string(512, 'x') + "/tree.spt"};
    const std::vector<uint32> seeds = {
        0, 1, 10, 2, 9, 100, 2147483647u, 2147483648u,
        static_cast<uint32>(-2), static_cast<uint32>(-1), std::numeric_limits<uint32>::max()};
    std::vector<std::shared_ptr<TreeType>> types;
    for (const auto & path : resources)
        for (auto seed : seeds) types.push_back(owner.acquire(path, seed));
    // A different owner can have distinct type pointers with the same key.
    Owner other;
    types.push_back(other.acquire(resource, 10));
    types.push_back(rebuilt);
    std::vector<TreeInstance> instances;
    for (unsigned repetition = 0; repetition != 24; ++repetition)
        for (auto & type : types) instances.push_back({type, static_cast<unsigned>(instances.size())});
    std::vector<TreeDrawItem> items;
    for (auto & instance : instances) items.push_back({&instance});
    std::mt19937 random(1513);
    std::shuffle(items.begin(), items.end(), random);
    auto expected = items, actual = items;
    const auto oldAllocations = sortAllocations(expected, true);
    const auto cachedAllocations = sortAllocations(actual, false);
    assert(actual.size() == expected.size());
    for (std::size_t i = 0; i != actual.size(); ++i)
        assert(actual[i].instance->id == expected[i].instance->id);
    // Long paths force the old comparator's transient keys beyond any SSO.
    // A cached comparison must add no allocations beyond stable_sort's buffer.
    auto bufferOnly = items;
    allocations = 0;
    countAllocations = true;
    std::stable_sort(bufferOnly.begin(), bufferOnly.end(), [](const TreeDrawItem & a, const TreeDrawItem & b) {
        return a.instance->id < b.instance->id;
    });
    countAllocations = false;
    assert(cachedAllocations == allocations);
    assert(oldAllocations > cachedAllocations + items.size());
    // Explicitly protect decimal lexical ordering rather than numeric ordering.
    TreeInstance ten = {rebuilt, 0}, two = {retry, 1};
    std::vector<TreeDrawItem> decimal = {{&two}, {&ten}};
    sortItems(decimal);
    assert(decimal[0].instance == &ten);
    std::vector<TreeDrawItem> empty;
    sortItems(empty);
    std::printf("Tree sort: %zu IDs identical; allocations old=%zu cached=%zu buffer=%zu\n",
                actual.size(), oldAllocations, cachedAllocations, allocations);
}
