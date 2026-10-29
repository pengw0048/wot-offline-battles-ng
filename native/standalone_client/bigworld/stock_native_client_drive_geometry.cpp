#include "stock_native_client_drive_geometry.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cctype>
#include <limits>

namespace stock_native_client_drive {
namespace {
namespace p=stock_native_bot_python;
namespace world=stock_native_world;
using Key=world::Key;
int cell(double value){const double out=std::floor(value/8.);if(!std::isfinite(out)||out<std::numeric_limits<int>::min()||out>std::numeric_limits<int>::max())throw p::Error("Native player support region is invalid");return int(out);}
p::Ref keyValue(Key key){return p::Ref(Py_BuildValue("(II)",key.first,key.second));}
Key key(PyObject*raw){if(p::size(raw)!=2)throw p::Error("Native player local geometry key is invalid");auto c=p::item(raw,0),i=p::item(raw,1);const auto chunk=p::wide(c.get()),item=p::wide(i.get());
    if(chunk<0||std::uint64_t(chunk)>0xffffffffULL||item<0||item>0xffff)throw p::Error("Native player local geometry key is outside the native domain");return {std::uint32_t(chunk),std::uint32_t(item)};}
void addMembers(PyObject*raw,std::set<Key>&out){if(!raw||raw==Py_None)return;if(!PySet_Check(raw)&&!PyFrozenSet_Check(raw))throw p::Error("Native player contact-bin members are not a raw set");
    p::Ref iterator(PyObject_GetIter(raw));while(PyObject*item=PyIter_Next(iterator.get())){p::Ref value(item);out.insert(key(value.get()));}if(PyErr_Occurred())throw p::Error("Native player contact-bin iteration failed");}
std::vector<world::Box> boxes(PyObject*row){auto raw=p::value(row,"boxes");std::vector<world::Box> out;
    for(Py_ssize_t i=0;i<p::size(raw.get());++i){auto value=p::item(raw.get(),i);if(p::size(value.get())!=3)throw p::Error("Native player live box width is invalid");
        auto center=p::item(value.get(),0),axes=p::item(value.get(),1),material=p::item(value.get(),2);world::Box box;box.center=p::numbers<3>(center.get());
        if(p::size(axes.get())!=3)throw p::Error("Native player live box basis width is invalid");
        for(Py_ssize_t j=0;j<3;++j){auto axis=p::item(axes.get(),j);box.axes.push_back(p::numbers<3>(axis.get()));}
        const auto mat=material.get()==Py_None?-1:p::wide(material.get());if(mat!=-1&&(mat<73||mat>=86))throw p::Error("Native player live box material is invalid");box.material=int(mat);
        const auto&a=box.axes[0];const auto&b=box.axes[1];const auto&c=box.axes[2];
        const double volume=std::abs(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]));
        if(volume<=1.e-9)throw p::Error("Native player live box basis is singular");out.push_back(std::move(box));}
    if(out.empty())throw p::Error("Native player live geometry has no boxes");return out;}
std::string normalizedFilename(std::string out){
    for(auto&c:out){if(c=='\\')c='/';c=char(std::tolower(static_cast<unsigned char>(c)));}
    const auto first=out.find_first_not_of(" \t\r\n\v\f");
    if(first==std::string::npos)return {};
    return out.substr(first,out.find_last_not_of(" \t\r\n\v\f")-first+1);
}
std::string filename(PyObject*row){auto v=p::value(row,"filename");return normalizedFilename(p::string(v.get()));}
}
void LocalGeometry::seedIsolation(PyObject*sensor){
    if(isolation_seeded_)throw p::Error("Native player isolation mirror is already seeded");
    auto chunks=p::value(sensor,"g_offh_destr_isolated_chunks"),slots=p::value(sensor,"g_offh_destr_isolated_slots");
    if(chunks.get()!=Py_None){if(!PySet_Check(chunks.get())&&!PyFrozenSet_Check(chunks.get()))throw p::Error("Native player initial isolation chunks are invalid");
        p::Ref iterator(PyObject_GetIter(chunks.get()));while(PyObject*item=PyIter_Next(iterator.get())){p::Ref value(item);const auto id=p::wide(value.get());
            if(id<0||std::uint64_t(id)>0xffffffffULL)throw p::Error("Native player initial isolation chunk is invalid");mirrored_chunks_.insert(std::uint32_t(id));}
        if(PyErr_Occurred())throw p::Error("Native player initial isolation iteration failed");}
    if(slots.get()!=Py_None)addMembers(slots.get(),mirrored_slots_);
    isolation_seeded_=true;
}
std::optional<stock_native_world::Key> visibleTreeKey(PyObject*raw){if(p::size(raw)==3)return {};return key(raw);}
bool LocalGeometry::sync(PyObject*sensor,stock_native_world::Registry&registry,
    stock_native_world::Scene&scene,const std::array<Point,2>&region){
    using Cell=stock_offline_query::Cell;
    Stamp next;next.region={cell(std::min(region[0][0],region[1][0])),cell(std::max(region[0][0],region[1][0])),
        cell(std::min(region[0][2],region[1][2])),cell(std::max(region[0][2],region[1][2]))};
    next.global=p::integral(sensor,"g_offh_destr_spatial_global_generation");next.stream=scene.streamRevision();
    const auto revision=p::integral(sensor,"g_offh_destr_spatial_revision"),published=p::integral(sensor,"g_offh_destr_spatial_published_revision");
    next.unpublished=revision==published?0:revision;
    auto generations=p::value(sensor,"g_offh_destr_spatial_cell_generations");
    if(generations.get()!=Py_None&&!PyDict_Check(generations.get()))throw p::Error("Native player geometry generations are invalid");
    std::vector<Cell> cells;
    for(int x=next.region[0];;){for(int z=next.region[2];;){p::Ref cellkey(Py_BuildValue("(ii)",x,z));
        auto*value=generations.get()==Py_None?nullptr:PyDict_GetItem(generations.get(),cellkey.get());next.cells.push_back(value?p::wide(value):0);cells.push_back({x,z});
        if(z==next.region[3])break;++z;}if(x==next.region[1])break;++x;}
    if(installed_&&next==stamp_){++reuses_;return false;}
    auto bins=p::value(sensor,"g_offh_destr_contact_bins"),instances=p::value(sensor,"g_offh_destr_instances");
    if((bins.get()!=Py_None&&!PyDict_Check(bins.get()))||(instances.get()!=Py_None&&!PyDict_Check(instances.get())))throw p::Error("Native player local scene index is invalid");
    std::set<Key> current;
    for(const auto&at:cells){p::Ref cellkey(Py_BuildValue("(ii)",at.first,at.second));
        if(bins.get()!=Py_None)addMembers(PyDict_GetItem(bins.get(),cellkey.get()),current);
        const auto baked=registry.skin.baked_shot_bins.find(at);if(baked!=registry.skin.baked_shot_bins.end())current.insert(baked->second.begin(),baked->second.end());}
    auto affected=current;affected.insert(members_.begin(),members_.end());
    auto isolated_chunks=p::value(sensor,"g_offh_destr_isolated_chunks"),isolated_slots=p::value(sensor,"g_offh_destr_isolated_slots");
    for(const auto&id:affected){auto encoded=keyValue(id);auto*raw=instances.get()==Py_None?nullptr:PyDict_GetItem(instances.get(),encoded.get());
        auto chunk=p::integer(id.first);const int chunk_isolated=isolated_chunks.get()==Py_None?0:PySet_Contains(isolated_chunks.get(),chunk.get());
        const int slot_isolated=isolated_slots.get()==Py_None?0:PySet_Contains(isolated_slots.get(),encoded.get());
        if(chunk_isolated<0||slot_isolated<0)throw p::Error("Native player isolation snapshot is invalid");
        // Only retire isolation values this mirror installed. Native locator
        // failures remain quarantined independently of the visible snapshot.
        if(!chunk_isolated&&mirrored_chunks_.erase(id.first))registry.skin.isolated_chunks.erase(id.first);
        if(!slot_isolated&&mirrored_slots_.erase(id))registry.skin.isolated_slots.erase(id);
        if(chunk_isolated||slot_isolated){
            if(chunk_isolated&&registry.skin.isolated_chunks.insert(id.first).second)mirrored_chunks_.insert(id.first);
            if(slot_isolated&&registry.skin.isolated_slots.insert(id).second)mirrored_slots_.insert(id);
            registry.remove(id);registry.skin.tree_instances.erase(id);continue;}
        const auto admission=scene.refresh(id,registry);
        if(admission==world::Admission::Isolated){registry.remove(id);continue;}
        if(!raw){if(admission!=world::Admission::Admitted)registry.remove(id);continue;}
        // This is the existing visible owner's admitted live row, not a new
        // wire. Retain the cold exact descriptor and native isolation proof;
        // copy only its validated current geometry, including falling rotation.
        // Admission here belongs only to this private read-only filter store;
        // it does not prove a loaded resource or authorize native destruction.
        const auto baked=registry.baked_instances.find(id);if(baked==registry.baked_instances.end())continue;
        auto kind=p::value(raw,"kind");const auto tag=p::string(kind.get());const auto expected=baked->second.kind;
        if(filename(raw)!=normalizedFilename(baked->second.filename)||
            (expected==world::Kind::Fragile?tag!="fragile":expected==world::Kind::Structure?tag!="structure":expected==world::Kind::Falling?tag!="falling":tag!="tree"))
            throw p::Error("Native player visible geometry identity changed");
        auto value=baked->second;value.boxes=boxes(raw);value.admission=world::Admission::Admitted;
        registry.put(id,std::move(value));
    }
    members_=std::move(current);stamp_=std::move(next);installed_=true;++refreshes_;return true;
}
}
