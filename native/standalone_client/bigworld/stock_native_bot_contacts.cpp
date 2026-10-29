#include "stock_native_bot_contacts.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_contacts {
namespace {
Pair pair(ID a,ID b){return {std::min(a,b),std::max(a,b)};}
double norm2(const Point& p){return p[0]*p[0]+p[1]*p[1]+p[2]*p[2];}
std::array<std::array<double,2>,2> axes(double yaw)
{const double s=std::sin(yaw),c=std::cos(yaw);return {{{{c,-s}},{{s,c}}}};}
std::array<double,2> vertical(const Body& a)
{
    const double cp=std::cos(a.pitch),sp=std::sin(a.pitch),cr=std::cos(a.roll),sr=std::sin(a.roll);
    const double up=cp*cr,center=a.y+(a.shape[2]+a.shape[3])*.5*up;
    const double extent=std::abs(cp*sr)*a.shape[0]+std::abs(sp)*a.shape[1]+
        std::abs(up)*(a.shape[3]-a.shape[2])*.5;
    return {{center-extent,center+extent}};
}
std::array<double,3> oriented(std::array<double,3> hit,const Body& a,const Body& b)
{
    if(std::abs((a.x-b.x)*hit[0]+(a.z-b.z)*hit[1])<=1.e-9) {
        if(hit[0]<-1.e-9||(std::abs(hit[0])<=1.e-9&&hit[1]<0.)){hit[0]=-hit[0];hit[1]=-hit[1];}
        if(a.id>b.id){hit[0]=-hit[0];hit[1]=-hit[1];}
    }
    return hit;
}
std::optional<int> initiator(const Body& a,const Body& b,const std::array<double,3>& n)
{
    const double length=std::hypot(n[0],n[1]);if(length<=0.)return {};
    const double first=-(a.vx*n[0]/length+a.vz*n[1]/length);
    const double second=b.vx*n[0]/length+b.vz*n[1]/length;
    if(first+second<=.05)return {};
    if(first>.05&&first>std::max(0.,second)+.05)return 0;
    if(second>.05&&second>std::max(0.,first)+.05)return 1;
    return {};
}
bool profile_valid(const Profile& p)
{return std::isfinite(p.spall)&&std::isfinite(p.bonus)&&p.spall>=1.&&p.bonus>=0.&&p.bonus<=.15;}
double inertia(const Actor& a)
{
    if(a.motion->alive)return 0.;
    const auto& d=*a.descriptor;
    return d.drive.mass*(std::pow(d.collision_shape[0],2.)+std::pow(d.collision_shape[1],2.))/3.;
}
bool valid_checkpoints(const std::vector<Checkpoint>& values)
{
    if(values.size()>30)return false;
    std::set<ID> ids;
    for(const auto& v:values) {
        if(v.actor<1||v.actor>2147483647||v.sequence<1||v.sequence>2147483647||!ids.insert(v.actor).second)return false;
        for(double n:v.totals)if(!std::isfinite(n)||std::abs(n)>1.e12)return false;
    }
    return true;
}
}
bool vertical_overlap(const Body& a,const Body& b)
{
    if(!a.has(offline_contact::HasY)||!b.has(offline_contact::HasY))return true;
    const auto x=vertical(a),y=vertical(b);
    return std::min(x[1],y[1])-std::max(x[0],y[0])>.02;
}
std::optional<std::array<double,3>> overlap(const Body& a,const Body& b)
{
    const auto aa=axes(a.yaw),bb=axes(b.yaw);
    std::optional<std::array<double,3>> best;
    for(const auto axis:{aa[0],aa[1],bb[0],bb[1]}) {
        const auto dot=[&](const std::array<double,2>& v){return axis[0]*v[0]+axis[1]*v[1];};
        const double ra=a.shape[0]*std::abs(dot(aa[0]))+a.shape[1]*std::abs(dot(aa[1]));
        const double rb=b.shape[0]*std::abs(dot(bb[0]))+b.shape[1]*std::abs(dot(bb[1]));
        const double distance=(a.x-b.x)*axis[0]+(a.z-b.z)*axis[1],depth=ra+rb-std::abs(distance);
        if(!best||depth<(*best)[2])best=std::array<double,3>{{distance<0.?-axis[0]:axis[0],distance<0.?-axis[1]:axis[1],depth}};
    }
    return best&&(*best)[2]>0.?best:std::nullopt;
}
bool contains(const Body& body,const Point& point,double margin)
{
    const double sy=std::sin(body.yaw),cy=std::cos(body.yaw),sp=std::sin(body.pitch),cp=std::cos(body.pitch);
    const double sr=std::sin(body.roll),cr=std::cos(body.roll);
    const auto rotate=[&](double x,double y,double z) {
        const double yy=cp*y-sp*z,zz=sp*y+cp*z;
        return Point{{cy*x+sy*zz,yy,-sy*x+cy*zz}};
    };
    const std::array<Point,3> basis{{rotate(cr,sr,0.),rotate(-sr,cr,0.),rotate(0.,0.,1.)}};
    const Point delta{{point[0]-body.x,point[1]-body.y,point[2]-body.z}};
    Point local{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)local[i]+=basis[i][j]*delta[j];
    margin=std::max(0.,margin);
    return std::abs(local[0])<=body.shape[0]+margin&&local[1]>=body.shape[2]-margin&&
        local[1]<=body.shape[3]+margin&&std::abs(local[2])<=body.shape[1]+margin;
}
std::array<std::int64_t,2> damage(double speed,double own_mass,double other_mass,
    double own_armor,double other_armor,const Profile& own,const Profile& other,bool own_moving,bool other_moving)
{
    speed=std::abs(speed);
    const double a=std::max(0.,own_mass)*.001,b=std::max(0.,other_mass)*.001,total=a+b;
    if(total<=0.||speed<=0.)return {{0,0}};
    const double potential=.5*total*speed*speed;
    double raw_a=std::max(0.,.5*(potential*(b/total))-1.1*std::max(0.,own_armor)*std::max(1.,own.spall));
    double raw_b=std::max(0.,.5*(potential*(a/total))-1.1*std::max(0.,other_armor)*std::max(1.,other.spall));
    const double bonus_a=std::max(0.,std::min(.15,own.bonus)),bonus_b=std::max(0.,std::min(.15,other.bonus));
    if(own_moving){raw_a*=1.-bonus_a;raw_b*=1.+bonus_a;}
    if(other_moving){raw_b*=1.-bonus_b;raw_a*=1.+bonus_b;}
    return {{std::int64_t(raw_b*.25),std::int64_t(raw_a*.25)}};
}
offline_math::Body geometry(const Body& b)
{return {b.id,b.has(offline_contact::HasY),b.x,b.y,b.z,b.yaw,b.pitch,b.roll,b.shape};}
Body project(const Actor& a,double dt)
{
    const auto& s=*a.motion;const auto& d=*a.descriptor;
    const double speed=s.alive?s.speed:0.;
    const auto grip=stock_native_bot_motion::contact_push_deceleration(s,d,s.alive&&(speed||s.movement_direction));
    const auto traverse=stock_native_bot_motion::contact_traverse(s,d,
        s.alive?s.contact_motor_turn.value_or(s.rotation_direction):0,dt,s.movement_direction);
    Body body;body.id=s.id;body.team=a.team;body.flags=offline_contact::Impulse|offline_contact::HasY|offline_contact::HasGrip;
    if(s.alive)body.flags|=offline_contact::Alive;
    body.x=s.position[0];body.y=s.position[1];body.z=s.position[2];body.yaw=s.yaw;
    body.pitch=s.pitch;body.roll=s.roll;body.mass=d.drive.mass;body.shape=d.collision_shape;
    body.vx=std::sin(s.yaw)*speed+s.push_x;body.vz=std::cos(s.yaw)*speed+s.push_z;
    body.vy=a.ram_vertical_speed.value_or(s.vertical_speed);body.grip[0]=grip[0];body.grip[1]=grip[1];
    body.traverse_speed=traverse[0];body.traverse_torque=traverse[1];body.push_yaw=s.push_yaw;
    return body;
}
std::vector<ID> python27_integer_order(const std::vector<ID>& insertions)
{
    std::vector<std::optional<ID>> table(8);std::size_t used=0;
    const auto insert=[](std::vector<std::optional<ID>>& slots,ID key) {
        const std::uint32_t hash=std::uint32_t(key==-1?-2:key);
        std::uint32_t perturb=hash;std::size_t index=hash&(slots.size()-1);
        while(slots[index]&&*slots[index]!=key) {
            index=(index*5+perturb+1)&(slots.size()-1);perturb>>=5;
        }
        if(slots[index])return false;
        slots[index]=key;return true;
    };
    for(ID key:insertions) {
        if(key<-2147483647LL-1||key>2147483647LL)throw std::invalid_argument("Contact body ID exceeds CPython x86 integer range");
        if(!insert(table,key))continue;
        ++used;
        if(used*3<table.size()*2)continue;
        const std::size_t minimum=used*(used>50000?2:4);std::size_t size=8;
        while(size<=minimum)size*=2;
        std::vector<std::optional<ID>> resized(size);
        for(const auto& old:table)if(old)insert(resized,*old);
        table.swap(resized);
    }
    std::vector<ID> out;for(const auto& key:table)if(key)out.push_back(*key);return out;
}
bool wreck_active(const Actor& a)
{
    const auto& s=*a.motion;const auto& d=*a.descriptor;
    const double tangent=s.ground_plane?std::hypot(s.ground_plane->gradient_x,s.ground_plane->gradient_z):0.;
    const double normal=std::cos(std::atan(tangent));
    const double grip=normal>=d.side_full_y?d.side_full:normal<=d.side_min_y?d.side_min:
        d.side_min+(normal-d.side_min_y)/(d.side_full_y-d.side_min_y)*(d.side_full-d.side_min);
    if(tangent>d.drive.t[18]*grip||s.airborne)return true;
    for(double v:{s.vertical_speed,s.push_yaw,s.slide_speed,s.air_lateral_x,s.air_lateral_z,s.pitch_speed,s.roll_speed})
        if(std::abs(v)>1.e-9)return true;
    return false;
}
void Contacts::acknowledge(ID player,ID sequence)
{
    if(player<=0||sequence<=0||sequence<=acknowledged_[player])return;
    acknowledged_[player]=sequence;
    for(auto i=cached_reports_.begin();i!=cached_reports_.end();) {
        if(i->first.first==player&&i->first.second<=sequence)i=cached_reports_.erase(i);else ++i;
    }
}
Report Contacts::terminal(ID bot,ID player,ID sequence)
{Report r;r.bot=bot;r.target=player;r.target_human=true;r.sequence=++ram_sequence_;r.player_receipt=sequence;return r;}
void Contacts::consume(std::map<ID,Actor>& actors,const std::vector<Human>& humans,World& world,double now)
{
    for(const auto& human:humans) {
        if(!human.checkpoints_valid||!valid_checkpoints(human.checkpoints))continue;
        for(const auto& row:human.checkpoints) {
            const auto found=actors.find(row.actor);if(found==actors.end())continue;
            auto& a=found->second;auto& s=*a.motion;
            const auto old=a.push_acks.find(human.network_id);
            if(old!=a.push_acks.end()&&row.sequence<=old->second.sequence)continue;
            const auto& before=old==a.push_acks.end()?std::array<double,5>{}:old->second.totals;
            const double moment=inertia(a),mass=std::max(a.descriptor->drive.mass,1.);
            if(moment)s.push_yaw+=(row.totals[4]-before[4])/moment;
            stock_native_bot_motion::ContactResponse response;
            response.delta_velocity={{(row.totals[0]-before[0])/mass,(row.totals[1]-before[1])/mass}};
            stock_native_bot_motion::ContactOptions options;options.advance_push=false;options.apply_correction=false;
            stock_native_bot_motion::contact(s,*a.descriptor,response,0.,now,world.motion_world(s.id),options);
            a.push_acks[human.network_id]=row;
        }
    }
}
void Contacts::translations(std::map<ID,Actor>& actors,const std::vector<Human>& humans,
    const std::map<ID,Point>& starts,ID sample_us,const std::vector<ID>& owner_order)
{
    auto traversal=owner_order;if(traversal.empty())for(const auto&entry:actors)traversal.push_back(entry.first);
    std::vector<offline_math::Body> bodies;std::map<ID,Body> projected;
    std::map<ID,const Human*> players;
    for(ID id:traversal) {
        Body body=project(actors.at(id),0.);const auto start=starts.find(id);
        if(start!=starts.end()){body.x=start->second[0];body.z=start->second[2];}
        projected[body.id]=body;bodies.push_back(geometry(body));
    }
    for(const auto& human:humans) {
        Body body=human.body;body.id=human_base_+human.network_id;
        projected[body.id]=body;players[body.id]=&human;bodies.push_back(geometry(body));
    }
    for(ID id:traversal) {
        auto& a=actors.at(id);auto& s=*a.motion;
        if(!s.alive){a.ram_motion.clear();continue;}
        const auto origin=starts.find(id);if(origin==starts.end())continue;
        const auto& start=origin->second;
        const offline_math::Vec move{s.position[0]-start[0],s.position[2]-start[2]};
        const auto body=geometry(projected.at(s.id));
        std::vector<std::pair<ID,double>> hits;
        const double fraction=offline_math::translation_contacts(body,move,bodies,hits);
        std::map<ID,RamMotion> retained;
        for(const auto& old:a.ram_motion) {
            const auto found=players.find(human_base_+old.first);if(found==players.end())continue;
            const auto& peer=projected.at(found->first);const Body current=project(a,0.);
            if(peer.has(offline_contact::Alive)&&peer.team!=a.team&&vertical_overlap(current,peer)&&overlap(current,peer))
                retained.insert(old);
        }
        for(const auto& hit:hits) {
            const auto p=players.find(hit.first);
            if(hit.second>fraction+1.e-9||p==players.end())continue;
            const auto& human=*p->second;const auto& peer=projected.at(hit.first);
            if(!peer.has(offline_contact::Alive)||peer.team==a.team)continue;
            const Point velocity{{std::sin(s.yaw)*s.speed+s.push_x,s.vertical_speed,std::cos(s.yaw)*s.speed+s.push_z}};
            const auto old=retained.find(human.network_id);
            if(norm2(velocity)<=(old==retained.end()?0.:norm2(old->second.velocity)))continue;
            RamMotion r;r.player=human.network_id;r.sequence=++a.ram_motion_sequence;r.sample_us=sample_us;r.velocity=velocity;
            r.position={{start[0]+move.x*hit.second,start[1]+(s.position[1]-start[1])*hit.second,start[2]+move.z*hit.second}};
            r.angles={{s.yaw,s.pitch,s.roll}};r.player_position={{peer.x,peer.y,peer.z}};
            r.player_angles={{peer.yaw,peer.pitch,peer.roll}};r.player_velocity={{peer.vx,peer.vy,peer.vz}};
            retained[r.player]=r;
        }
        a.ram_motion=std::move(retained);
        if(fraction<1.||s.push_x||s.push_z)s.drive_sweep=stock_native_bot_motion::DriveSweep{start,{{move.x,move.z}}};
        else s.drive_sweep.reset();
        const auto accepted=offline_math::slide(body,move,bodies,true,fraction);
        s.position[0]=start[0]+accepted.x;s.position[2]=start[2]+accepted.z;
    }
}
std::vector<Report> Contacts::historical(std::map<ID,Actor>& actors,const std::vector<Human>& humans)
{
    std::vector<Report> out;std::map<ID,std::vector<std::pair<const Human*,const Receipt*>>> ordered;
    for(const auto& h:humans) {
        acknowledge(h.network_id,h.acknowledged_ram_sequence);
        for(const auto& r:h.receipts)ordered[h.network_id].push_back({&h,&r});
    }
    for(auto& entry:ordered) {
        std::stable_sort(entry.second.begin(),entry.second.end(),[](const auto& a,const auto& b){return a.second->sequence<b.second->sequence;});
        for(const auto& value:entry.second) {
            const auto& human=*value.first;const auto& r=*value.second;const ID player=human.network_id;
            if(r.sequence<=acknowledged_[player])continue;
            const Pair key{player,r.sequence};const auto cached=cached_reports_.find(key);
            if(cached!=cached_reports_.end()){out.insert(out.end(),cached->second.begin(),cached->second.end());break;}
            const auto found=actors.find(r.bot);if(found==actors.end())break;
            if(found->second.motion->alive&&!r.has_history)break;
            Report result=terminal(r.bot,player,r.sequence);
            if(found->second.motion->alive&&r.history.id==r.bot&&r.valid) {
                Body other=human.body;other.x=r.player_position[0];other.y=r.player_position[1];other.z=r.player_position[2];
                other.yaw=r.player_angles[0];other.pitch=r.player_angles[1];other.roll=r.player_angles[2];
                const double length=std::hypot(r.player_normal[0],r.player_normal[1]);
                const double alignment=r.player_normal[0]*(other.x-r.history.x)+r.player_normal[1]*(other.z-r.history.z);
                const bool proved=r.armor_player>0.&&r.armor_bot>0.&&std::isfinite(r.armor_player)&&std::isfinite(r.armor_bot)&&
                    r.spall_player>=1.&&r.spall_player<=1.5&&r.bonus_player>=0.&&r.bonus_player<=.15&&
                    std::isfinite(length)&&length>=.999&&length<=1.001&&alignment>1.e-6&&!r.player_screened&&!r.bot_screened&&
                    contains(other,r.hit,.75)&&contains(r.history,r.hit,.75)&&r.history_profile.spall>=1.&&
                    r.history_profile.spall<=1.5&&r.history_profile.bonus>=0.&&r.history_profile.bonus<=.15;
                if(proved) {
                    const double nx=-r.player_normal[0]/length,nz=-r.player_normal[1]/length;
                    result.closing_speed=std::max(0.,-((r.bot_velocity[0]-r.player_velocity[0])*nx+
                        (r.bot_velocity[2]-r.player_velocity[2])*nz));
                    if(!((r.history.team==1||r.history.team==2)&&r.history.team==other.team)) {
                        Profile p;p.spall=r.spall_player;p.bonus=r.bonus_player;
                        const auto hp=damage(result.closing_speed,r.history.mass,other.mass,r.armor_bot,r.armor_player,
                            r.history_profile,p,norm2(r.bot_velocity)>0.,norm2(r.player_velocity)>0.);
                        result.damage_bot=hp[1];result.damage_target=hp[0];
                    }
                    if(result.damage_bot||result.damage_target){result.bot_tracks=r.bot_tracks;result.target_tracks=r.player_tracks;}
                }
            }
            cached_reports_[key]={result};out.push_back(result);break;
        }
    }
    return out;
}
std::vector<Report> Contacts::settle(std::map<ID,Actor>& actors,const std::vector<Human>& humans,
    const std::vector<ID>& dictionary_order,double dt,double now,World& world,const std::vector<ID>& owner_order)
{
    consume(actors,humans,world,now);
    std::map<ID,Body> by_id;std::map<ID,Profile> profiles;
    std::vector<ID> owners=owner_order;if(owners.empty())for(const auto&entry:actors)owners.push_back(entry.first);
    for(ID id:owners) {
        auto& a=actors.at(id);by_id[id]=project(a,dt);profiles[id]=a.profile;
        a.motion->contact_motor_turn.reset();
    }
    for(const auto& human:humans) {
        Body b=human.body;b.id=human_base_+human.network_id;b.flags|=offline_contact::Player;
        if(human.checkpoint_field_present){b.flags&=~offline_contact::Impulse;b.flags|=offline_contact::PositionFixed;}
        if(!b.has(offline_contact::Alive))b.flags|=offline_contact::PositionFixed|offline_contact::Immovable;
        by_id[b.id]=b;profiles[b.id]=human.profile;
    }
    std::vector<Body> bodies;
    std::set<ID> seen;
    for(ID id:dictionary_order) {
        const auto it=by_id.find(id);if(it==by_id.end()||!seen.insert(id).second)continue;
        bodies.push_back(it->second);
    }
    if(bodies.size()!=by_id.size())throw std::invalid_argument("Incomplete native contact dictionary order");
    const auto rows=offline_contact::resolve(bodies,owners,dt,std::vector<Pair>(contacts_.begin(),contacts_.end()));
    std::map<ID,const offline_contact::Row*> by_row;for(const auto& row:rows)by_row[row.id]=&row;
    auto reports=historical(actors,humans);
    std::set<Pair> current,contact_seen;
    std::map<Pair,std::optional<Armor>> armor_cache;
    double max_radius=4.;for(const auto& b:bodies)max_radius=std::max(max_radius,std::hypot(b.shape[0],b.shape[1]));
    const double bucket_size=max_radius*2.+4.;
    std::map<std::pair<int,int>,std::vector<ID>> buckets;
    for(const auto& b:bodies)buckets[{int(std::floor(b.x/bucket_size)),int(std::floor(b.z/bucket_size))}].push_back(b.id);
    for(ID id:owners) {
        auto& a=actors.at(id);auto& s=*a.motion;const Body& own=by_id.at(id);const auto& row=*by_row.at(id);
        if(!row.has_peers) {
            if(s.push_x||s.push_z||(!s.alive&&wreck_active(a))) {
                if(s.alive)stock_native_bot_motion::contact(s,*a.descriptor,{},dt,now,world.motion_world(s.id));
                else stock_native_bot_motion::wreck_contact(s,*a.descriptor,{},dt,now,world.motion_world(s.id));
            }
            s.drive_sweep.reset();continue;
        }
        if(!s.alive&&!wreck_active(a)&&!s.push_x&&!s.push_z&&!row.has_active_peer)continue;
        if(s.alive) {
            current.insert(row.retained_contacts.begin(),row.retained_contacts.end());
            std::set<Pair> new_pairs;
            for(const auto& candidate:row.ram_candidates) {
                const Body& other=by_id.at(candidate.other_id);const Pair key=pair(own.id,other.id);
                if(contacts_.count(key)||current.count(key)){current.insert(key);continue;}
                if(new_pairs.count(key))continue;
                const Profile& p=profiles.at(own.id);const Profile& q=profiles.at(other.id);
                auto aa=p.contact_armor,bb=q.contact_armor;
                std::optional<Armor> proof;
                if(!aa||!bb) {
                    auto found=armor_cache.find(key);
                    if(found==armor_cache.end()) {
                        proof=world.armor(own,other,candidate.impact);
                        if(proof&&own.id>other.id){std::swap(proof->value[0],proof->value[1]);std::swap(proof->tracks[0],proof->tracks[1]);}
                        found=armor_cache.emplace(key,proof).first;
                    }
                    proof=found->second;
                    if(proof&&own.id>other.id){std::swap(proof->value[0],proof->value[1]);std::swap(proof->tracks[0],proof->tracks[1]);}
                    if(proof){if(!aa)aa=proof->value[0];if(!bb)bb=proof->value[1];}
                }
                if(!aa||!bb||!std::isfinite(*aa)||!std::isfinite(*bb)||*aa<0.||*bb<0.||!profile_valid(p)||!profile_valid(q))continue;
                const auto hp=damage(candidate.closing_speed,std::max(own.mass,1.),std::max(other.mass,1.),*aa,*bb,p,q,
                    candidate.moving_self,candidate.moving_other);
                if(!hp[0]&&!hp[1])continue;
                new_pairs.insert(key);cooldowns_[key]=now;
                if(!other.has(offline_contact::Player)) {
                    Report r;r.bot=own.id;r.target=other.id;r.sequence=++ram_sequence_;r.damage_bot=hp[1];r.damage_target=hp[0];
                    r.closing_speed=candidate.closing_speed;r.positions={{own.x,own.z,other.x,other.z}};
                    if(proof){r.bot_tracks=proof->tracks[0];r.target_tracks=proof->tracks[1];}reports.push_back(std::move(r));
                }
            }
            current.insert(new_pairs.begin(),new_pairs.end());
        }
        std::vector<ID> nearby;const int cx=int(std::floor(own.x/bucket_size)),cz=int(std::floor(own.z/bucket_size));
        for(int z=-1;z<=1;++z)for(int x=-1;x<=1;++x) {
            const auto it=buckets.find({cx+x,cz+z});
            if(it!=buckets.end())nearby.insert(nearby.end(),it->second.begin(),it->second.end());
        }
        for(ID peer:nearby) {
            const auto& other=by_id.at(peer);
            if(other.id==own.id)continue;
            const Pair key=pair(own.id,other.id);if(contact_seen.count(key))continue;
            contact_seen.insert(key);
            const Body& first=by_id.at(key.first);const Body& second=by_id.at(key.second);
            if(!first.has(offline_contact::Alive)||!second.has(offline_contact::Alive)||
                (first.vx==second.vx&&first.vz==second.vz)||!vertical_overlap(first,second))continue;
            auto hit=overlap(first,second);if(!hit)continue;
            const auto owner=initiator(first,second,oriented(*hit,first,second));if(!owner)continue;
            const Body& attacker=*owner==0?first:second;const Body& target=*owner==0?second:first;
            PushProof proof;proof.sequence=++push_sequence_;proof.attacker_human=attacker.has(offline_contact::Player);
            proof.target_human=target.has(offline_contact::Player);
            proof.attacker=attacker.id-(proof.attacker_human?human_base_:0);proof.target=target.id-(proof.target_human?human_base_:0);
            push_proofs_[{proof.target_human,proof.target}]=proof;
        }
        stock_native_bot_motion::ContactResponse response{row.correction,row.delta_velocity,row.delta_yaw};
        if(!s.alive){stock_native_bot_motion::wreck_contact(s,*a.descriptor,response,dt,now,world.motion_world(s.id));continue;}
        bool contacted=s.rotation_contact_blocked;s.rotation_contact_blocked=false;
        for(double v:row.correction)contacted|=std::abs(v)>.0001;
        for(double v:row.delta_velocity)contacted|=std::abs(v)>.0001;
        if(contacted&&dt>0.)wait_elapsed_[s.id]+=dt;
        stock_native_bot_motion::contact(s,*a.descriptor,response,dt,now,world.motion_world(s.id));
    }
    contacts_=std::move(current);
    return reports;
}
std::map<ID,double> Contacts::take_waits()
{std::map<ID,double> out;out.swap(wait_elapsed_);return out;}
}
