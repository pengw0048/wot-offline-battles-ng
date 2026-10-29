#include "pch.hpp"
#include "stock_native_bot_world_adapter.hpp"
#include "stock_battle_scene_queries.hpp"
#include "space/space_manager.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace stock_native_world {
namespace {
BW::Vector3 native(const Point& p)
{return BW::Vector3(float(p[0]),float(p[1]),float(p[2]));}
Point point(const BW::Vector3& p){return {{p.x,p.y,p.z}};}
QueryStatus status(BW::StockSegmentResult value)
{
    if(value==BW::STOCK_SEGMENT_HIT)return QueryStatus::Found;
    if(value==BW::STOCK_SEGMENT_MISS)return QueryStatus::Miss;
    return QueryStatus::Failed;
}
bool finite(const Point& p)
{return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
std::string normalized(const std::string& name)
{
    std::string out=name;for(auto& c:out){if(c=='\\')c='/';c=char(std::tolower(static_cast<unsigned char>(c)));}
    const auto a=out.find_first_not_of(" \t\n\r\v\f");if(a==std::string::npos)return {};
    return out.substr(a,out.find_last_not_of(" \t\n\r\v\f")-a+1);
}
Point translated(const BW::Vector3& value,const BW::Vector3& offset)
{return {{double(value.x)+double(offset.x),double(value.y)+double(offset.y),double(value.z)+double(offset.z)}};}
std::vector<Box> transformed(const std::vector<Box>& local,const BW::Matrix& matrix,const BW::Vector3& offset)
{
    std::vector<Box> out;
    for(auto box:local) {
        box.center=translated(matrix.applyPoint(native(box.center)),offset);
        for(auto& axis:box.axes)axis=point(matrix.applyVector(native(axis)));
        if(box.axes.size()!=3)continue;
        const auto& a=box.axes[0];const auto& b=box.axes[1];const auto& c=box.axes[2];
        const double volume=std::abs(a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0]));
        if(volume<=1.e-9)continue;
        if(!finite(box.center)||!finite(a)||!finite(b)||!finite(c))continue;
        out.push_back(std::move(box));
    }
    return out;
}
}
NativeScene::NativeScene(BW::ClientSpacePtr space,Registry& registry,Effects& effects):
    space_(space),registry_(registry),effects_(effects){}
bool NativeScene::current()const
{return !retired_&&space_&&!space_->cleared()&&BW::SpaceManager::instance().space(space_->id())==space_;}
bool NativeScene::install(const Key& key,FrozenIdentity identity)
{
    if(!current()||identity.quantization<=0.||!std::isfinite(identity.quantization))return false;
    if(!identities_.count(key))by_chunk_[key.first].push_back(key);
    identities_[key]=std::move(identity);
    refresh(key,registry_);
    return true;
}
void NativeScene::remove(const Key& key)
{
    identities_.erase(key);registry_.remove(key);
    auto& rows=by_chunk_[key.first];rows.erase(std::remove(rows.begin(),rows.end(),key),rows.end());
    registry_.ready_tree_chunks.erase(key.first);
}
bool NativeScene::locator(const Key& key,const FrozenIdentity& source,
    const BW::StockDestructibleSnapshot& live,BW::Matrix& local,BW::Matrix& chunk)const
{
    BW::stockDestructibleLocalMatrix(key.first,live.world,local,chunk);
    const std::array<Point,3> basis{{point(local.applyVector(BW::Vector3(1.f,0.f,0.f))),
        point(local.applyVector(BW::Vector3(0.f,1.f,0.f))),point(local.applyVector(BW::Vector3(0.f,0.f,1.f)))}};
    try {
        return equivalentLocator(locatorSignature(point(local.applyToOrigin()),point(chunk.applyToOrigin()),
            basis,source.quantization),source.locator_signature,source.canonical_ambiguous);
    } catch(const std::exception&) {return false;}
}
QueryStatus NativeScene::segment(const Point& start,const Point& end,std::uint16_t skip,
    const Filter& filter,Hit& out)
{
    if(!current()||!finite(start)||!finite(end))return QueryStatus::Failed;
    BW::StockSegmentHit hit;
    const auto result=BW::collideStockSegmentFiltered(space_->id(),native(start),native(end),
        skip,[&](BW::uint32 material,BW::uint32 flags,BW::uint64 identity) {
            const auto chunk=std::uint32_t(identity&0xffffffffu);
            const auto item=std::uint32_t((identity>>32)&0xffffu);
            return !filter||filter(Surface{material,flags,item,chunk});
        },hit);
    if(result==BW::STOCK_SEGMENT_HIT) {
        out.point=point(hit.point);out.normal=point(hit.normal);
        out.translation=point(hit.translation);
        out.surface={hit.materialKind,0,hit.itemIndex,hit.chunkID};
    }
    return status(result);
}
QueryStatus NativeScene::material(const Point& start,const Point& end,const Point& contact,
    MaterialHit& out)
{
    return materialFiltered(start,end,contact,{},out);
}
QueryStatus NativeScene::materialFiltered(const Point& start,const Point& end,const Point& contact,
    const Filter& keep,MaterialHit& out)
{
    if(!current()||!finite(start)||!finite(end)||!finite(contact))return QueryStatus::Failed;
    BW::StockMaterialHit hit;
    const auto result=BW::stockMaterialNearPoint(space_->id(),native(start),native(end),native(contact),
        [&](BW::uint32 chunk,BW::uint16 item,BW::uint32 material,const BW::string&) {
            return keep&&!keep(Surface{material,0,item,chunk});
        },hit);
    out.hit.point=point(hit.point);out.hit.normal=point(hit.normal);
    out.hit.surface={hit.materialKind,0,hit.itemIndex,hit.chunkID};
    out.filename.assign(hit.filename.data(),hit.filename.size());
    return status(result);
}
QueryStatus NativeScene::water(const Point& start,const Point& end,double& distance)
{
    if(!current()||!finite(start)||!finite(end))return QueryStatus::Failed;
    distance=BW::collideStockWater(native(start),native(end),false);
    return distance<0.?QueryStatus::Miss:QueryStatus::Found;
}
Admission NativeScene::refresh(const Key& key,Registry& registry)
{
    if(!current())return Admission::Pending;
    const auto found=identities_.find(key);
    if(found==identities_.end())return Admission::Pending;
    auto& source=found->second;
    BW::StockDestructibleSnapshot live;BW::StockDestructibleBinding binding;
    if(!BW::readStockDestructible(space_->id(),key.first,key.second,-1,live,&binding))
        return Admission::Pending;
    const auto isolate=[&](){registry.skin.isolated_slots.insert(key);registry.remove(key);return Admission::Isolated;};
    if(registry.isolated(key))return Admission::Isolated;
    BW::Matrix local,chunk;
    const bool changed=source.native_pending||!(binding==source.binding);
    if(changed&&!locator(key,source,live,local,chunk))return isolate();
    if(!changed)BW::stockDestructibleLocalMatrix(key.first,live.world,local,chunk);
    if(!source.instance.descriptor_valid)return Admission::Pending;
    const std::string raw(live.filename.data(),live.filename.size());
    if(!normalized(raw).empty()&&normalized(raw)!=normalized(source.instance.filename))return isolate();
    if(source.instance.kind==Kind::Tree) {
        if(raw.empty()||live.category!=source.category)return isolate();
        if(source.instance.tree_health<=0.||source.instance.tree_health>1000.)return Admission::Pending;
    } else if(source.instance.kind==Kind::Structure) {
        std::set<int> materials;for(const auto& box:source.local_boxes)materials.insert(box.material);
        if(materials.empty())for(const auto& box:source.instance.boxes)materials.insert(box.material);
        for(int material:materials) {
            if(material<73||material>=86||!source.instance.scaled_health.count(material))return isolate();
            BW::StockDestructibleSnapshot module;
            if(!BW::readStockDestructible(space_->id(),key.first,key.second,material-73,module)||
                (module.category!=-1&&module.category!=source.category))return isolate();
        }
    } else if(live.category!=-1&&live.category!=source.category)return isolate();
    if(changed){source.binding=binding;source.reference_world=live.world;source.native_pending=false;}
    // Convert the previously proved world boxes through the exact live matrix.
    // World-space translation alone cannot follow a falling column's rotation.
    BW::Matrix inverse,delta;
    if(!inverse.invert(source.reference_world))return Admission::Isolated;
    D3DXMatrixMultiply(&delta,&inverse,&live.world);
    Instance value=source.instance;
    if(!source.local_boxes.empty())value.boxes=transformed(source.local_boxes,local,chunk.applyToOrigin());
    else for(auto& box:value.boxes) {
        box.center=point(delta.applyPoint(native(box.center)));
        for(auto& axis:box.axes)axis=point(delta.applyVector(native(axis)));
        if(!finite(box.center))return isolate();
        for(const auto& axis:box.axes)if(!finite(axis))return isolate();
    }
    value.origin=translated(local.applyToOrigin(),chunk.applyToOrigin());
    if(!finite(value.origin))return isolate();
    value.native_name_exact=!raw.empty()&&normalized(raw)==normalized(value.filename);
    value.admission=Admission::Admitted;
    registry.put(key,std::move(value));
    effects_.bind(key,binding);
    return Admission::Admitted;
}
bool NativeScene::unresolved(const Key& key,std::vector<Box>& boxes)
{
    boxes.clear();if(!current()||registry_.isolated(key))return false;
    const auto found=identities_.find(key);if(found==identities_.end())return false;
    const auto& source=found->second;
    if(source.instance.kind==Kind::Tree||source.instance.kind==Kind::Falling)return false;
    BW::StockDestructibleSnapshot live;BW::Matrix local,chunk;
    if(!BW::readStockDestructible(space_->id(),key.first,key.second,-1,live)||!locator(key,source,live,local,chunk))return false;
    boxes=source.instance.boxes;
    return !boxes.empty();
}
bool NativeScene::destroy(const Destruction& event)
{
    if(!current()||registry_.isolated(event.key))return false;
    const auto found=identities_.find(event.key);
    if(found==identities_.end()||found->second.native_pending)return false;
    BW::StockDestructibleSnapshot live;BW::StockDestructibleBinding binding;
    if(BW::readStockDestructible(space_->id(),event.key.first,event.key.second,-1,live,&binding)&&
        !(binding==found->second.binding)&&refresh(event.key,registry_)!=Admission::Admitted)return false;
    return effects_.destroy(event)&&current();
}
bool NativeScene::treeChunks(const std::vector<Box>& boxes,std::vector<std::uint32_t>& chunks)
{
    if(!current())return false;
    chunks.clear();
    std::set<std::uint32_t> required;
    for(const auto& box:boxes) {
        double ex=.5,ez=.5;
        for(const auto& axis:box.axes){ex+=std::abs(axis[0]);ez+=std::abs(axis[2]);}
        const double min_x=box.center[0]-ex,max_x=box.center[0]+ex;
        const double min_z=box.center[2]-ez,max_z=box.center[2]+ez;
        const double nx=std::max(1.,std::ceil((max_x-min_x)/50.));
        const double nz=std::max(1.,std::ceil((max_z-min_z)/50.));
        if(!std::isfinite(nx)||!std::isfinite(nz)||nx>32.||nz>32.)return false;
        for(int ix=0;ix<=int(nx);++ix)for(int iz=0;iz<=int(nz);++iz) {
            // Math.Vector3 stores these samples as binary32 before the exact
            // DestructiblesCache chunk mapper performs floor and bit packing.
            const double x=float(min_x+(max_x-min_x)*double(ix)/nx);
            const double z=float(min_z+(max_z-min_z)*double(iz)/nz);
            const double gx=std::floor(x*.01)+127.,gz=std::floor(z*.01)+127.;
            if(!std::isfinite(gx)||!std::isfinite(gz)||gx<0.||gz<0.||gx>253.||gz>253.)return false;
            required.insert((std::uint32_t(gx)<<8)|std::uint32_t(gz));
        }
    }
    chunks.assign(required.begin(),required.end());
    return warmTreeChunks(chunks)&&!chunks.empty();
}
bool NativeScene::warmTreeChunks(const std::vector<std::uint32_t>& required)
{
    if(!current())return false;
    const auto revision=streamRevision();if(revision!=stream_revision_){
        for(auto chunk:registry_.ready_tree_chunks)registry_.geometry.drop_chunk(chunk);
        registry_.ready_tree_chunks.clear();stream_revision_=revision;
    }
    std::vector<BW::uint32> streamed;
    if(!BW::stockDestructibleChunks(space_->id(),streamed))return false;
    const std::set<std::uint32_t> loaded(streamed.begin(),streamed.end());
    for(auto id:required) {
        if(!loaded.count(id)){registry_.ready_tree_chunks.erase(id);registry_.geometry.drop_chunk(id);continue;}
        if(registry_.ready_tree_chunks.count(id))continue;
        offline_destructible::Chunk prepared;bool pending=false;
        for(const auto& key:by_chunk_[id]) {
            const auto& frozen=identities_.at(key).instance;
            if(frozen.kind!=Kind::Tree&&(frozen.kind!=Kind::Falling||!frozen.boxes.empty()))continue;
            if(frozen.kind==Kind::Tree&&(frozen.tree_health<=0.||frozen.tree_health>1000.))continue;
            const auto admitted=refresh(key,registry_);
            if(admitted!=Admission::Admitted){pending|=admitted==Admission::Pending;continue;}
            const auto& source=registry_.instances.at(key);
            offline_destructible::Item item;item.index=int(key.second);item.position=source.origin;
            item.tree=source.kind==Kind::Tree;item.named=source.native_name_exact;item.catalog=false;
            prepared.items[item.index]=item;
            prepared.origins[{int(std::floor(item.position[0]/8.)),int(std::floor(item.position[2]/8.))}].push_back(item.index);
        }
        registry_.geometry.chunk(id,std::move(prepared));
        if(!pending)registry_.ready_tree_chunks.insert(id);
    }
    return true;
}
bool NativeScene::turretSweep(const TurretSweep& sweep)
{return current()&&effects_.turretSweep(sweep)&&current();}
TreeIdentity NativeScene::trustedTree(const Key&key)
{
    if(!current())return TreeIdentity::Pending;if(registry_.isolated(key))return TreeIdentity::Hard;
    const auto found=identities_.find(key);if(found==identities_.end())return TreeIdentity::Other;
    BW::StockDestructibleSnapshot live;if(!BW::readStockDestructible(space_->id(),key.first,key.second,-1,live))return TreeIdentity::Pending;
    const std::string name(live.filename.data(),live.filename.size());
    const auto& value=found->second.instance;
    if(normalized(name).empty()||normalized(name)!=normalized(value.filename))return TreeIdentity::Other;
    return value.descriptor_valid&&value.kind==Kind::Tree?TreeIdentity::Tree:TreeIdentity::Other;
}
std::uint64_t NativeScene::streamRevision()const{return current()?BW::stockDestructibleRevision(space_->id()):0;}
}
