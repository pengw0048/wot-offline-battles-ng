#pragma once
#include "stock_native_client_drive_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace stock_native_client_drive {
inline std::array<double,3>wreckContactStep(const PlayerDescriptor&d,const std::array<double,4>&shape,double x,double z,double omega,double yaw,double dt,double normal){if(dt<=0.)return {x,z,omega};const double sn=std::sin(yaw),cs=std::cos(yaw),inertia=(shape[0]*shape[0]+shape[1]*shape[1])/3.;std::array<double,3>velocity{x*sn+z*cs,x*cs-z*sn,omega};normal=std::clamp(normal,0.,1.);const auto&law=d.motion.drive;
    const double rr=law.rolling(0,0.)*law.mass/std::max(law.mass,1.),hold=law.t[18]*law.t[0]*std::max(normal,.1);
    const double side=normal>=d.motion.side_full_y?d.motion.side_full:normal<=d.motion.side_min_y?d.motion.side_min:d.motion.side_min+(normal-d.motion.side_min_y)/(d.motion.side_full_y-d.motion.side_min_y)*(d.motion.side_full-d.motion.side_min);
    const Vec decel{std::max(rr,hold),std::max(rr,hold*side)};
    struct Row{unsigned axis;double lever,budget,impulse=0.;};std::vector<Row>constraints;for(double width:{-shape[0],shape[0]})for(double length:{-shape[1]/2.,shape[1]/2.}){constraints.push_back({0,width,decel[0]*dt/4.});constraints.push_back({1,-length,decel[1]*dt/4.});}
    for(int iteration=0;iteration<d.motion.constraint_iterations;++iteration){double largest=0.;for(auto&row:constraints){double delta=-(velocity[row.axis]+row.lever*velocity[2])/(1.+row.lever*row.lever/inertia);const double updated=std::max(-row.budget,std::min(row.budget,row.impulse+delta));delta=updated-row.impulse;row.impulse=updated;velocity[row.axis]+=delta;velocity[2]+=delta*row.lever/inertia;largest=std::max(largest,std::abs(delta));}if(largest<1.e-9)break;}
    for(auto&v:velocity)if(std::abs(v)<1.e-9)v=0.;return {velocity[0]*sn+velocity[1]*cs,velocity[0]*cs-velocity[1]*sn,velocity[2]};
}
}
