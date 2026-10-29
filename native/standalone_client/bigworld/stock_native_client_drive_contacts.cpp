#include "stock_native_client_drive_contacts.hpp"
#include "stock_native_client_drive_roster.hpp"
#include <algorithm>
#include <cmath>
namespace stock_native_client_drive {
namespace {
offline_math::Body geometry(const offline_contact::Body&b){return {b.id,b.has(offline_contact::HasY),b.x,b.y,b.z,b.yaw,b.pitch,b.roll,b.shape};}
Vec deceleration(const PlayerDescriptor&d,const PlayerState&s,bool rolling){const double normal=std::cos(s.pitch)*std::cos(s.roll),ny=std::max(normal,.1);const auto&law=d.motion.drive;
    const double rr=law.rolling(0,0.)*law.mass/std::max(law.mass,1.),hold=law.t[18]*law.t[0]*ny;
    const double grip=normal>=d.motion.side_full_y?d.motion.side_full:normal<=d.motion.side_min_y?d.motion.side_min:d.motion.side_min+(normal-d.motion.side_min_y)/(d.motion.side_full_y-d.motion.side_min_y)*(d.motion.side_full-d.motion.side_min);
    return {rolling?rr:std::max(rr,hold),std::max(rr,hold*grip)};
}
}
Point PlayerContacts::settle(PlayerState&s,const PlayerDescriptor&d,const Controls&controls,ContactWorld&world,
    const Point&position,double yaw,double dt,double now,std::optional<Point>start){
    using namespace offline_contact;
    const bool reaction=s.contact_reaction_paid;s.contact_reaction_paid=false;if(reaction)start.reset();world.retry_ram_proofs();
    Body own;own.id=-1;own.team=d.team;own.flags=Alive|Impulse|HasY|HasGrip;own.mass=d.motion.drive.mass;own.x=position[0];own.y=position[1];own.z=position[2];own.yaw=yaw;own.pitch=s.pitch;own.roll=s.roll;own.shape=d.motion.collision_shape;
    const double sn=std::sin(yaw),cs=std::cos(yaw);own.vx=sn*s.speed+s.push_x;own.vy=s.vertical_speed;own.vz=cs*s.speed+s.push_z;
    const auto grip=deceleration(d,s,s.speed!=0.||controls.forward!=0.);own.grip[0]=grip[0];own.grip[1]=grip[1];
    const Vec travel=start?Vec{position[0]-(*start)[0],position[2]-(*start)[2]}:Vec{};
    auto peers=world.peers(s,position,dt,std::hypot(travel[0],travel[1]));std::vector<offline_math::Body>geometries;for(const auto&p:peers)geometries.push_back(geometry(p.body));
    double drive_fraction=1.;Point accepted=position;
    if(start){auto initial=geometry(own);initial.x=(*start)[0];initial.z=(*start)[2];drive_fraction=offline_math::translation(initial,{travel[0],travel[1]},geometries);
        accepted[0]=(*start)[0]+travel[0]*drive_fraction;accepted[2]=(*start)[2]+travel[1]*drive_fraction;own.x=accepted[0];own.z=accepted[2];}
    if(s.drive_turn!=0.&&dt>0.){const auto&law=d.motion.drive;own.traverse_speed=law.traverse(0.,s.drive_turn,s.speed,std::max(dt,law.t[24]),0,s.drive_throttle);
        own.traverse_torque=std::abs(law.engine(std::abs(s.speed)+std::abs(own.traverse_speed)*own.shape[0],s.drive_turn,s.pitch))*(1.-.75*std::min(1.,std::abs(s.drive_throttle)))*own.shape[0];}
    if(reaction)own.traverse_torque=0.;world.poll_ram(own,peers);
    std::vector<Body>bodies{own};std::vector<ID>owners{own.id};for(const auto&p:peers){auto b=p.body;b.flags|=PositionFixed;if(p.physical_velocity){b.vx=(*p.physical_velocity)[0];b.vz=(*p.physical_velocity)[1];}bodies.push_back(b);owners.push_back(b.id);}
    const auto rows=stock_native_client_contact_roster::resolve(bodies,owners,dt,{physical_contacts_.begin(),physical_contacts_.end()},own.id);
    const auto&response=rows.front();physical_contacts_={response.retained_contacts.begin(),response.retained_contacts.end()};
    for(std::size_t i=1;i<rows.size();++i){const auto&r=rows[i];if(r.delta_velocity==Vec{}&&r.correction==Vec{}&&r.delta_yaw==0.)continue;auto physical=peers[i-1];physical.body=bodies[i];world.collision_feedback(own,physical,now);
        const auto&p=peers[i-1];if(!p.bot)continue;const double inertia=(p.body.has(Alive)||p.body.has(Immovable))?0.:p.body.mass*(p.body.shape[0]*p.body.shape[0]+p.body.shape[1]*p.body.shape[1])/3.;
        const double x=r.delta_velocity[0]*p.body.mass,z=r.delta_velocity[1]*p.body.mass,angular=r.delta_yaw*inertia;
        if(x!=0.||z!=0.||angular!=0.){auto&ledger=pushes_[p.network_id];ledger.actor=p.network_id;++ledger.sequence;ledger.totals[0]+=x;ledger.totals[1]+=z;ledger.totals[4]+=angular;
            if(const auto stamp=world.motion_time(now))impulses_[p.network_id].push_back({ledger.sequence,*stamp,x,z,angular});}}
    const double dx=response.delta_velocity[0],dz=response.delta_velocity[1],forward=dx*sn+dz*cs;s.speed+=forward;
    auto pushed=s;pushed.yaw=yaw;const auto push=push_step(d,pushed,{s.push_x+dx-forward*sn,s.push_z+dz-forward*cs},dt,s.speed!=0.||controls.forward!=0.);
    Vec move{response.correction[0]+push[0]*dt,response.correction[1]+push[1]*dt};const auto guarded=accepted;
    if(start&&(drive_fraction<1.||dx!=0.||dz!=0.||push[0]!=0.||push[1]!=0.)){accepted[0]=(*start)[0];accepted[2]=(*start)[2];own.x=accepted[0];own.z=accepted[2];move[0]+=travel[0]+sn*forward*dt;move[1]+=travel[1]+cs*forward*dt;}
    const auto slide=offline_math::slide(geometry(own),{move[0],move[1]},geometries,false,1.);move={slide.x,slide.z};const double distance=std::hypot(move[0],move[1]);
    Vec retained=push;if(distance>.0001){const double travel_yaw=std::atan2(move[0],move[1]),speed=distance/std::max(dt,1./120.);auto candidate=accepted;candidate[0]+=move[0];candidate[2]+=move[1];const bool recovery=world.arena_outside(s,accepted,yaw);
        if(!world.motion(s,accepted,travel_yaw,speed,dt,yaw)||(!world.baked_safe(candidate)&&!recovery)){retained={};accepted=guarded;}else accepted=candidate;}
    s.push_x=retained[0];s.push_z=retained[1];return world.turret_contacts(s,accepted,yaw,dt);
}
}
