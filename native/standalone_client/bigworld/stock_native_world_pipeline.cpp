#include "stock_native_world_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace stock_native_world {
namespace {
namespace q = stock_offline_query;
namespace g = offline_destructible;
Point rounded(const Point & p) { return {{float(p[0]), float(p[1]), float(p[2])}}; }
Point difference(const Point & a, const Point & b) {
    return {{float(float(a[0])-float(b[0])), float(float(a[1])-float(b[1])), float(float(a[2])-float(b[2]))}};
}
double length(const Point & p) {
    const float sum = float(float(float(p[0])*float(p[0])) + float(float(p[1])*float(p[1]))) + float(float(p[2])*float(p[2]));
    return float(std::sqrt(sum));
}
double distance(void *, const Point & a, const Point & b) { return length(difference(a,b)); }
Point bound(void *, const Point & a, const Point & b, double factor) {
    auto delta = difference(b,a); const float f = float(factor);
    return {{float(float(a[0])+float(float(delta[0])*f)), float(float(a[1])+float(float(delta[1])*f)), float(float(a[2])+float(float(delta[2])*f))}};
}
Point advanced(const Point & hit, const Point & end, double epsilon) {
    const double remaining = distance(nullptr,end,hit);
    return remaining <= epsilon ? rounded(end) : bound(nullptr,hit,end,epsilon/remaining);
}
q::Cell cell(const Point & p, double metres = 8.) {
    const double x=std::floor(p[0]/metres), z=std::floor(p[2]/metres);
    if(!std::isfinite(x)||!std::isfinite(z)||x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max()||z<std::numeric_limits<int>::min()||z>std::numeric_limits<int>::max())
        throw std::invalid_argument("Native world cell is outside the integer domain");
    return {int(x),int(z)};
}
MaterialKey identity(const Key & key, std::int64_t material) { return {key.first,key.second,material}; }
Key key(const MaterialKey & value) { return {std::get<0>(value),std::get<1>(value)}; }
std::int64_t material(const MaterialKey & value) { return std::get<2>(value); }
bool original(const Surface & s) { return 71<=s.material && s.material<=86; }
std::array<double,4> bounds(const Box & b) {
    double x=0.,z=0.;for(const auto & axis:b.axes){x+=std::abs(axis[0]);z+=std::abs(axis[2]);}
    return {{b.center[0]-x,b.center[0]+x,b.center[2]-z,b.center[2]+z}};
}
std::set<Key> members(const Registry & registry,const Point & a,const Point & b) {
    std::set<Key> out;
    const auto first=cell(a),last=cell(b);
    const int lx=std::min(first.first,last.first),rx=std::max(first.first,last.first);
    const int lz=std::min(first.second,last.second),rz=std::max(first.second,last.second);
    for(auto at=registry.skin.contact_bins.lower_bound({lx,std::numeric_limits<int>::min()});at!=registry.skin.contact_bins.end()&&at->first.first<=rx;++at)
        if(lz<=at->first.second&&at->first.second<=rz)out.insert(at->second.begin(),at->second.end());
    return out;
}
bool keepBroken(const Registry & r,const std::set<Key> & ids,const Surface & s) {
    if(87<=s.material && s.material<=100)return true;
    const auto k=s.key();
    if(r.skin.tree_instances.count(k) && r.destroyed(identity(k,-1)))return false;
    if(!ids.count(k))return true;
    const bool whole=r.destroyed(identity(k,-1));
    bool keep=!whole && !r.destroyed(identity(k,s.material));
    if(whole && !original(s)) {
        const auto found=r.instances.find(k);
        if(found!=r.instances.end()&&found->second.retained_collision)keep=true;
    }
    return keep;
}
q::SkinKind skinKind(Kind k) {
    return k==Kind::Structure?q::SkinKind::Structure:k==Kind::Fragile?q::SkinKind::Fragile:k==Kind::Falling?q::SkinKind::Falling:q::SkinKind::Other;
}
bool contains(const Box & box,const Point & point,double padding=.075) {
    return g::probe_boxes({box},point,point,point,padding,1).front().inside;
}
}

LocatorSignature locatorSignature(const Point & origin,const Point & translation,const std::array<Point,3> & basis,double quantization) {
    if(!std::isfinite(quantization)||!(quantization>0.))throw std::invalid_argument("Locator quantization is invalid");
    std::array<double,12> values;
    // Python adds the two exposed binary32 coordinates as doubles; there is
    // no additional Vector3 store on the world-origin sum in this law.
    for(unsigned i=0;i<3;++i)values[i]=double(float(origin[i]))+double(float(translation[i]));
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)values[3+i*3+j]=float(basis[i][j]);
    LocatorSignature out;
    for(unsigned i=0;i<12;++i) {
        const double scaled=values[i]*quantization;
        const double result=scaled>=0.?std::floor(scaled+.5):std::ceil(scaled-.5);
        if(!std::isfinite(result)||result<double(std::numeric_limits<std::int64_t>::min())||result>=9223372036854775808.)throw std::invalid_argument("Locator signature is outside the integer domain");
        out[i]=std::int64_t(result);
    }
    return out;
}
bool equivalentLocator(const LocatorSignature & live,const LocatorSignature & canonical,bool ambiguous) {
    if(ambiguous)return false;
    for(unsigned i=0;i<12;++i)if(std::abs(static_cast<long double>(live[i])-static_cast<long double>(canonical[i]))>1.)return false;
    return true;
}

void Registry::put(const Key & key,Instance value) {
    remove(key);
    q::SkinInstance skin;skin.kind=skinKind(value.kind);skin.boxes=value.boxes;
    if(value.kind==Kind::Tree) this->skin.tree_instances.insert(key);
    else this->skin.instances[key]=std::move(skin);
    g::Instance geometry;geometry.key={key.first,int(key.second)};geometry.kind=value.kind==Kind::Structure?1:0;geometry.boxes=value.boxes;
    for(const auto & box:value.boxes) {
        const auto b=bounds(box);
        const auto first=cell({{b[0],0.,b[2]}}),last=cell({{b[1],0.,b[3]}});
        for(int x=first.first;;++x){for(int z=first.second;;++z){this->skin.contact_bins[{x,z}].insert(key);geometry.bins.insert({x,z});if(z==last.second)break;}if(x==last.first)break;}
    }
    this->geometry.put(std::move(geometry));instances[key]=std::move(value);++generation;
}
void Registry::putBaked(const Key & key,Instance value) {
    for(auto at=skin.baked_shot_bins.begin();at!=skin.baked_shot_bins.end();) {at->second.erase(key);if(at->second.empty())at=skin.baked_shot_bins.erase(at);else ++at;}
    q::SkinInstance row;row.kind=skinKind(value.kind);row.boxes=value.boxes;skin.baked_instances[key]=std::move(row);
    if(value.kind==Kind::Tree)skin.tree_instances.insert(key);
    for(const auto & box:value.boxes) {
        const auto b=bounds(box);const auto first=cell({{b[0]-value.broad_phase_margin,0.,b[2]-value.broad_phase_margin}}),last=cell({{b[1]+value.broad_phase_margin,0.,b[3]+value.broad_phase_margin}});
        for(int x=first.first;;++x){for(int z=first.second;;++z){skin.baked_shot_bins[{x,z}].insert(key);if(z==last.second)break;}if(x==last.first)break;}
    }
    baked_dirty=true;baked_instances[key]=std::move(value);++generation;
}
void Registry::syncBaked() {
    if(!baked_dirty)return;
    std::map<g::Cell,std::set<g::Key>> bins;
    for(const auto & bin:skin.baked_shot_bins)for(const auto & k:bin.second)bins[bin.first].insert({k.first,int(k.second)});
    geometry.baked(std::move(bins));baked_dirty=false;
}
void Registry::remove(const Key & key) {
    instances.erase(key);skin.instances.erase(key);
    const auto baked=baked_instances.find(key);if(baked==baked_instances.end()||baked->second.kind!=Kind::Tree)skin.tree_instances.erase(key);
    for(auto at=skin.contact_bins.begin();at!=skin.contact_bins.end();) {at->second.erase(key);if(at->second.empty())at=skin.contact_bins.erase(at);else ++at;}
    geometry.remove({key.first,int(key.second)});++generation;
}
void Registry::clear() { const auto next=epoch+1;*this=Registry();epoch=next; }
bool Registry::isolated(const Key & k) const {
    const auto i=instances.find(k);
    return skin.isolated_chunks.count(k.first)||skin.isolated_slots.count(k)||skin.excluded_instances.count(k)||(i!=instances.end()&&i->second.admission==Admission::Isolated);
}
bool Registry::destroyed(const MaterialKey & id) const {
    return skin.destroyed.count(id)||skin.destroyed.count(identity(key(id),-1))||skin.speculative.count(id)||skin.speculative.count(identity(key(id),-1));
}
void Registry::acknowledge(std::uint64_t sequence) {
    journal.erase(std::remove_if(journal.begin(),journal.end(),[&](const Event & event){return event.sequence<=sequence;}),journal.end());
}

Pipeline::Pipeline(Registry & r,Scene & s,std::uint16_t flags,unsigned budget,double epsilon,q::SkinTuning tuning):
    registry_(r),scene_(s),skip_flags_(flags),recast_budget_(budget),epsilon_(epsilon),skin_tuning_(tuning) {
    if(budget>1024||!(epsilon>0.)||!std::isfinite(epsilon)||tuning.bin_metres!=8.)throw std::invalid_argument("Invalid native world pipeline tuning");
}
QueryStatus Pipeline::water(const Point & a,const Point & b,double & value) { return scene_.water(rounded(a),rounded(b),value); }
QueryStatus Pipeline::ray(const Point & a,const Point & b,Hit & output,const Filter & filter,bool skin) {
    struct Segment {Point a,b;std::set<Surface> excluded;};
    std::vector<Segment> stack{{rounded(a),rounded(b),{}}};unsigned remaining=recast_budget_;
    while(!stack.empty()) {
        auto segment=std::move(stack.back());stack.pop_back();std::vector<Surface> candidates;
        const Filter selected=[&](const Surface & s) {
            const bool accepted=!filter||filter(s);
            bool keep=accepted&&!segment.excluded.count(s);
            if(keep&&q::anonymousOriginal(registry_.skin,s))for(const auto & other:segment.excluded)if(other.material==s.material&&other.flags==s.flags){keep=false;break;}
            if(keep&&candidates.size()<16&&std::find(candidates.begin(),candidates.end(),s)==candidates.end())candidates.push_back(s);
            return keep;
        };
        Hit hit;const auto status=scene_.segment(segment.a,segment.b,skip_flags_,selected,hit);
        if(status==QueryStatus::Failed)return status;
        if(status==QueryStatus::Miss)continue;
        q::SkinResult result;
        if(skin&&registry_.catalog_available) {
            bool relevant=false;for(const auto & s:candidates)relevant=relevant||(original(s)&&(s.flags==0||(s.flags&0x80)));
            if(relevant)result=q::motionSkin(registry_.skin,hit.point,segment.a,segment.b,&hit.normal,candidates,skin_tuning_,{nullptr,distance,bound});
        }
        const double n=distance(nullptr,segment.b,segment.a);
        if(!result.found||!remaining||n<=epsilon_){output=hit;return QueryStatus::Found;}
        // Retain Vector3's float32 normalise, multiplication and stores.
        auto delta=difference(segment.b,segment.a);const float inverse=float(1./n);
        for(auto & component:delta)component=float(float(component)*inverse);
        Point bounded=segment.b;
        if(result.distance<n)for(unsigned i=0;i<3;++i)bounded[i]=float(float(segment.a[i])+float(float(delta[i])*float(result.distance)));
        --remaining;
        if(result.distance+epsilon_<n) {
            Point next;for(unsigned i=0;i<3;++i)next[i]=float(float(bounded[i])+float(float(delta[i])*float(epsilon_)));
            stack.push_back({next,segment.b,segment.excluded});
        }
        segment.excluded.insert(result.excluded.begin(),result.excluded.end());
        stack.push_back({segment.a,bounded,std::move(segment.excluded)});
    }
    return QueryStatus::Miss;
}
QueryStatus Pipeline::ground(const Point & a,const Point & b,Hit & out,const std::set<Key> * prepared) {
    std::set<Key> ids;if(prepared)ids=*prepared;else {const auto at=registry_.skin.contact_bins.find(cell(a));if(at!=registry_.skin.contact_bins.end())ids=at->second;}
    return ray(a,b,out,[&](const Surface & s){return keepBroken(registry_,ids,s);},true);
}
QueryStatus Pipeline::horizontal(const Point & a,const Point & b,Hit & out,const std::set<Key> * prepared) {
    const auto ids=prepared?*prepared:members(registry_,a,b);
    return ray(a,b,out,[&](const Surface & s){return keepBroken(registry_,ids,s);},true);
}
QueryStatus Pipeline::navigation(const Point & a,const Point & b,Hit & out,bool descriptor_present) {
    if(!descriptor_present)return ray(a,b,out,{},false);
    return ray(a,b,out,[](const Surface & s){return s.material<71||s.material>=86;},true);
}
bool Pipeline::crushable(const MaterialKey & id,double speed) const {
    const auto found=registry_.instances.find(key(id));if(found==registry_.instances.end()||registry_.isolated(key(id)))return false;
    const auto & value=found->second;
    if(value.admission!=Admission::Admitted||!value.descriptor_valid||!(value.item_scale>0.)||!(actor_.mass>0.))return false;
    if(value.kind!=Kind::Fragile&&value.kind!=Kind::Structure&&value.kind!=Kind::Falling)return false;
    const auto health=value.scaled_health.find(value.kind==Kind::Structure?material(id):-1);
    if(health==value.scaled_health.end()||!std::isfinite(health->second))return false;
    double damage=.5*actor_.mass*speed*speed*.00015;
    if(value.kind!=Kind::Structure) {
        if(!(registry_.unit_vehicle_mass>0.))return false;
        damage*=std::pow(actor_.mass/registry_.unit_vehicle_mass,value.kinetic_correction);
    }
    return health->second<damage;
}
bool Pipeline::commit(const MaterialKey & id,const Point & point,double speed) {
    return commit(id,point,speed,actor_.yaw);
}
bool Pipeline::commit(const MaterialKey & id,const Point & point,double speed,double yaw) {
    const auto found=registry_.instances.find(key(id));if(found==registry_.instances.end())return false;
    const auto value=found->second;
    if(!commitOne(id,point,speed,yaw))return false;
    if(value.kind!=Kind::Structure)return true;
    // Authored collapse closure uses the health of the originally struck
    // module for every reverse edge, never the health of an intermediate node.
    if(value.reference_health.size()>13||!value.reference_health.count(material(id)))return true;
    const auto root_health=value.reference_health.at(material(id));
    std::set<std::int64_t> dependencies{material(id)};std::vector<std::int64_t> pending{material(id)};
    while(!pending.empty()) {
        const auto current=pending.back();pending.pop_back();
        for(unsigned reverse=0;reverse<2;++reverse) {
            const auto & table=reverse?value.inverse_dependencies:value.dependencies;
            const auto row=table.find(current);if(row==table.end())continue;
            if(row->second.size()>13)return true;
            for(const auto next:row->second) {
                const auto health=value.reference_health.find(next);if(health==value.reference_health.end())return true;
                if(reverse&&(!std::isfinite(root_health)||!std::isfinite(health->second)||health->second<0.||health->second>root_health))continue;
                if(dependencies.insert(next).second)pending.push_back(next);
            }
        }
    }
    dependencies.erase(material(id));
    for(const auto child:dependencies)if(!registry_.destroyed(identity(key(id),child)))
        if(!commitOne(identity(key(id),child),point,speed,yaw))throw std::runtime_error("Native crush dependency was not accepted");
    return true;
}
bool Pipeline::commitOne(const MaterialKey & id,const Point & point,double speed,double yaw) {
    const auto found=registry_.instances.find(key(id));if(found==registry_.instances.end()||registry_.isolated(key(id))||registry_.destroyed(id))return false;
    const auto value=found->second;
    if(value.admission!=Admission::Admitted||!value.descriptor_valid)return false;
    if(value.kind==Kind::Tree&&(!(value.tree_health>0.&&value.tree_health<=1000.)||!value.native_name_exact))return false;
    Destruction order;order.kind=value.kind;order.key=key(id);order.material=value.kind==Kind::Structure?material(id):-1;
    order.position=rounded(point);order.yaw=yaw;order.speed=speed;order.now=actor_.now;
    // Install the final journal capacity before entering a synchronously
    // reentrant native mutation; accepted operations must remain observable.
    registry_.journal.reserve(registry_.journal.size()+1);
    if(!scene_.destroy(order))return false;
    const auto accepted=identity(order.key,order.material);
    registry_.skin.destroyed.insert(accepted);registry_.pending[accepted]=actor_.now;
    registry_.journal.push_back({registry_.next_sequence++,order});++registry_.generation;
    if(value.kind==Kind::Tree)registry_.skin.tree_instances.insert(order.key);
    if(value.kind==Kind::Falling)scene_.refresh(order.key,registry_);
    return true;
}

std::vector<MaterialKey> Pipeline::candidates(const Point & point,const Point & start,const Point & end,const std::set<MaterialKey> & excluded,bool prefer) {
    struct Candidate {MaterialKey id;double entry,exit;};std::vector<Candidate> found;
    const auto at=registry_.skin.contact_bins.find(cell(point));if(at==registry_.skin.contact_bins.end())return {};
    const double n=distance(nullptr,end,start),d=distance(nullptr,point,start);if(n<=1.e-9)return {};
    const auto ids=at->second;
    for(const auto & k:ids) {
        if(registry_.isolated(k))continue;
        auto row=registry_.instances.find(k);if(row==registry_.instances.end())continue;
        if(row->second.admission!=Admission::Admitted)continue;
        for(const auto & box:row->second.boxes) {
            const auto id=identity(k,row->second.kind==Kind::Structure?box.material:-1);if(excluded.count(id))continue;
            const auto interval=g::probe_boxes({box},point,start,end,0.,2).front().segment;
            if(!interval.valid||interval.entry*n>d+.075||interval.leave*n+.075<d)continue;
            bool duplicate=false;for(const auto & existing:found)duplicate=duplicate||existing.id==id;
            if(!duplicate)found.push_back({id,interval.entry*n,interval.leave*n});
        }
    }
    if(found.size()==1)return {found.front().id};
    if(prefer&&found.size()>1) {
        std::stable_sort(found.begin(),found.end(),[](const Candidate & a,const Candidate & b){return std::tie(a.exit,a.entry,a.id)<std::tie(b.exit,b.entry,b.id);});
        for(const auto & value:found)if(registry_.destroyed(value.id)) {
            bool safe=true;for(const auto & other:found)if(!registry_.destroyed(other.id)&&other.entry<value.exit-.001)safe=false;
            if(safe)return {value.id};
        }
    }
    return {};
}
int Pipeline::softPath(const Point & start,const Point & end,const Hit & first,bool pending_first,bool allow_kinetic,unsigned & budget) {
    if(!registry_.catalog_available||distance(nullptr,end,start)<=1.e-6)return 0;
    Hit current=first;std::set<MaterialKey> excluded;bool kinetic=false,pending=false;
    for(unsigned i=0;i<4;++i) {
        auto possible=candidates(current.point,start,end,excluded,pending_first&&i==0);
        if(possible.empty()) {
            // A baked OBB only selects a live-validation candidate. It never
            // proves native identity, clearance or permission to destroy.
            std::set<Key> cold;
            const auto low=cell({{current.point[0]-.075,0.,current.point[2]-.075}}),high=cell({{current.point[0]+.075,0.,current.point[2]+.075}});
            for(int x=low.first;;++x){for(int z=low.second;;++z){const auto bin=registry_.skin.baked_shot_bins.find({x,z});if(bin!=registry_.skin.baked_shot_bins.end())cold.insert(bin->second.begin(),bin->second.end());if(z==high.second)break;}if(x==high.first)break;}
            bool deferred=false;unsigned attempted=0;
            for(const auto & k:cold) {
                if(registry_.instances.count(k)||registry_.isolated(k))continue;
                const auto baked=registry_.skin.baked_instances.find(k);if(baked==registry_.skin.baked_instances.end())continue;
                bool relevant=false;
                for(const auto & box:baked->second.boxes) {
                    const auto id=identity(k,baked->second.kind==q::SkinKind::Structure?box.material:-1);
                    if(excluded.count(id)||!contains(box,current.point))continue;
                    relevant=relevant||g::probe_boxes({box},current.point,start,end,0.,2).front().segment.valid;
                }
                if(!relevant)continue;
                if(attempted>=4||!budget)return pending?-3:-2;
                ++attempted;--budget;
                if(scene_.refresh(k,registry_)!=Admission::Admitted&&!registry_.isolated(k))deferred=true;
            }
            if(deferred)return pending?-3:-2;
            possible=candidates(current.point,start,end,excluded,pending_first&&i==0);
        }
        if(possible.size()!=1)return pending?-3:0;
        const auto id=possible.front();const bool broken=registry_.destroyed(id);
        bool crush=broken||crushable(id,actor_.speed);
        if(pending_first&&i==0) {
            if(broken)pending=true;
            else if(allow_kinetic&&(crush||(actor_.has_kinetic_speed&&crushable(id,actor_.kinetic_speed)))){kinetic=true;crush=true;}
            else return 0;
        } else if(allow_kinetic&&!crush&&actor_.has_kinetic_speed&&crushable(id,actor_.kinetic_speed)){kinetic=true;crush=true;}
        if(!crush)return pending?-3:0;
        excluded.insert(id);if(!budget)return pending?-3:-2;--budget;
        const auto status=ray(start,end,current,[&](const Surface & s){return !original(s)||(!excluded.count(identity(s.key(),s.material))&&!excluded.count(identity(s.key(),-1)));},false);
        if(status==QueryStatus::Failed)return -1;
        if(status==QueryStatus::Miss)return kinetic?2:1;
    }
    return pending?-3:0;
}
SoftStatus Pipeline::softStatic(const Point & start,const Point & end,const Hit & hit,bool pending_first,bool allow_kinetic,unsigned & budget) {
    const auto result=softPath(start,end,hit,pending_first,allow_kinetic,budget);
    return result==1?SoftStatus::Clear:result==2?SoftStatus::Kinetic:result==-1?SoftStatus::Failed:result==-2?SoftStatus::Deferred:result==-3?SoftStatus::PendingHard:SoftStatus::Hard;
}
int Pipeline::resolve(const Point & start,const Point & end,const Hit & collision,bool & crushed,bool commit_enabled) {
    unsigned budget=recast_budget_;
    if(crushed)return softPath(start,end,collision,false,false,budget)==1?1:0;
    int clear=softPath(start,end,collision,true,actor_.allow_kinetic,budget);
    if(clear==1){crushed=true;return 1;}if(clear==2||clear==-1)return clear;if(clear==-2||clear==-3)return 0;
    if(!commit_enabled)return 0;
    const double normal_length=length(collision.normal);if(normal_length<=.001)return -1;
    Point normal=collision.normal;for(auto & component:normal)component=float(float(component)*float(1./normal_length));
    auto offset=[](const Point & p,const Point & n,double amount){Point r;for(unsigned i=0;i<3;++i)r[i]=float(float(p[i])+float(float(n[i])*float(amount)));return r;};
    std::vector<std::pair<Point,Point>> probes{{offset(collision.point,normal,-3.),offset(collision.point,normal,2.)}};
    auto incoming=difference(collision.point,start);const auto n=length(incoming);
    if(n>.001){for(auto & v:incoming)v=float(float(v)*float(1./n));probes.push_back({offset(collision.point,incoming,3.),offset(collision.point,incoming,-2.)});}
    bool accepted=false;
    for(const auto & probe:probes) {
        MaterialHit hit;const auto status=scene_.material(probe.first,probe.second,collision.point,hit);if(status==QueryStatus::Failed)return -1;if(status==QueryStatus::Miss)continue;
        const auto k=hit.hit.surface.key();if(registry_.isolated(k))continue;
        auto row=registry_.instances.find(k);if(row==registry_.instances.end()||row->second.admission!=Admission::Admitted){scene_.refresh(k,registry_);row=registry_.instances.find(k);}
        if(row==registry_.instances.end())continue;
        const auto value=row->second;const auto id=identity(k,value.kind==Kind::Structure?hit.hit.surface.material:-1);
        if(hit.filename!=value.filename||hit.hit.surface.material<71||hit.hit.surface.material>100)continue;
        bool same=false;for(const auto & box:value.boxes)if((value.kind!=Kind::Structure||box.material==material(id))&&contains(box,collision.point)&&contains(box,hit.hit.point))same=true;
        if(!same||!crushable(id,actor_.speed))continue;
        if(commit(id,hit.hit.point,actor_.speed)){accepted=true;break;}
    }
    if(!accepted) {
        std::set<MaterialKey> possible;
        const auto bin=registry_.skin.contact_bins.find(cell(collision.point));
        if(bin!=registry_.skin.contact_bins.end())for(const auto & k:bin->second) {
            const auto row=registry_.instances.find(k);if(row==registry_.instances.end()||registry_.isolated(k)||row->second.admission!=Admission::Admitted)continue;
            for(const auto & box:row->second.boxes)if(contains(box,collision.point))possible.insert(identity(k,row->second.kind==Kind::Structure?box.material:-1));
        }
        if(possible.size()==1&&crushable(*possible.begin(),actor_.speed))accepted=commit(*possible.begin(),collision.point,actor_.speed);
    }
    if(!accepted){budget=recast_budget_;clear=softPath(start,end,collision,true,actor_.allow_kinetic,budget);return clear==1||clear==2?clear:clear==-1?-1:0;}
    crushed=true;Hit recast;const auto status=horizontal(start,end,recast);if(status==QueryStatus::Failed)return -1;if(status==QueryStatus::Miss)return 1;
    budget=recast_budget_;clear=softPath(start,end,recast,false,false,budget);return clear==1?1:clear==-1?-1:0;
}

ContactResult Pipeline::catalogMotion(const g::Pose & pose,double speed,double dt,bool kinetic_commit,const g::Pose * physical,bool spawn,bool proposal_only) {
    if(spawn&&speed!=0.)throw std::invalid_argument("Spawn overlap resolution must be stationary");
    ContactResult out;if(!registry_.catalog_available)return out;
    beginSlice(actor_.now);
    registry_.syncBaked();
    const auto sweep=g::vehicle_box(pose);Box contact;if(physical)contact=g::vehicle_box(*physical);
    const auto missing=registry_.geometry.missing(sweep);
    bool blocked=false,crushed=false,kinetic=false,approach=false;
    for(const auto & unknown:missing) {
        const Key k{std::uint32_t(unknown.first),std::uint32_t(unknown.second)};
        if(registry_.instances.count(k))continue;
        if(scene_.refresh(k,registry_)==Admission::Admitted)continue;
        std::vector<Box> unresolved;
        if(scene_.unresolved(k,unresolved))for(const auto & box:unresolved)
            if(g::intersects(box,sweep)&&!registry_.destroyed(identity(k,box.material))){blocked=true;out.kinds.insert(Kind::Other);}
    }
    const auto found=registry_.geometry.catalog(sweep,physical?&contact:nullptr);
    struct Commit {MaterialKey id;Point point;double speed;bool cap;};std::vector<Commit> commits;
    std::set<MaterialKey> seen;
    for(const auto & candidate:found.grouped) {
        const Key k{std::uint32_t(candidate.key.first),std::uint32_t(candidate.key.second)};
        const auto row=registry_.instances.find(k);if(row==registry_.instances.end()||registry_.isolated(k))continue;
        const auto & value=row->second;if(spawn&&value.kind!=Kind::Fragile)continue;
        out.kinds.insert(value.kind);
        if(candidate.box<0||std::size_t(candidate.box)>=value.boxes.size()){blocked=true;continue;}
        const auto & box=value.boxes[std::size_t(candidate.box)];const auto id=identity(k,value.kind==Kind::Structure?box.material:-1);if(!seen.insert(id).second)continue;
        bool exact=false;if(physical)for(const auto & current:value.boxes)if((value.kind!=Kind::Structure||current.material==material(id))&&g::intersects(contact,current))exact=true;
        if(registry_.destroyed(id)){crushed=true;if(exact)out.contacts.insert(id);continue;}
        const bool physical_crush=crushable(id,speed),cap=actor_.has_kinetic_speed&&crushable(id,actor_.kinetic_speed);
        if(actor_.has_kinetic_speed&&!exact&&!physical_crush){if(cap)approach=true;else blocked=true;continue;}
        if(physical_crush&&actor_.commit_enabled)commits.push_back({id,box.center,speed,false});
        else if(physical_crush){out.contacts.insert(id);blocked=true;}
        else if(cap){out.contacts.insert(id);if(kinetic_commit)commits.push_back({id,box.center,actor_.kinetic_speed,true});else kinetic=true;}
        else blocked=true;
    }
    // Commit independently proved siblings even when another candidate blocks.
    for(const auto & item:commits) {
        if(registry_.destroyed(item.id))continue;
        if(proposal_only){out.contacts.insert(item.id);out.requires_commit=true;out.used_kinetic_speed=out.used_kinetic_speed||item.cap;crushed=true;continue;}
        const auto before=registry_.journal.size();
        // The directional cap admits the contact only. Stock column motion
        // and every published event retain the realised velocity.
        if(!commit(item.id,item.point,speed)){out.status=ContactStatus::Failed;return out;}
        for(std::size_t i=before;i<registry_.journal.size();++i){const auto & event=registry_.journal[i].destruction;out.contacts.insert(identity(event.key,event.material));}
        out.contacts.insert(item.id);out.accepted_now=true;out.used_kinetic_speed=out.used_kinetic_speed||item.cap;crushed=true;
    }
    out.status=blocked?ContactStatus::Hard:kinetic?ContactStatus::Kinetic:crushed?ContactStatus::Crushed:approach?ContactStatus::Approach:ContactStatus::Clear;
    (void)dt;return out;
}
ContactResult Pipeline::trees(const Point & start,double start_yaw,const Point & end,double end_yaw,const Point & minimum,const Point & maximum,double speed,double dt,double pivot,bool mutate,const std::set<MaterialKey> * requested) {
    ContactResult out;
    if(!std::isfinite(speed)||!std::isfinite(dt)||dt<=0.||dt>.25){out.status=ContactStatus::Hard;return out;}
    const auto geometry_start=pivot!=0.&&!mutate?start:rounded(start),geometry_end=pivot!=0.&&!mutate?end:rounded(end);
    const auto sweeps=g::tree_sweep(geometry_start,start_yaw,geometry_end,end_yaw,minimum,maximum,pivot);
    if(sweeps.empty()){out.status=ContactStatus::Hard;return out;}
    std::vector<std::uint32_t> chunks;if(!scene_.treeChunks(sweeps,chunks)){out.status=ContactStatus::Hard;return out;}
    bool pending=false,candidate_pending=false,hard=false;std::set<Key> contacts;
    for(const auto chunk:chunks) {
        if(registry_.skin.isolated_chunks.count(chunk))continue;
        if(!registry_.ready_tree_chunks.count(chunk)){pending=true;continue;}
        for(const auto index:registry_.geometry.trees(chunk,sweeps,.5))contacts.insert({chunk,std::uint32_t(index)});
    }
    if(contacts.size()>64){out.status=ContactStatus::Hard;return out;}
    if(requested) {
        for(const auto & id:*requested)if(material(id)!=-1||!contacts.count(key(id))){out.status=ContactStatus::Hard;return out;}
        contacts.clear();for(const auto & id:*requested)contacts.insert(key(id));
    }
    std::vector<std::pair<MaterialKey,Point>> commits;
    for(const auto & k:contacts) {
        const auto id=identity(k,-1);out.contacts.insert(id);if(registry_.destroyed(id))continue;
        if(registry_.isolated(k)){hard=true;continue;}
        auto row=registry_.instances.find(k);if(row==registry_.instances.end()||row->second.admission!=Admission::Admitted){scene_.refresh(k,registry_);row=registry_.instances.find(k);}
        if(row==registry_.instances.end()||row->second.admission!=Admission::Admitted){candidate_pending=true;continue;}
        const auto & tree=row->second;if(tree.kind!=Kind::Tree||!(tree.tree_health>0.&&tree.tree_health<=1000.)){hard=true;continue;}
        commits.push_back({id,tree.origin});
    }
    if(hard){out.status=ContactStatus::Hard;return out;}
    // No mutation may precede the identity check of another requested tree.
    if(candidate_pending){out.status=ContactStatus::Pending;return out;}
    out.requires_commit=!commits.empty();
    if(!mutate||!actor_.commit_enabled){out.status=pending&&contacts.empty()?ContactStatus::Pending:contacts.empty()?ContactStatus::Clear:ContactStatus::Crushed;return out;}
    const auto native_start=rounded(start),native_end=rounded(end);const double dx=native_end[0]-native_start[0],dz=native_end[2]-native_start[2];
    const double yaw=dx*dx+dz*dz>1.e-16?std::atan2(dx,dz):end_yaw+(speed<0.?3.14159265358979323846:0.);
    for(const auto & item:commits) {
        if(!commit(item.first,item.second,speed,yaw)){out.status=ContactStatus::Pending;return out;}
        out.accepted_now=true;
    }
    out.requires_commit=false;
    out.status=pending&&contacts.empty()?ContactStatus::Pending:contacts.empty()?ContactStatus::Clear:ContactStatus::Crushed;return out;
}
ContactResult Pipeline::occupiedScan(const g::Pose& input,double speed,const std::vector<std::uint32_t>& chunks) {
    if(!std::isfinite(speed))throw std::invalid_argument("Native occupied destructible scan speed is invalid");
    auto pose=input;pose.position=rounded(pose.position);pose.travel=pose.pitch=pose.roll=0.;pose.explicit_motion_yaw=false;
    const auto box=g::vehicle_box(pose);ContactResult out;std::set<std::uint32_t> seen_chunks;
    for(const auto chunk:chunks) {
        if(!seen_chunks.insert(chunk).second||registry_.skin.isolated_chunks.count(chunk)||!registry_.ready_tree_chunks.count(chunk))continue;
        std::set<int> retired;for(const auto& row:registry_.skin.destroyed)if(std::get<0>(row)==chunk&&std::get<2>(row)==-1)retired.insert(int(std::get<1>(row)));
        const auto nearby=registry_.geometry.body(chunk,pose,speed,box,retired);
        for(int item:nearby.items) {
            const Key key{chunk,std::uint32_t(item)};if(registry_.isolated(key)||registry_.destroyed(identity(key,-1)))continue;
            if(scene_.refresh(key,registry_)!=Admission::Admitted)continue;
            const auto row=registry_.instances.find(key);if(row==registry_.instances.end())continue;const auto value=row->second;
            if(value.kind!=Kind::Tree&&(value.kind!=Kind::Falling||!value.boxes.empty()))continue;
            if(!commitOne(identity(key,-1),value.origin,speed,pose.yaw+(speed<0.?3.14159265358979323846:0.))){out.status=ContactStatus::Failed;return out;}
            out.contacts.insert(identity(key,-1));out.kinds.insert(value.kind);out.accepted_now=true;
        }
    }
    out.status=out.accepted_now?ContactStatus::Crushed:ContactStatus::Clear;return out;
}

namespace {
Point rotationEnd(const stock_motion_rotation::Snapshot & request) {
    return {{request.position[0]+request.pivot_offset*(std::cos(request.start_yaw)-std::cos(request.end_yaw))+request.translation[0],
        request.position[1],request.position[2]+request.pivot_offset*(std::sin(request.end_yaw)-std::sin(request.start_yaw))+request.translation[1]}};
}
int priority(ContactStatus status) {
    switch(status){case ContactStatus::Failed:return 7;case ContactStatus::Hard:return 6;case ContactStatus::Pending:return 5;case ContactStatus::Kinetic:return 4;
    case ContactStatus::Crushed:return 3;case ContactStatus::Approach:return 1;case ContactStatus::Clear:return 0;}
    return 7;
}
bool passable(ContactStatus status) {return status==ContactStatus::Clear||status==ContactStatus::Crushed||status==ContactStatus::Approach;}
g::Pose bodyPose(const Point & position,double yaw,const Point & minimum,const Point & maximum,double pitch,double roll,double travel,bool explicit_yaw,double motion_yaw) {
    g::Pose pose;pose.position=position;pose.yaw=yaw;pose.minimum=minimum;pose.maximum=maximum;pose.pitch=pitch;pose.roll=roll;
    pose.travel=travel;pose.explicit_motion_yaw=explicit_yaw;pose.motion_yaw=motion_yaw;return pose;
}
class RestoreActor {
    Pipeline & owner_;ActorInputs value_;
public:
    explicit RestoreActor(Pipeline & owner):owner_(owner),value_(owner.actor()) {}
    ~RestoreActor(){owner_.actor(value_);}
};
}
ContactResult Pipeline::poseSweep(const RotationRequest & request,bool mutate) {
    const auto & input=request.world;stock_motion_rotation::validateSnapshot(input);
    ContactResult result;result.impact_speed=std::abs(actor_.speed);
    if(!registry_.catalog_available)return result;
    const auto end=request.geometry_end.value_or(rotationEnd(input));const auto start=input.position;
    const double delta=stock_motion_rotation::angleDelta(input.start_yaw,input.end_yaw);
    if(std::abs(delta)<=1.e-8)return result;
    const double count=std::ceil(std::abs(delta)/input.max_angle_step);
    if(count<1.||count>input.max_steps)throw std::invalid_argument("Destructible pose sweep exceeds its bound");
    const unsigned steps=unsigned(count);auto body=input.body;
    // Native horizontal collision uses a height shear while preserving X/Z.
    const double cp=std::cos(input.pitch),sp=std::sin(input.pitch),cr=std::cos(input.roll),sr=std::sin(input.roll);
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(const double x:{body.minimum[0],body.maximum[0]})for(const double y:{body.minimum[1],body.maximum[1]})for(const double z:{body.minimum[2],body.maximum[2]}) {
        const double value=cp*sr*x+cp*cr*y-sp*z;low=std::min(low,value);high=std::max(high,value);
    }
    body.minimum[1]=low;body.maximum[1]=high;
    double radius=0.;for(const double x:{body.minimum[0],body.maximum[0]})for(const double z:{body.minimum[2],body.maximum[2]})radius=std::max(radius,std::sqrt(x*x+z*z));
    const double duration=std::max(1.e-6,request.dt),dx=end[0]-start[0],dz=end[2]-start[2];
    const double edge_speed=std::abs(delta)*radius/duration;
    const double center_speed=std::max(std::abs(actor_.speed),std::sqrt(dx*dx+dz*dz)/duration);
    const double magnitude=std::min(200.,std::sqrt(center_speed*center_speed+edge_speed*edge_speed));
    const double impact=actor_.speed<0.?-magnitude:magnitude;result.impact_speed=impact;
    if(magnitude<=1.e-8)return result;
    RestoreActor restore(*this);auto actor=actor_;actor.speed=impact;actor.commit_enabled=true;
    actor.has_kinetic_speed=request.has_rotation_speed_cap;
    if(actor.has_kinetic_speed) {
        if(!std::isfinite(request.rotation_speed_cap)||!std::isfinite(request.drive_speed_cap))throw std::invalid_argument("Destructible rotation speed cap is invalid");
        actor.kinetic_speed=std::abs(request.rotation_speed_cap)*radius;
        if(request.rotation_speed_cap!=0.&&request.has_drive_speed_cap)actor.kinetic_speed=std::max(actor.kinetic_speed,std::abs(request.drive_speed_cap));
    }
    for(unsigned index=0;index<steps;++index) {
        const double lower=double(index)/steps,upper=double(index+1)/steps,middle=(lower+upper)*.5;
        Point a{},b{};for(unsigned i=0;i<3;++i){a[i]=start[i]+(end[i]-start[i])*lower;b[i]=start[i]+(end[i]-start[i])*upper;}
        const double yaw=input.start_yaw+delta*middle;
        const auto interval=stock_motion_rotation::intervalEnvelope(body,std::abs(delta)*.5/steps,input.pivot_offset);
        if(input.pivot_offset) {
            a={{start[0]+input.pivot_offset*(std::cos(input.start_yaw)-std::cos(yaw)),start[1],start[2]+input.pivot_offset*(std::sin(yaw)-std::sin(input.start_yaw))}};b=a;
        }
        const double mx=b[0]-a[0],mz=b[2]-a[2],distance=std::sqrt(mx*mx+mz*mz);
        const bool has_motion=distance>1.e-8;const double motion=has_motion?std::atan2(mx,mz):0.;
        const double slice_dt=has_motion?distance/magnitude:0.;
        auto sweep=bodyPose(rounded(a),yaw,interval.minimum,interval.maximum,0.,0.,impact<0.?-distance:distance,has_motion,motion);
        auto physical=sweep;physical.travel=impact*std::max(0.,slice_dt);
        actor.yaw=yaw;actor_=actor;
        const auto detail=catalogMotion(sweep,impact,slice_dt,true,actor.has_kinetic_speed?&physical:nullptr,false,!mutate);
        result.contacts.insert(detail.contacts.begin(),detail.contacts.end());result.kinds.insert(detail.kinds.begin(),detail.kinds.end());
        result.accepted_now=result.accepted_now||detail.accepted_now;result.used_kinetic_speed=result.used_kinetic_speed||detail.used_kinetic_speed;
        result.requires_commit=result.requires_commit||detail.requires_commit;
        result.pending=result.pending||detail.pending||detail.status==ContactStatus::Pending;
        if(priority(detail.status)>priority(result.status))result.status=detail.status;
        if(result.status==ContactStatus::Hard||result.status==ContactStatus::Failed)break;
    }
    return result;
}
ContactStatus Pipeline::completeTrees(const ContactResult & proposal,const Point & start,double start_yaw,const Point & end,double end_yaw,const Point & minimum,const Point & maximum,double speed,double dt,double pivot,bool mutate,ContactStatus world_status) {
    if(proposal.status==ContactStatus::Pending||proposal.status==ContactStatus::Hard||proposal.status==ContactStatus::Failed||!proposal.requires_commit||!mutate)return world_status;
    const auto committed=trees(start,start_yaw,end,end_yaw,minimum,maximum,speed,dt,pivot,true,&proposal.contacts);
    const bool has_token=std::includes(committed.contacts.begin(),committed.contacts.end(),proposal.contacts.begin(),proposal.contacts.end());
    if(committed.status!=ContactStatus::Crushed||!has_token) {
        if(committed.status==ContactStatus::Hard)tree_retries_.erase(proposal.contacts);
        else if(!tree_retries_.count(proposal.contacts))tree_retries_[proposal.contacts]={start,end,minimum,maximum,start_yaw,end_yaw,speed,dt,pivot,actor_.now,actor_,registry_.epoch};
        return world_status;
    }
    tree_retries_.erase(proposal.contacts);return ContactStatus::Crushed;
}
void Pipeline::retryTreeContacts(double now) {
    using Row=std::pair<std::set<MaterialKey>,TreeRetry>;
    std::vector<Row> due;
    for(auto at=tree_retries_.begin();at!=tree_retries_.end();) {
        if(at->second.epoch!=registry_.epoch){at=tree_retries_.erase(at);continue;}
        if(now-at->second.stamp>=.25)due.push_back(*at);++at;
    }
    std::sort(due.begin(),due.end(),[](const Row & a,const Row & b){return std::tie(a.second.stamp,a.first)<std::tie(b.second.stamp,b.first);});
    RestoreActor restore(*this);
    for(std::size_t i=0;i<std::min<std::size_t>(2,due.size());++i) {
        const auto & row=due[i];tree_retries_.erase(row.first);actor_=row.second.actor;actor_.now=now;
        ContactResult proposal;proposal.contacts=row.first;proposal.status=ContactStatus::Crushed;proposal.requires_commit=true;
        const auto & r=row.second;completeTrees(proposal,r.start,r.start_yaw,r.end,r.end_yaw,r.minimum,r.maximum,r.speed,r.dt,r.pivot,true,ContactStatus::Clear);
    }
}
void Pipeline::beginSlice(double now) {
    if(synchronized_epoch_==registry_.epoch&&synchronized_time_==now)return;
    synchronized_epoch_=registry_.epoch;synchronized_time_=now;
    std::vector<Key> falling;
    for(const auto & row:registry_.instances)if(row.second.kind==Kind::Falling&&registry_.destroyed(identity(row.first,-1)))falling.push_back(row.first);
    for(const auto & k:falling)scene_.refresh(k,registry_);
}
Resolution Pipeline::resolveRotation(const RotationRequest & request) {
    Resolution out;
    if(request.turret&&!scene_.turretSweep(*request.turret)){out.status=ContactStatus::Hard;out.turret_blocked=true;return out;}
    RestoreActor restore(*this);auto actor=actor_;actor.speed=0.;actor_=actor;
    auto detail=poseSweep(request,false);
    if(detail.requires_commit) {
        const auto proposed=detail.contacts;detail=poseSweep(request,true);
        if(!std::includes(detail.contacts.begin(),detail.contacts.end(),proposed.begin(),proposed.end())){out.status=ContactStatus::Failed;out.contact=detail;return out;}
    }
    out.contact=detail;out.status=detail.status;if(!passable(out.status))return out;
    auto input=request.world;input.include_static=true;
    const auto world_result=rotation(input);
    if(!world_result.clear){out.status=world_result.blocking_failure?ContactStatus::Failed:ContactStatus::Hard;out.world_blocked=true;out.trace=world_result.blocking_trace;return out;}
    const auto end=request.geometry_end.value_or(rotationEnd(input));
    const auto tree=trees(input.position,input.start_yaw,end,input.end_yaw,request.hull_minimum,request.hull_maximum,0.,request.dt,input.pivot_offset,false);
    out.status=completeTrees(tree,input.position,input.start_yaw,end,input.end_yaw,request.hull_minimum,request.hull_maximum,0.,request.dt,input.pivot_offset,true,ContactStatus::Clear);
    return out;
}
Resolution Pipeline::resolveMotion(const MotionRequest & request) {
    Resolution out;const auto & input=request.world;
    if(request.turret&&!scene_.turretSweep(*request.turret)){out.status=ContactStatus::Hard;out.turret_blocked=true;return out;}
    RestoreActor restore(*this);auto actor=actor_;actor.speed=input.speed;actor.yaw=input.yaw;actor.allow_kinetic=request.allow_crush_drive;actor.commit_enabled=request.commit_enabled;
    if(!request.allow_crush_drive)actor.has_kinetic_speed=false;actor_=actor;
    const Point start=request.geometry_position.value_or(Point{{input.position[0],input.position[1],input.position[2]}});
    const double angle=input.passive?input.motion_yaw:input.yaw+(input.speed<0.?3.14159265358979323846:0.);
    const double travel=std::abs(input.speed)*input.dt;
    const Point end{{start[0]+std::sin(angle)*travel,start[1],start[2]+std::cos(angle)*travel}};
    ContactResult tree;
    if(!input.airborne)tree=trees(start,input.yaw,end,input.yaw,request.hull_minimum,request.hull_maximum,input.speed,input.dt,0.,false);
    const auto world_result=world(input);
    if(world_result.status<0||world_result.status==1){out.status=world_result.status<0?ContactStatus::Failed:ContactStatus::Hard;out.world_blocked=true;out.trace=world_result.trace;return out;}
    if(input.airborne){out.status=world_result.status==0?ContactStatus::Clear:ContactStatus::Hard;return out;}
    auto sweep=bodyPose(rounded(start),input.yaw,request.body_minimum,request.body_maximum,input.pitch,input.roll,input.speed*std::max(0.,input.dt),input.passive!=0,input.motion_yaw);
    const auto physical=sweep;
    auto detail=catalogMotion(sweep,input.speed,input.dt,request.allow_crush_drive&&request.commit_enabled,actor.has_kinetic_speed?&physical:nullptr);
    out.contact=detail;out.status=detail.status;
    if(!passable(detail.status))return out;
    if(detail.used_kinetic_speed&&(!detail.accepted_now||detail.status!=ContactStatus::Crushed||detail.contacts.empty())){out.status=ContactStatus::Failed;return out;}
    if(detail.accepted_now&&detail.status!=ContactStatus::Crushed){out.status=ContactStatus::Failed;return out;}
    if(detail.accepted_now) {
        actor_.commit_enabled=false;const auto after=world(input);actor_=actor;
        if(after.status<0||after.status==1){out.status=after.status<0?ContactStatus::Failed:ContactStatus::Hard;out.world_blocked=true;out.trace=after.trace;return out;}
    }
    out.status=completeTrees(tree,start,input.yaw,end,input.yaw,request.hull_minimum,request.hull_maximum,input.speed,input.dt,0.,request.commit_enabled,detail.status==ContactStatus::Approach?ContactStatus::Clear:detail.status);
    return out;
}

namespace {
class Frame final:public stock_motion_rotation::WorldFrame {
    Pipeline & owner_;WorldSnapshot snapshot_;stock_motion_rotation::DeparturePolicy * departure_;
    std::set<Key> members_;std::vector<Hit> hits_;stock_motion_rotation::Trace trace_;
    bool prepared_=false,crushed_=false,has_departing_=false;
    std::array<double,15> departing_{};
    std::vector<std::pair<Point,Point>> planes_;
    bool departing(const Hit & hit) {
        if(hit.normal[1]<-.2)return false;
        const Point delta{{departing_[0],departing_[2],departing_[1]}};
        double outward=0.,normal_length2=0.;
        for(unsigned i=0;i<3;++i){outward+=delta[i]*hit.normal[i];normal_length2+=hit.normal[i]*hit.normal[i];}
        const bool drivable=normal_length2>0.&&hit.normal[1]/std::sqrt(normal_length2)>=1./std::sqrt(1.+1.28*1.28);
        if(outward<-1.e-8||(outward<=1.e-8&&!drivable))return false;
        Point relative{},local{},local_delta{};
        for(unsigned i=0;i<3;++i)relative[i]=hit.point[i]-float(snapshot_.position[i]);
        if(snapshot_.passive) {
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){local[i]+=departing_[6+i*3+j]*relative[j];local_delta[i]+=departing_[6+i*3+j]*delta[j];}
        } else {
            if(std::abs(departing_[4])<.1)return false;
            const double sine=std::sin(snapshot_.yaw),cosine=std::cos(snapshot_.yaw);
            local[0]=relative[0]*cosine-relative[2]*sine;local[2]=relative[0]*sine+relative[2]*cosine;
            local[1]=(relative[1]-local[0]*departing_[3]-local[2]*departing_[5])/departing_[4];
        }
        const Point lower{{snapshot_.bounds[0],snapshot_.heights[0],-snapshot_.bounds[2]}},upper{{snapshot_.bounds[1],snapshot_.heights[1],snapshot_.bounds[3]}};
        bool inside=true;for(unsigned i=0;i<3;++i)inside=inside&&lower[i]-.001<=local[i]&&local[i]<=upper[i]+.001;
        if(!inside&&snapshot_.passive&&outward>1.e-8) {
            double low=0.,high=1.;
            for(unsigned i=0;i<3;++i) {
                if(std::abs(local_delta[i])<1.e-12){if(!(lower[i]-.001<=local[i]&&local[i]<=upper[i]+.001)){high=-1.;break;}}
                else {const double a=(local[i]-upper[i]-.001)/local_delta[i],b=(local[i]-lower[i]+.001)/local_delta[i];low=std::max(low,std::min(a,b));high=std::min(high,std::max(a,b));}
            }
            inside=low<=high;
        }
        if(inside){planes_.push_back({hit.point,hit.normal});return true;}
        for(const auto & plane:planes_) {
            double alignment=0.,separation=0.;for(unsigned i=0;i<3;++i){alignment+=hit.normal[i]*plane.second[i];separation+=(hit.point[i]-plane.first[i])*hit.normal[i];}
            if(alignment>=.9999&&std::abs(separation)<=.001)return true;
        }
        return false;
    }
public:
    Frame(Pipeline & owner,const WorldSnapshot & snapshot,stock_motion_rotation::DeparturePolicy * departure):owner_(owner),snapshot_(snapshot),departure_(departure) {}
    int dispatch(std::uint32_t op,const WorldRequest * requests,std::uint32_t count,WorldResponse * responses) override {
        bool stopped=false;
        for(unsigned i=0;i<count;++i) {
            const auto & request=requests[i];auto & response=responses[i];response={};
            const Point a=rounded({{request.start[0],request.start[1],request.start[2]}}),b=rounded({{request.end[0],request.end[1],request.end[2]}});
            std::copy(a.begin(),a.end(),response.start);std::copy(b.begin(),b.end(),response.end);
            if(op==WORLD_FILTER){members_=members(owner_.registry(),a,b);prepared_=true;continue;}
            if(op==WORLD_DEPARTING){std::copy(request.auxiliary,request.auxiliary+15,departing_.begin());planes_.clear();has_departing_=true;continue;}
            if(op==WORLD_HORIZONTAL||op==WORLD_GROUND) {
                if(stopped||(request.flags&4)){stopped=stopped||(op==WORLD_GROUND&&(request.flags&1));continue;}
                Hit hit;auto current=a;QueryStatus status=QueryStatus::Miss;
                for(unsigned pass=0;pass<=4;++pass) {
                    status=op==WORLD_GROUND?owner_.ground(current,b,hit,prepared_?&members_:nullptr):owner_.horizontal(current,b,hit,prepared_?&members_:nullptr);
                    if(status!=QueryStatus::Found||op!=WORLD_HORIZONTAL||!(request.flags&1)||(!departure_&&!has_departing_)||!(departure_?departure_->accepts({hit.point,hit.normal}):departing(hit)))break;
                    if(distance(nullptr,b,hit.point)<=.001){status=QueryStatus::Miss;break;}
                    current=advanced(hit.point,b,.001);
                }
                if(status==QueryStatus::Failed)return 0;
                if(status==QueryStatus::Found){response.status=1;response.handle=hits_.size()+1;hits_.push_back(hit);std::copy(hit.point.begin(),hit.point.end(),response.point);std::copy(hit.normal.begin(),hit.normal.end(),response.normal);}
                stopped=stopped||(op==WORLD_GROUND&&!response.status&&(request.flags&1));continue;
            }
            if(request.handle==0||request.handle>hits_.size())return 0;
            const auto & hit=hits_[std::size_t(request.handle-1)];
            if(op==WORLD_RESOLVE) {response.status=owner_.resolve(a,b,hit,crushed_,owner_.actor().commit_enabled);if(response.status<0)return 0;}
            else if(op==WORLD_TRACE) {trace_.present=true;trace_.snapshot=snapshot_;trace_.request=request;trace_.response={};trace_.response.handle=request.handle;trace_.response.status=1;std::copy(hit.point.begin(),hit.point.end(),trace_.response.point);std::copy(hit.normal.begin(),hit.normal.end(),trace_.response.normal);}
            else return 0;
        }
        return 1;
    }
    stock_motion_rotation::Trace trace() const override{return trace_;}
};
}
std::unique_ptr<stock_motion_rotation::WorldFrame> Pipeline::begin(const WorldSnapshot & snapshot,stock_motion_rotation::DeparturePolicy * policy) { return std::unique_ptr<stock_motion_rotation::WorldFrame>(new Frame(*this,snapshot,policy)); }
stock_motion_rotation::WorldResult Pipeline::world(const WorldSnapshot & snapshot,stock_motion_rotation::DeparturePolicy * policy) {
    stock_motion_rotation::WorldStageEngine engine(*this);return engine.run(snapshot,policy);
}
stock_motion_rotation::Result Pipeline::rotation(const stock_motion_rotation::Snapshot & snapshot) {
    stock_motion_rotation::WorldStageEngine engine(*this);const auto old=actor_.commit_enabled;actor_.commit_enabled=false;
    try {auto result=stock_motion_rotation::run(snapshot,engine);actor_.commit_enabled=old;return result;}
    catch(...){actor_.commit_enabled=old;throw;}
}
}
