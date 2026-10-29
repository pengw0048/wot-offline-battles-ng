#include "stock_native_detached_turret_runtime.hpp"
#include <cassert>
#include <cmath>
#include <iomanip>
#include <iostream>
using namespace stock_native_detached_turret;
Point readPoint(){Point p;for(auto&v:p)std::cin>>v;return p;}
void write(double v){std::cout<<v;}
void write(bool v){std::cout<<(v?"true":"false");}
template<class T,std::size_t N>void write(const std::array<T,N>&);
void write(const Box&);
template<class T>void write(const std::vector<T>&v){std::cout<<'[';bool first=true;for(const auto&x:v){if(!first)std::cout<<',';first=false;write(x);}std::cout<<']';}
template<class T,std::size_t N>void write(const std::array<T,N>&v){std::cout<<'[';for(unsigned i=0;i<N;++i){if(i)std::cout<<',';write(v[i]);}std::cout<<']';}
void write(const Box&b){std::cout<<'[';write(b.center);std::cout<<',';write(b.axes);std::cout<<']';}
struct Scene:World {
    double floor=-100.,slope=0.,wall=100.,tick=0.,time=0.;unsigned queries=0,applications=0,begins=0,rollbacks=0;bool fail=false;
    std::optional<Surface> collide(const Point&a,const Point&b)override{++queries;if(fail)throw std::runtime_error("fixture scenery failure");double t=2.;std::optional<Surface> out;const auto hit=[&](double x,double y,Point normal){if(x>=0.&&y<=0.&&x!=y){const auto ratio=x/(x-y);if(ratio<t){t=ratio;Point p{};for(unsigned i=0;i<3;++i)p[i]=a[i]+(b[i]-a[i])*ratio;const auto norm=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);for(auto&v:normal)v/=norm;out=Surface{p,normal};}}};hit(a[1]-floor-slope*a[0],b[1]-floor-slope*b[0],{{-slope,1.,0.}});hit(wall-a[0],wall-b[0],{{-1.,0.,0.}});return out;}
    void applyVehicle(Vehicle&v,const Contact&hit,double)override{++applications;for(auto&box:v.boxes)for(unsigned i=0;i<3;++i)box.center[i]+=hit.vehicle_correction[i];for(unsigned i=0;i<3;++i)v.velocity[i]+=hit.delta[i];}
    double clock()override{const auto value=time;time+=tick;return value;}
    void begin(Owner)override{++begins;}
    void rollback(Owner)override{++rollbacks;}
};
Component component(){Component c;std::cin>>c.mass;c.geometry.bounds.minimum=readPoint();c.geometry.bounds.maximum=readPoint();c.geometry.offset=readPoint();return c;}
int main(int argc,char**){std::cout<<std::setprecision(17);if(argc>1){
    Component c;c.mass=800.;c.geometry.bounds.minimum={{-1.,-.5,-1.}};c.geometry.bounds.maximum={{1.,.5,1.}};Frame initial;initial.position={{0.,8.,0.}};Row row;row.owner={false,1};row.flight.body=initial;Runtime runtime;assert(runtime.accept(row,{c}));Scene scene;scene.tick=.003;std::vector<Vehicle> vehicles;auto updates=runtime.update(100.,vehicles,{},scene);assert(updates.size()==1);assert(std::abs(runtime.simulationTime(row.owner)-20.)<1.e-9);scene.fail=true;const auto before=runtime.body(row.owner)->frame();const auto retry=runtime.update(100.,vehicles,{},scene);assert(retry.empty()&&runtime.failures.size()==1&&scene.rollbacks==1);assert(runtime.body(row.owner)->frame().position==before.position&&std::abs(runtime.simulationTime(row.owner)-20.)<1.e-9);scene.fail=false;scene.tick=0.;Checkpoint checkpoint;checkpoint.player={true,4};checkpoint.sequence=1;checkpoint.linear={{800.,0.,0.}};runtime.update(140.,vehicles,{{row.owner,checkpoint}},scene);assert(runtime.simulationTime(row.owner)==140.);const auto v=runtime.body(row.owner)->velocity[0];runtime.update(180.,vehicles,{{row.owner,checkpoint}},scene);assert(runtime.body(row.owner)->velocity[0]==v);checkpoint.sequence=2;checkpoint.linear={{1600.,0.,0.}};runtime.update(220.,vehicles,{{row.owner,checkpoint}},scene);assert(runtime.body(row.owner)->velocity[0]==v+1.);std::cout<<"ok\n";return 0;}
    unsigned count;while(std::cin>>count){std::vector<Component> components;while(count--)components.push_back(component());Frame frame;frame.position=readPoint();frame.attitude=readPoint();frame.velocity=readPoint();frame.angular_velocity=readPoint();std::cin>>frame.grounded>>frame.sleeping;double dt;Scene scene;std::cin>>dt>>scene.floor>>scene.slope>>scene.wall>>scene.tick;Body body(components,frame);unsigned n;std::cin>>n;std::vector<Vehicle> vehicles;while(n--){Vehicle v;std::cin>>v.mass>>v.human>>v.alive;v.velocity=readPoint();Box b;b.center=readPoint();for(unsigned i=0;i<3;++i)b.axes.push_back(readPoint());v.boxes.push_back(b);vehicles.push_back(v);}const auto consumed=advance(body,dt,vehicles,scene,scene.tick>0.?std::optional<double>(.004):std::nullopt);const auto f=body.frame();std::cout<<'[';write(body.properties.mass);std::cout<<',';write(body.properties.centre);std::cout<<',';write(body.properties.inertia);std::cout<<',';write(body.properties.inverse_inertia);std::cout<<',';write(f.position);std::cout<<',';write(f.attitude);std::cout<<',';write(f.velocity);std::cout<<',';write(f.angular_velocity);std::cout<<',';write(f.grounded);std::cout<<',';write(f.sleeping);std::cout<<',';write(body.kineticEnergy());std::cout<<',';write(body.points());std::cout<<',';write(body.boxes());std::cout<<','<<scene.queries<<','<<consumed<<','<<scene.applications<<','<<f.impact_serial<<',';if(f.impact){std::cout<<'[';write(f.impact->point);std::cout<<',';write(f.impact->normal);std::cout<<',';write(f.impact->velocity);std::cout<<',';write(f.impact->energy);std::cout<<']';}else std::cout<<"null";std::cout<<",[";for(unsigned i=0;i<vehicles.size();++i){if(i)std::cout<<',';std::cout<<'[';write(vehicles[i].boxes);std::cout<<',';write(vehicles[i].velocity);std::cout<<']';}std::cout<<"]]\n";}
    return 0;
}
