#include "wot/stock_trajectory_drawer.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace wot {
namespace stock_trajectory {
namespace {
Vector3 sub(Vector3 a, Vector3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vector3 add(Vector3 a, Vector3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Vector3 mul(Vector3 a, float s) { return {a.x*s,a.y*s,a.z*s}; }
float dot(Vector3 a, Vector3 b) { return (a.y*b.y+a.x*b.x)+a.z*b.z; }
Vector3 cross(Vector3 a, Vector3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float horizontal(Vector3 a) { return std::sqrt(a.x*a.x+a.z*a.z); }
Vector3 normalise(Vector3 a) {
    const float length=std::sqrt((a.x*a.x+a.y*a.y)+a.z*a.z);
    return length==0.f?Vector3{0.f,0.f,0.f}:Vector3{a.x/length,a.y/length,a.z/length};
}
bool intersect(Vector3 normal, float distance, Vector3 a, Vector3 b, Vector3& hit) {
    const float da=dot(normal,a)-distance, db=dot(normal,b)-distance;
    if ((da<0.f && db<0.f) || (da>0.f && db>0.f)) return false;
    const float delta=da-db;
    if (delta==0.f) return false;
    hit=add(a,mul(sub(b,a),da/delta));
    return true;
}
bool inside(Vector3 p,Vector3 lo,Vector3 hi) {
    return lo.x<=p.x && p.x<=hi.x && lo.y<=p.y && p.y<=hi.y && lo.z<=p.z && p.z<=hi.z;
}
float blend(float a,float b,float t) { return a*(1.f-t)+b*t; }
}

std::uint32_t colour(const std::array<float,4>& rgba) {
    std::uint32_t channels[4];
    for (unsigned i=0;i<4;++i) {
        // cvttss2si returns INT_MIN for NaN/infinity/out-of-range, then the
        // original clamps that integer to [0,255] (006f6cc0).
        const float v=rgba[i];
        const int n=(!std::isfinite(v) || v>=2147483648.f || v< -2147483648.f)?
            std::numeric_limits<int>::min():static_cast<int>(v);
        channels[i]=static_cast<std::uint32_t>(std::max(0,std::min(255,n)));
    }
    return (channels[3]<<24)|(channels[0]<<16)|(channels[1]<<8)|channels[2];
}

bool leave_bounds(Vector3 minimum,Vector3 maximum,Vector3 start,Vector3 end,Vector3& hit) {
    // The exact owner extends y using the horizontal extrema (006f678b).
    minimum.y=std::min(minimum.x,minimum.z);
    maximum.y=std::max(maximum.x,maximum.z);
    if (!inside(start,minimum,maximum) || inside(end,minimum,maximum)) return false;
    const std::array<Vector3,6> normals{{{0,1,0},{0,-1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}}};
    const std::array<float,6> distances{{minimum.y,-maximum.y,minimum.z,-maximum.z,minimum.x,-maximum.x}};
    float best=0.f;
    for (unsigned i=0;i<normals.size();++i) {
        Vector3 candidate;
        if (intersect(normals[i],distances[i],start,end,candidate)) {
            const Vector3 delta=sub(candidate,start);
            const float distance=std::sqrt((delta.y*delta.y+delta.x*delta.x)+delta.z*delta.z);
            if (best==0.f || distance<best) {best=distance;hit=candidate;}
        }
    }
    return true;
}

Fractions calculate(Vector3 target,Vector3 start,Vector3 velocity,Vector3 gravity,
    float maxDistance,const SegmentQuery& scene,const BoundaryQuery& bounds) {
    const Vector3 forward=normalise(sub(target,start));
    const Vector3 right=normalise(cross({0.f,1.f,0.f},forward));
    const Vector3 normal=cross(forward,right);
    const float planeDistance=dot(normal,start);
    Vector3 previous=start,first=start,second=start,hit=start;
    Vector3 point=start,pointVelocity=velocity;
    bool collided=false,planeHit=false,finished=false;
    float time=0.f;
    do {
        const auto points=gun_projectile_trajectory(point,pointVelocity,gravity,0.1f,0.03f);
        for (const Vector3& next:points) {
            if (!collided && scene && scene(previous,next,hit)) {
                first=hit;second=hit;previous=hit;collided=true;
            }
            if (collided && intersect(normal,planeDistance,previous,next,hit)) {
                second=hit;planeHit=true;
            }
            if (bounds && bounds(previous,next,hit)) {finished=true;break;}
            if (horizontal(sub(start,next))>=maxDistance) {
                hit=add(start,mul(normalise(sub(next,start)),maxDistance));
                finished=true;break;
            }
            if (planeHit) {finished=true;break;}
            previous=next;
        }
        if (finished) break;
        time=time+0.1f;
        const float half=time*time*0.5f;
        point={gravity.x*half+(start.x+velocity.x*time),gravity.y*half+(start.y+velocity.y*time),
            gravity.z*half+(start.z+velocity.z*time)};
        pointVelocity={velocity.x+gravity.x*time,velocity.y+gravity.y*time,velocity.z+gravity.z*time};
    } while (time<10.f);
    const float targetDistance=horizontal(sub(target,start));
    if (horizontal(sub(first,start))>=targetDistance) first=target;
    if (horizontal(sub(second,start))>=targetDistance) second=target;
    if (-0.0004f<targetDistance && targetDistance<0.0004f) return {1.f,0.f,0.f,0.f};
    const float inverse=1.f/targetDistance;
    return {horizontal(sub(first,start))*inverse,horizontal(sub(second,first))*inverse,first.y,second.y};
}

bool simulate(Vector3 start,Vector3 velocity,Vector3 gravity,float step,float epsilon,
    const SegmentQuery& scene,SimulatedHit& hit) {
    if (!std::isfinite(step) || step<=0.f)
        throw std::invalid_argument("trajectory step must be finite and positive");
    Vector3 position=start,currentVelocity=velocity,point{};
    float elapsed=0.f;
    bool found=false;
    while (elapsed<5.f) {
        const auto points=gun_projectile_trajectory(position,currentVelocity,gravity,step,epsilon);
        Vector3 previous=position;
        for (const auto& next:points) {
            if (scene && scene(previous,next,point)) {found=true;break;}
            previous=next;
        }
        if (found) break;
        position=points.back();
        currentVelocity={currentVelocity.x+step*gravity.x,currentVelocity.y+gravity.y*step,
            currentVelocity.z+gravity.z*step};
        elapsed=elapsed+step;
    }
    if (!found) return false;
    float time=0.f;
    const auto nearZero=[](float value) {return -0.0004f<value && value<0.0004f;};
    if (!nearZero(velocity.x)) time=(point.x-start.x)/velocity.x;
    else if (!nearZero(velocity.z)) time=(point.z-start.z)/velocity.z;
    else {
        const float discriminant=velocity.y*velocity.y-(gravity.y+gravity.y)*(start.y-point.y);
        if (!(discriminant>0.f) || nearZero(gravity.y)) return false;
        const float root=std::sqrt(discriminant),inverse=1.f/gravity.y;
        const float first=(-velocity.y-root)*inverse,second=(root-velocity.y)*inverse;
        if (first>0.f) time=first;
        else if (second>0.f) time=second;
    }
    hit={time,point,{velocity.x+gravity.x*time,velocity.y+gravity.y*time,velocity.z+gravity.z*time}};
    return true;
}

State::State() {
    set_colours({{255.f,0.f,255.f,0.f}},{{0.f,0.f,255.f,255.f}},{{255.f,255.f,255.f,255.f}});
}
void State::set_colours(const std::array<float,4>& good,const std::array<float,4>& blocked,
    const std::array<float,4>& missing) {
    const std::uint32_t colours[]={colour(good),colour(blocked),colour(missing)};
    for(unsigned i=0;i<6;++i) vertices[i].colour=colours[i/2];
}
void State::update(Vector3 target,Vector3 start,Vector3 velocity,float interval,
    const SegmentQuery& scene,const BoundaryQuery& bounds) {
    // The scene may re-enter Python. Do not publish partially updated
    // interpolation state before the complete calculation has succeeded.
    const Fractions next=calculate(target,start,velocity,gravity,maxDistance,scene,bounds);
    elapsed=0.f;
    rate=(-0.0004f<interval && interval<0.0004f)?1.f:1.f/std::fabs(interval);
    previous=current;previousStart=currentStart;currentStart=start;current=next;
}
void State::advance(float dt,const SegmentQuery& cameraRay,Vector3 rayStart,Vector3 rayEnd) {
    if (!visible || !configured) return;
    elapsed=elapsed+dt;
    const float t=std::min(rate*elapsed,1.f);
    const float first=blend(previous.first,current.first,t), second=blend(previous.second,current.second,t);
    const float firstHeight=blend(previous.firstHeight,current.firstHeight,t);
    const float secondHeight=blend(previous.secondHeight,current.secondHeight,t);
    const Vector3 start{blend(previousStart.x,currentStart.x,t),blend(previousStart.y,currentStart.y,t),
        blend(previousStart.z,currentStart.z,t)};
    Vector3 hit;
    if (!cameraRay || !cameraRay(rayStart,rayEnd,hit)) return;
    const Vector3 delta=sub(hit,start);
    vertices[0].position=start;
    vertices[1].position=add(start,mul(delta,first));
    if (strategic) vertices[1].position.y=firstHeight;
    vertices[2].position=vertices[1].position;
    vertices[3].position=add(vertices[2].position,mul(delta,second));
    if (strategic) vertices[3].position.y=secondHeight;
    vertices[4].position=vertices[3].position;
    vertices[5].position=hit;
}

} // namespace stock_trajectory
} // namespace wot
