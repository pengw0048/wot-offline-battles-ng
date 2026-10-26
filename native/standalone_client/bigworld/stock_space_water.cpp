#ifndef WOT_STOCK_SPACE_WATER_PORTABLE
#include "pch.hpp"
#endif
#include "stock_space_water.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace wot
{
namespace
{
struct Reader
{
    const unsigned char* bytes;
    std::size_t size, offset = 0;
    std::string& error;
    bool fail(const char* message) { error=message;return false; }
    template<class T> bool array(std::vector<T>& output)
    {
        if (!bytes || offset>size || size-offset<8) return fail("Truncated BWWa2 array header");
        std::uint32_t stride,count;
        std::memcpy(&stride,bytes+offset,4);std::memcpy(&count,bytes+offset+4,4);offset+=8;
        if (stride!=sizeof(T)) return fail("Incorrect BWWa2 array stride");
        if (count>(size-offset)/sizeof(T)) return fail("Truncated BWWa2 array body");
        output.resize(count);
        if (count) std::memcpy(output.data(),bytes+offset,sizeof(T)*count);
        offset+=sizeof(T)*count;
        return true;
    }
};
}
StockSpaceWaterData::U32 StockSpaceWaterData::Definition::word(std::size_t offset) const
{
    if (offset%4 || offset>=244) throw std::out_of_range("BWWa2 state field");
    return state[offset/4];
}
float StockSpaceWaterData::Definition::scalar(std::size_t offset) const
{
    const U32 bits=word(offset);
    float value;std::memcpy(&value,&bits,4);return value;
}
bool StockSpaceWaterData::grid(const Definition& definition, Grid& result)
{
    const float tessellation=definition.scalar(0x18);
    if (!std::isfinite(tessellation) || tessellation<=0.f) return false;
    U32 dimensions[2];
    for (unsigned axis=0;axis<2;++axis)
    {
        const float extent=definition.scalar(0xc+axis*4);
        if (!std::isfinite(extent) || extent<0.f) return false;
        // a25ff0 divides in float before passing the value to double ceil.
        const float quotient=extent/tessellation;
        const float dimension=static_cast<float>(std::ceil(static_cast<double>(quotient)))+1.f;
        if (!std::isfinite(dimension) || dimension<1.f || dimension>=2147483648.f) return false;
        dimensions[axis]=static_cast<U32>(dimension);
    }
    result={dimensions[0],dimensions[1],std::uint64_t(dimensions[0])*dimensions[1]};
    return true;
}
void StockSpaceWaterData::clear()
{
    definitions_.clear();bounds_.clear();cells_.clear();instances_.clear();resolved_=false;
}
bool StockSpaceWaterData::read(const void* bytes, std::size_t size, std::string& error)
{
    clear();error.clear();
    StockSpaceWaterData next;
    Reader reader{static_cast<const unsigned char*>(bytes),size,0,error};
    if (!reader.array(next.definitions_) || !reader.array(next.bounds_)) return false;
    if (reader.offset!=size) return reader.fail("Unexpected BWWa2 trailing data");
    next.cells_.resize(next.bounds_.size());
    const U32 unassigned=(std::numeric_limits<U32>::max)();
    for (auto& cell:next.cells_) cell.surface=unassigned;
    next.instances_.resize(next.definitions_.size());
    for (std::size_t surface=0;surface<next.definitions_.size();++surface)
    {
        const auto& definition=next.definitions_[surface];
        auto& instance=next.instances_[surface];
        instance.definition=static_cast<U32>(surface);
        for (std::size_t offset: {0u,4u,8u,0x14u})
            if (!std::isfinite(definition.scalar(offset))) return reader.fail("Non-finite BWWa2 water transform");
        if (!grid(definition,instance.grid)) return reader.fail("Invalid BWWa2 x86 water grid dimensions");
        if (definition.firstCell>definition.endCell || definition.endCell>next.bounds_.size())
            return reader.fail("BWWa2 water cell range escapes its bounds array");
        for (U32 index=definition.firstCell;index<definition.endCell;++index)
        {
            auto& cell=next.cells_[index];
            if (cell.surface!=unassigned) return reader.fail("BWWa2 water cell has multiple surface owners");
            cell.bounds=next.bounds_[index];cell.surface=static_cast<U32>(surface);
            cell.first=index==definition.firstCell;
        }
    }
    for (const auto& cell:next.cells_)
    {
        if (cell.surface==unassigned) return reader.fail("BWWa2 water cell has no surface owner");
        for (unsigned axis=0;axis<3;++axis)
            if (!std::isfinite(cell.bounds.minimum[axis]) || !std::isfinite(cell.bounds.maximum[axis]))
                return reader.fail("Non-finite BWWa2 water cell bounds");
    }
    next.resolved_=next.definitions_.empty();
    *this=std::move(next);
    return true;
}
bool StockSpaceWaterData::resolve(const Resolver& lookup, std::string& error)
{
    error.clear();resolved_=false;
    auto instances=instances_;
    for (auto& instance:instances)
    {
        const auto& definition=definitions_[instance.definition];
        for (unsigned field=0;field<4;++field)
        {
            const char* value=lookup ? lookup(definition.resourceKeys[field]) : nullptr;
            if (!value) { error="BWWa2 resource key is absent from BWST";return false; }
            instance.resources[field]=value;
        }
    }
    instances_=std::move(instances);resolved_=true;
    return true;
}
}
