#include "stock_native_projectile_scene.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace stock_native_projectile {
namespace world=stock_native_world;
namespace {
using Identity=world::MaterialKey;
using ItemKey=world::Key;
Point native(Point p){for(double& x:p)x=static_cast<float>(x);return p;}
Point subtract(const Point&a,const Point&b){return native({a[0]-b[0],a[1]-b[1],a[2]-b[2]});}
double magnitude(const Point&p){const float x=float(p[0]),y=float(p[1]),z=float(p[2]);return std::sqrt(float(float(x*x+y*y)+z*z));}
double distance(const Point&a,const Point&b){return magnitude(subtract(a,b));}
Point offset(const Point&a,const Point&v,double d){Point p;const float f=float(d);for(unsigned i=0;i<3;++i)p[i]=float(float(a[i])+float(float(v[i])*f));return p;}
Identity identity(const ItemKey& k,std::int64_t m){return {k.first,k.second,m};}
bool destructible(std::uint32_t m){return m>=71&&m<=100;}
bool replacement(std::uint32_t m){return m>=87&&m<=100;}
bool keep(const std::set<Identity>& ignored,const world::Surface&s){return replacement(s.material)||(!ignored.count(identity(s.key(),-1))&&!ignored.count(identity(s.key(),s.material)));}
bool ap(const std::string&k){return k=="ARMOR_PIERCING"||k=="ARMOR_PIERCING_HE"||k=="ARMOR_PIERCING_CR";}
bool pole(const world::Instance&i){
    if(i.kind!=world::Kind::Falling)return false;
    std::string name=i.filename;for(char& c:name){if(c=='\\')c='/';else if(c>='A'&&c<='Z')c=char(c-'A'+'a');}
    std::vector<std::string> parts;std::size_t at=0;for(;;){const auto next=name.find('/',at);parts.push_back(name.substr(at,next-at));if(next==std::string::npos)break;at=next+1;}
    if(parts.size()!=6||parts[0]!="content"||parts[1]!="environment"||parts[3]!="normal"||parts[4]!="lod0")return false;
    static const std::set<std::string> families={"env413_streetlamp","env414_pole","env423_streetlamp","envam_006_streetlamps","envam_009_poles","envam_018_lamppost","enveu_013_streetlights","envsu_83_02_trampost","env_112_09_streetlamp"};
    return families.count(parts[2])||(parts[2]=="envf_001_factoryclock"&&(parts[5]=="envf_008_factorylamppost.model"||parts[5]=="envf_009_factorystreetlamp.model"));
}
void checked(world::QueryStatus q){if(q==world::QueryStatus::Failed)throw std::runtime_error("Native projectile scene query failed");}
}
struct ShotScene::Contact {Identity id{};double distance=0,exit=0;bool ambiguous=false;world::MaterialHit material;};
struct ShotScene::Step {double world=99999,loss=0;std::optional<double> stop,advance,loss_distance;bool destructible=false;std::string reason;};
std::optional<Identity> ShotScene::transparent(const world::MaterialHit&hit,bool trees){
    const auto&s=hit.hit.surface;if(!destructible(s.material)||replacement(s.material)||registry_.isolated(s.key()))return {};
    if(trees){
        auto i=registry_.instances.find(s.key());if(i==registry_.instances.end()){scene_.refresh(s.key(),registry_);i=registry_.instances.find(s.key());}
        if(i!=registry_.instances.end()&&i->second.kind==world::Kind::Tree&&i->second.descriptor_valid&&i->second.admission==world::Admission::Admitted&&i->second.tree_health>0&&i->second.tree_health<=1000&&i->second.native_name_exact&&i->second.filename==hit.filename)return identity(s.key(),-1);
    }
    if(registry_.destroyed(identity(s.key(),-1)))return identity(s.key(),-1);
    if(registry_.destroyed(identity(s.key(),s.material)))return identity(s.key(),s.material);
    return {};
}
world::QueryStatus ShotScene::material(const Point&a,const Point&b,world::MaterialHit&out){
    std::set<Identity> ignored;
    for(unsigned attempt=0;attempt<65;++attempt){
        const auto q=scene_.materialFiltered(a,b,a,[&](const world::Surface&s){return keep(ignored,s);},out);checked(q);
        if(q!=world::QueryStatus::Found)return q;
        const auto skip=transparent(out,true);if(!skip)return q;
        if(ignored.size()>=64||!ignored.insert(*skip).second)throw std::runtime_error("Native projectile material filter did not advance");
    }
    throw std::runtime_error("Native projectile material traversal exceeded 64 hits");
}
std::optional<ShotScene::Contact> ShotScene::catalog(const Point&a,const Point&b,std::optional<double>cap){
    const auto segment=subtract(b,a);const double n=magnitude(segment);if(n<=1e-9)return {};
    Point end=b;if(cap&&*cap<n)end=offset(a,segment,std::min(n,std::max(0.,*cap+1e-6))/n);
    const int x0=int(std::floor(std::min(a[0],end[0])/8)),x1=int(std::floor(std::max(a[0],end[0])/8));
    const int z0=int(std::floor(std::min(a[2],end[2])/8)),z1=int(std::floor(std::max(a[2],end[2])/8));
    std::set<ItemKey> ids;for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)for(const auto* bins:{&registry_.skin.contact_bins,&registry_.skin.baked_shot_bins}){const auto found=bins->find({x,z});if(found!=bins->end())ids.insert(found->second.begin(),found->second.end());}
    std::map<Identity,Contact> hits;world::MaterialHit precise;bool sampled=false;world::QueryStatus status=world::QueryStatus::Miss;
    for(const auto&id:ids){
        if(registry_.isolated(id))continue;
        auto found=registry_.instances.find(id);bool pending=false;const world::Instance* instance=nullptr;
        if(found==registry_.instances.end()){
            const auto baked=registry_.baked_instances.find(id);if(baked==registry_.baked_instances.end())continue;
            bool intersects=false;for(const auto&box:baked->second.boxes){if(registry_.destroyed(identity(id,baked->second.kind==world::Kind::Structure?box.material:-1)))continue;const auto probe=offline_destructible::probe_boxes({box},a,a,b,baked->second.broad_phase_margin,2).front();if(probe.segment.valid&&(!cap||probe.segment.entry*n<=*cap+1e-6)){intersects=true;break;}}
            if(!intersects)continue;
            scene_.refresh(id,registry_);if(registry_.isolated(id))continue;found=registry_.instances.find(id);
            if(found==registry_.instances.end()){instance=&baked->second;pending=true;}
        }
        if(!instance)instance=&found->second;
        for(const auto&box:instance->boxes){
            const auto probe=offline_destructible::probe_boxes({box},a,a,b,0.,2).front();if(!probe.segment.valid)continue;
            const auto key=identity(id,instance->kind==world::Kind::Structure?box.material:-1);if(registry_.destroyed(key))continue;
            if(!sampled){status=material(a,b,precise);sampled=true;}
            if(status!=world::QueryStatus::Found||precise.hit.surface.key()!=id||(instance->kind==world::Kind::Structure&&box.material!=int(precise.hit.surface.material)))continue;
            const auto&p=precise.hit.point;double d=0;for(unsigned i=0;i<3;++i)d+=(p[i]-a[i])*segment[i];d/=n;
            if(d<0||d>n+1e-6||distance(p,offset(a,segment,d/n))>1e-4||!offline_destructible::probe_boxes({box},p,a,b,0.,1).front().inside||(cap&&d>*cap+1e-6))continue;
            auto old=hits.find(key);if(old==hits.end()||d<old->second.distance)hits[key]={key,d,probe.segment.leave*n,pending,precise};
        }
    }
    if(hits.empty())return {};
    const auto nearest=std::min_element(hits.begin(),hits.end(),[](const auto&a,const auto&b){return a.second.distance<b.second.distance;});auto out=nearest->second;unsigned count=0;for(const auto&row:hits)if(std::abs(row.second.distance-out.distance)<=.075)++count;
    out.ambiguous=out.ambiguous||count!=1;return out;
}
std::optional<double> ShotScene::registeredExit(const world::MaterialHit&hit,const Point&a,const Point&b){
    const auto&s=hit.hit.surface;if(registry_.isolated(s.key()))return {};const auto found=registry_.instances.find(s.key());if(found==registry_.instances.end()||found->second.filename!=hit.filename)return {};
    const double n=distance(a,b),d=distance(a,hit.hit.point);if(n<=1e-9)return {};std::vector<double> exits;
    for(const auto&box:found->second.boxes){if(found->second.kind==world::Kind::Structure&&box.material!=int(s.material))continue;const auto q=offline_destructible::probe_boxes({box},hit.hit.point,a,b,0.,2).front().segment;if(q.valid&&q.entry*n<=d+.075&&q.leave*n+.075>=d)exits.push_back(q.leave*n);}
    return exits.size()==1?std::optional<double>(exits.front()):std::nullopt;
}
bool ShotScene::destroy(const world::MaterialHit&hit,double yaw,double now,std::vector<world::Destruction>&events){
    const auto&s=hit.hit.surface;if(!destructible(s.material)||registry_.isolated(s.key()))return false;
    auto found=registry_.instances.find(s.key());if(found==registry_.instances.end()){scene_.refresh(s.key(),registry_);found=registry_.instances.find(s.key());}
    if(found==registry_.instances.end())return false;const auto value=found->second;
    if(value.kind==world::Kind::Tree||value.kind==world::Kind::Other||value.admission!=world::Admission::Admitted||!value.descriptor_valid||value.filename!=hit.filename)return false;
    const auto id=identity(s.key(),value.kind==world::Kind::Structure?std::int64_t(s.material):-1);
    if(registry_.destroyed(id))return false;
    if(value.kind==world::Kind::Structure&&(s.material<73||s.material>=86||std::none_of(value.boxes.begin(),value.boxes.end(),[&](const auto&box){return box.material==int(s.material);})))return false;
    world::Destruction order;order.kind=value.kind;order.key=s.key();order.material=std::get<2>(id);order.position=native(hit.hit.point);order.yaw=yaw;order.speed=12;order.now=now;order.shot=true;
    events.reserve(events.size()+14);
    const auto commit=[&](world::Destruction item){
        if(!scene_.destroy(item))throw std::runtime_error("Native projectile destruction was not accepted");
        const auto accepted=identity(item.key,item.material);registry_.skin.destroyed.insert(accepted);registry_.pending[accepted]=now;++registry_.generation;events.push_back(item);
    };
    commit(order);
    if(value.kind==world::Kind::Structure&&value.reference_health.count(order.material)&&value.reference_health.size()<=13){
        const double root=value.reference_health.at(order.material);std::set<std::int64_t> closure{order.material};std::vector<std::int64_t> pending{order.material};
        while(!pending.empty()){const auto current=pending.back();pending.pop_back();for(unsigned reverse=0;reverse<2;++reverse){const auto&table=reverse?value.inverse_dependencies:value.dependencies;const auto row=table.find(current);if(row==table.end())continue;for(const auto child:row->second){const auto health=value.reference_health.find(child);if(health==value.reference_health.end())throw std::runtime_error("Native projectile module dependency unavailable");if(reverse&&(!std::isfinite(root)||!std::isfinite(health->second)||health->second<0||health->second>root))continue;if(closure.insert(child).second)pending.push_back(child);}}}
        closure.erase(order.material);for(const auto child:closure)if(!registry_.destroyed(identity(order.key,child))){auto item=order;item.material=child;commit(item);}
    }
    if(value.kind==world::Kind::Falling)scene_.refresh(s.key(),registry_);
    return true;
}
ShotScene::Step ShotScene::one(const Point&a,const Point&b,const Point&direction,const damage::FrozenShot&shot,double now,std::vector<world::Destruction>&events){
    std::set<Identity> ignored;const auto filter=[&](const world::Surface&s){return keep(ignored,s);};
    world::Hit world_hit;auto status=scene_.segment(a,b,128,{},world_hit);checked(status);double world_distance=99999.;
    std::optional<world::MaterialHit> decoded;std::optional<Contact> candidate;
    const double yaw=std::atan2(direction[0],direction[2]);
    const auto continueBroken=[&](const world::MaterialHit&hit,double obstacle){
        const auto id=transparent(hit,false);if(!id)return obstacle+.6;
        ignored.insert(*id);world::Hit next;const auto result=scene_.segment(a,b,128,filter,next);checked(result);
        double maximum=result==world::QueryStatus::Found?distance(a,next.point):distance(a,b);
        const auto catalog_hit=catalog(a,b,maximum);if(catalog_hit)maximum=std::min(maximum,catalog_hit->distance);
        const double floor=obstacle+1e-4;
        return std::max(floor,maximum-((result!=world::QueryStatus::Found&&!catalog_hit)?0.:1e-4));
    };
    const auto accepted=[&](const world::MaterialHit&hit,double obstacle,std::optional<double>exit,bool catalog_path){
        Step result;const auto row=registry_.instances.find(hit.hit.surface.key());
        const world::Instance* instance=row==registry_.instances.end()?nullptr:&row->second;
        if(instance&&pole(*instance)&&catalog_path){result.advance=obstacle+1e-4;return result;}
        std::optional<double> health;if(instance){const auto h=instance->scaled_health.find(instance->kind==world::Kind::Structure?std::int64_t(hit.hit.surface.material):-1);if(h!=instance->scaled_health.end())health=h->second;}
        if(ap(shot.shell.kind)&&health&&*health<=19.){result.advance=exit?*exit+1e-4:continueBroken(hit,obstacle);result.loss=25.;result.loss_distance=obstacle;return result;}
        result.world=obstacle;result.stop=obstacle;result.destructible=true;result.reason=!ap(shot.shell.kind)?"shell_family":!health?"health_unavailable":"above_threshold_hp";return result;
    };
    unsigned skipped=0;
    while(status==world::QueryStatus::Found){
        world_distance=distance(a,world_hit.point);world::MaterialHit info;
        auto mq=scene_.material(a,offset(world_hit.point,direction,.3),world_hit.point,info);checked(mq);decoded=mq==world::QueryStatus::Found?std::optional<world::MaterialHit>(info):std::nullopt;
        // A nameless HE/APHE point query can recover only one coincident exact
        // native surface. Callback visitation order never proves ownership.
        if(registry_.catalog_available&&(shot.shell.kind=="HIGH_EXPLOSIVE"||shot.shell.kind=="ARMOR_PIERCING_HE")&&(!decoded||decoded->filename.empty())){
            std::set<world::Surface> surfaces;world::Hit replay;const auto collect=[&](const world::Surface&s){if(!filter(s))return false;if(s.material>=71&&!replacement(s.material)&&surfaces.size()<17)surfaces.insert(s);return true;};
            const auto replay_status=scene_.segment(a,b,128,collect,replay);checked(replay_status);
            if(surfaces.size()<=16&&replay_status==world::QueryStatus::Found&&distance(replay.point,world_hit.point)<=1e-4){
                std::vector<world::MaterialHit> matches;for(const auto&s:surfaces){world::Hit exact;const auto q=scene_.segment(a,b,128,[&](const world::Surface&v){return v==s;},exact);checked(q);if(q!=world::QueryStatus::Found||distance(exact.point,world_hit.point)>1e-4||registry_.isolated(s.key()))continue;
                    auto row=registry_.instances.find(s.key());if(row==registry_.instances.end()){scene_.refresh(s.key(),registry_);row=registry_.instances.find(s.key());}
                    if(row!=registry_.instances.end()&&row->second.admission==world::Admission::Admitted&&!row->second.filename.empty()){world::MaterialHit match;match.hit=exact;match.hit.surface=s;match.filename=row->second.filename;matches.push_back(match);}}
                if(matches.size()==1)decoded=matches.front();
            }
        }
        std::optional<Identity> tree;if(decoded){const auto transparent_key=transparent(*decoded,true);const auto row=registry_.instances.find(decoded->hit.surface.key());if(row!=registry_.instances.end()&&row->second.kind==world::Kind::Tree)tree=transparent_key;}
        if(tree){candidate=catalog(a,b,world_distance);if(candidate)break;}
        const bool did_destroy=decoded&&!tree&&destroy(*decoded,yaw,now,events);
        std::optional<Identity> skip=tree;
        if(did_destroy){const auto found=registry_.instances.find(decoded->hit.surface.key());if(found!=registry_.instances.end()&&pole(found->second))skip=identity(found->first,-1);}
        else if(!skip&&decoded)skip=transparent(*decoded,false);
        if(skip){if(!ignored.insert(*skip).second||++skipped>64)throw std::runtime_error("Native transparent projectile traversal did not advance");status=scene_.segment(a,b,128,filter,world_hit);checked(status);continue;}
        if(did_destroy)return accepted(*decoded,world_distance,registeredExit(*decoded,a,b),false);
        break;
    }
    if(status!=world::QueryStatus::Found){world_distance=99999.;decoded.reset();}
    if(!candidate&&status==world::QueryStatus::Found&&decoded&&distance(decoded->hit.point,world_hit.point)<=1e-4){
        const auto&s=decoded->hit.surface;const auto row=registry_.instances.find(s.key());
        if(row!=registry_.instances.end()&&row->second.admission==world::Admission::Admitted&&row->second.filename==decoded->filename&&!registry_.isolated(s.key())){
            const auto id=identity(s.key(),row->second.kind==world::Kind::Structure?std::int64_t(s.material):-1);
            if(!registry_.destroyed(id)){
                // Exact point ownership still requires a matching registered
                // box, as in the fixed source's native-identity candidate.
                unsigned matches=0;for(const auto&box:row->second.boxes)if((row->second.kind!=world::Kind::Structure||box.material==int(s.material))&&offline_destructible::probe_boxes({box},decoded->hit.point,a,b,.075,1).front().inside)++matches;
                if(matches){const auto exit=registeredExit(*decoded,a,b);candidate=Contact{id,world_distance,exit.value_or(world_distance),false,*decoded};if(!exit)candidate->exit=-1.;}
            }
        }
    }
    if(!candidate)candidate=catalog(a,b,status==world::QueryStatus::Found?std::optional<double>(world_distance):std::nullopt);
    if(!candidate){Step out;out.world=world_distance;if(status==world::QueryStatus::Found){out.stop=world_distance;out.reason="catalog_miss";}return out;}
    if(candidate->ambiguous){Step out;out.world=candidate->distance;out.stop=candidate->distance;out.destructible=true;out.reason="catalog_ambiguous";return out;}
    if(!destroy(candidate->material,yaw,now,events)){Step out;out.world=world_distance;if(status==world::QueryStatus::Found){out.stop=world_distance;out.reason="native_reject";}return out;}
    return accepted(candidate->material,candidate->distance,candidate->exit>=0?std::optional<double>(candidate->exit):std::nullopt,true);
}
SceneResult ShotScene::trace(FlightMeta&meta,const Point&start,const Point&end,const Point&direction,double now,damage::Random&random,std::vector<world::Destruction>&events){
    const Point a=native(start),b=native(end),dir=native(direction);const double maximum=distance(a,b);double travelled=0;
    for(unsigned i=0;i<64;++i){const auto result=one(offset(a,dir,travelled),b,dir,meta.shot,now,events);meta.piercing_loss+=std::max(0.,result.loss);
        if(result.loss>0&&!meta.shot.penetration_factor)damage::freezePenetration(meta.shot,random);
        const double obstacle=travelled+std::max(0.,result.loss_distance.value_or(result.advance.value_or(0.)));
        if(result.advance&&meta.shot.penetration_factor){const auto p=offset(a,dir,obstacle);const double range=length(meta.range_origin,p);const double available=stock_native_bot_armor::piercing(meta.shot.shell,range)*(*meta.shot.penetration_factor)*meta.shot.penetration_multiplier-meta.piercing_loss;if(available<1.)return {obstacle,true,"penetration_exhausted"};}
        if(result.stop)return {travelled+std::max(0.,*result.stop),result.destructible,result.reason};
        if(!result.advance)return {result.world<99999?travelled+result.world:999999.,false,result.reason};
        if(!(*result.advance>0))throw std::runtime_error("Native projectile destructible traversal did not advance");travelled+=*result.advance;if(travelled>=maximum)return {};
    }
    throw std::runtime_error("Native projectile destructible traversal exceeded 64 hits");
}
}
