#include "wot/stock_trajectory_drawer.hpp"
#include <cmath>
namespace st=wot::stock_trajectory;
static wot::Vector3 vec(const float* a) {return {a[0],a[1],a[2]};}
extern "C" {
unsigned trajectory_colour(const float* rgba) {return st::colour({{rgba[0],rgba[1],rgba[2],rgba[3]}});}
int trajectory_bounds(const float* bounds,const float* a,const float* b,float* out) {
    wot::Vector3 hit{0,0,0};const bool result=st::leave_bounds(vec(bounds),vec(bounds+3),vec(a),vec(b),hit);
    out[0]=hit.x;out[1]=hit.y;out[2]=hit.z;return result;
}
void trajectory_calculate(const float* values,float* out) {
    const float ground=values[13];
    auto scene=[ground](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
        if ((a.y>ground && b.y<=ground) || (a.y<ground && b.y>=ground)) {
            const float t=(a.y-ground)/(a.y-b.y);
            hit={a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t};return true;
        }return false;
    };
    auto bounds=[values](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
        return st::leave_bounds(vec(values+14),vec(values+17),a,b,hit);
    };
    const st::Fractions r=st::calculate(vec(values),vec(values+3),vec(values+6),vec(values+9),values[12],scene,bounds);
    out[0]=r.first;out[1]=r.second;out[2]=r.firstHeight;out[3]=r.secondHeight;
}
int trajectory_simulate(const float* values,float* out) {
    const float ground=values[11];
    auto scene=[ground](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
        if ((a.y>ground&&b.y<=ground)||(a.y<ground&&b.y>=ground)) {
            wot::Vector3 delta{b.x-a.x,b.y-a.y,b.z-a.z};
            const float length=std::sqrt((delta.x*delta.x+delta.y*delta.y)+delta.z*delta.z);
            const float distance=length*((a.y-ground)/(a.y-b.y));
            hit={a.x+(delta.x/length)*distance,a.y+(delta.y/length)*distance,a.z+(delta.z/length)*distance};
            return true;
        }return false;
    };
    st::SimulatedHit hit;
    if (!st::simulate(vec(values),vec(values+3),vec(values+6),values[9],values[10],scene,hit)) return 0;
    out[0]=hit.time;out[1]=hit.point.x;out[2]=hit.point.y;out[3]=hit.point.z;
    out[4]=hit.velocity.x;out[5]=hit.velocity.y;out[6]=hit.velocity.z;return 1;
}
void trajectory_advance(const float* values,float* out) {
    st::State s;s.configured=values[0]!=0;s.visible=values[1]!=0;s.strategic=values[2]!=0;
    s.elapsed=values[3];s.rate=values[4];
    s.previous={values[5],values[6],values[7],values[8]};
    s.current={values[9],values[10],values[11],values[12]};
    s.previousStart=vec(values+13);s.currentStart=vec(values+16);
    auto scene=[values](wot::Vector3,wot::Vector3,wot::Vector3& hit) {hit=vec(values+19);return values[23]!=0;};
    s.advance(values[22],scene,{0,0,0},{0,0,0});
    out[0]=s.elapsed;
    for(unsigned i=0;i<6;++i) {out[1+3*i]=s.vertices[i].position.x;out[2+3*i]=s.vertices[i].position.y;out[3+3*i]=s.vertices[i].position.z;}
}
}
