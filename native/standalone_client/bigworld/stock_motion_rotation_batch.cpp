#include "stock_motion_rotation_batch.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_motion_rotation {
namespace {
constexpr double PI = 3.14159265358979323846;
void validateBounds(const Bounds & body)
{
    for (unsigned i=0;i<3;++i)
        if (!std::isfinite(body.minimum[i]) || !std::isfinite(body.maximum[i]) || body.minimum[i]>body.maximum[i])
            throw std::invalid_argument("Rotation body bounds are invalid");
}
void validate(const Snapshot & input)
{
    validateBounds(input.body);
    for (double value: input.position)
        if (!std::isfinite(value)) throw std::invalid_argument("Rotation position is invalid");
    for (double value: input.translation)
        if (!std::isfinite(value)) throw std::invalid_argument("Rotation translation is invalid");
    const double values[]={input.start_yaw,input.end_yaw,input.pitch,input.roll,input.pivot_offset,
        input.max_angle_step,input.drivable_gradient};
    for (double value: values)
        if (!std::isfinite(value)) throw std::invalid_argument("Rotation scalar is invalid");
    if (!(input.max_angle_step>0.) || !(input.drivable_gradient>0.) ||
        input.max_steps<1 || input.max_steps>1024)
        throw std::invalid_argument("Rotation tuning is outside the native contract");
}
void validateOutcome(const WorldResult & outcome, Result & result)
{
    if (outcome.status<0) ++result.query_failures;
    if (outcome.status>2) throw std::invalid_argument("Rotation world status is invalid");
}
std::array<double, 2> extrema(double a, double b, double first, double last)
{
    double low = std::min(a*std::cos(first)+b*std::sin(first),
        a*std::cos(last)+b*std::sin(last));
    double high = std::max(a*std::cos(first)+b*std::sin(first),
        a*std::cos(last)+b*std::sin(last));
    const double stationary = std::atan2(b, a);
    const int start = static_cast<int>(std::ceil((first-stationary)/PI));
    const int end = static_cast<int>(std::floor((last-stationary)/PI));
    for (int i=start; i<=end; ++i) {
        const double angle=stationary+i*PI, value=a*std::cos(angle)+b*std::sin(angle);
        low=std::min(low,value); high=std::max(high,value);
    }
    return {{low,high}};
}
double intervalMinimum(double a, double b, double first, double last, double drift)
{
    const double delta=last-first, radius=std::hypot(a,b);
    double value=std::min(a*std::cos(first)+b*std::sin(first),
        a*std::cos(last)+b*std::sin(last)+drift);
    if (std::abs(delta)*radius<=1.e-15) return value;
    const double rhs=-drift/(delta*radius);
    if (rhs < -1. || rhs > 1.) return value;
    const double phase=std::atan2(-a,b), alpha=std::acos(rhs);
    const double low=std::min(first,last), high=std::max(first,last);
    for (double root: {phase+alpha,phase-alpha}) {
        const double begin=std::ceil((low-root)/(2.*PI));
        const double end=std::floor((high-root)/(2.*PI));
        // A shortest yaw interval spans at most one revolution. Evaluate
        // its endpoint cycles without converting a large finite yaw to int
        // or relying on a floating cycle increment making progress.
        if (begin<=end) for (double cycle: {begin,end}) {
            const double angle=root+cycle*2.*PI, t=(angle-first)/delta;
            if (t>=0. && t<=1.) value=std::min(value,a*std::cos(angle)+b*std::sin(angle)+drift*t);
        }
    }
    return value;
}
Point poseY(double pitch, double roll)
{
    if (pitch==0. && roll==0.) return {{0.,1.,0.}};
    const double cosine=std::cos(pitch);
    return {{cosine*std::sin(roll),cosine*std::cos(roll),-std::sin(pitch)}};
}
Point pivotPosition(const Point & position, double yaw, double end, double offset)
{
    return {{position[0]+offset*(std::cos(yaw)-std::cos(end)), position[1],
        position[2]+offset*(std::sin(end)-std::sin(yaw))}};
}
void putPosition(double * output, const Point & input)
{
    for (unsigned i=0;i<3;++i) output[i]=static_cast<float>(input[i]);
}
WorldSnapshot worldSnapshot(const Snapshot & input, const Bounds & body,
    const Point & position, double yaw, double speed, bool translating,
    double motion_yaw, bool supplied)
{
    WorldSnapshot result{};
    putPosition(result.position,position);
    result.yaw=yaw; result.speed=speed; result.dt=translating?1.:0.;
    result.pitch=input.pitch; result.roll=input.roll; result.motion_yaw=motion_yaw;
    result.bounds[0]=body.minimum[0]; result.bounds[1]=body.maximum[0];
    result.bounds[2]=-body.minimum[2]; result.bounds[3]=body.maximum[2];
    result.heights[0]=body.minimum[1]; result.heights[1]=body.maximum[1];
    result.passive=translating?1u:0u; result.exact_footprint=translating?0u:1u;
    result.supplied_departing=supplied?1u:0u;
    return result;
}
class PreviousContacts {
    const Snapshot & input_;
    WorldEngine & engine_;
    Result & result_;
    bool checked_ = false;
    std::vector<Contact> contacts_;
public:
    PreviousContacts(const Snapshot & input, WorldEngine & engine, Result & result):
        input_(input),engine_(engine),result_(result) {}
    const std::vector<Contact> & read()
    {
        if (!checked_) {
            // Mark before the synchronous native call, preserving lazy
            // ownership even if that call re-enters the world adapter.
            checked_=true;
            for (double direction: {1.e-6,-1.e-6}) {
                const auto snapshot=worldSnapshot(input_,input_.body,input_.position,
                    input_.start_yaw,direction,false,0.,false);
                ++result_.world_calls; ++result_.previous_world_calls;
                const auto outcome=engine_.run(snapshot,nullptr);
                validateOutcome(outcome,result_);
                // The fixed wrapper contains a failed world as hard but
                // retains any trace already written, then probes the other
                // direction. A genuine retained plane may still be useful.
                if (outcome.trace.present) {
                    Contact contact;
                    for (unsigned i=0;i<3;++i) {
                        contact.point[i]=static_cast<float>(outcome.trace.response.point[i]);
                        contact.normal[i]=static_cast<float>(outcome.trace.response.normal[i]);
                    }
                    contacts_.push_back(contact);
                }
            }
        }
        return contacts_;
    }
};
class RotationDeparture final: public DeparturePolicy {
    Snapshot input_;
    PreviousContacts & previous_;
    Result & result_;
public:
    RotationDeparture(const Snapshot & input, PreviousContacts & previous, Result & result):
        input_(input),previous_(previous),result_(result) {}
    bool accepts(const Contact & contact) override
    {
        ++result_.departure_tests;
        const auto proof=departureProof(input_,contact);
        if (!proof.improves || proof.inside) return proof.improves;
        for (const auto & old: previous_.read()) {
            double alignment=0., difference=0.;
            for (unsigned i=0;i<3;++i) {
                alignment+=contact.normal[i]*old.normal[i];
                difference+=(contact.point[i]-old.point[i])*contact.normal[i];
            }
            if (alignment>=.9999 && std::abs(difference)<=.001) return true;
        }
        return false;
    }
};
}

WorldResult WorldStageEngine::run(const WorldSnapshot & snapshot, DeparturePolicy * policy)
{
    auto frame=frames_.begin(snapshot,policy);
    if (!frame) throw std::runtime_error("Rotation world frame is unavailable");
    WorldResult result;
    result.status=wot_world_run(&snapshot,[](void * raw,std::uint32_t op,
        const WorldRequest * requests,std::uint32_t count,WorldResponse * responses) {
        return static_cast<WorldFrame *>(raw)->dispatch(op,requests,count,responses);
    },frame.get());
    result.trace=frame->trace();
    return result;
}

void validateSnapshot(const Snapshot &input) { validate(input); }

double angleDelta(double current, double target)
{
    double wrapped=std::fmod(target-current+PI,2.*PI);
    if (wrapped<0.) wrapped+=2.*PI;
    return wrapped-PI;
}
Bounds intervalEnvelope(const Bounds & body, double half_angle, double pivot)
{
    validateBounds(body);
    if(!std::isfinite(half_angle) || !std::isfinite(pivot) || std::abs(half_angle)>PI)
        throw std::invalid_argument("Rotation envelope input is invalid");
    const double angle=std::abs(half_angle), infinity=std::numeric_limits<double>::infinity();
    Bounds result{{{infinity,body.minimum[1],infinity}},{{-infinity,body.maximum[1],-infinity}}};
    for (double x: {body.minimum[0],body.maximum[0]})
        for (double z: {body.minimum[2],body.maximum[2]}) {
            const auto rx=extrema(x-pivot,z,-angle,angle);
            const auto rz=extrema(z,pivot-x,-angle,angle);
            result.minimum[0]=std::min(result.minimum[0],rx[0]+pivot);
            result.maximum[0]=std::max(result.maximum[0],rx[1]+pivot);
            result.minimum[2]=std::min(result.minimum[2],rz[0]);
            result.maximum[2]=std::max(result.maximum[2],rz[1]);
        }
    return result;
}
DepartureProof departureProof(const Snapshot & input, const Contact & contact)
{
    validate(input);
    for(double value:contact.point)if(!std::isfinite(value))throw std::invalid_argument("Rotation contact point is invalid");
    for(double value:contact.normal)if(!std::isfinite(value))throw std::invalid_argument("Rotation contact normal is invalid");
    const auto pose=poseY(input.pitch,input.roll);
    if (contact.normal[1]<-.2 || std::abs(pose[1])<.1) return {false,false};
    const double first=input.start_yaw, last=first+angleDelta(first,input.end_yaw);
    const double sine=std::sin(first), cosine=std::cos(first);
    const double dx=contact.point[0]-input.position[0],dy=contact.point[1]-input.position[1],dz=contact.point[2]-input.position[2];
    const double x=dx*cosine-dz*sine,z=dx*sine+dz*cosine;
    const double y=(dy-x*pose[0]-z*pose[2])/pose[1];
    const auto & low=input.body.minimum; const auto & high=input.body.maximum;
    const bool inside=low[0]-.001<=x && x<=high[0]+.001 && low[1]-.001<=y && y<=high[1]+.001 && low[2]-.001<=z && z<=high[2]+.001;
    double before=std::numeric_limits<double>::infinity(),after=before,swept=before;
    const double drift=input.translation[0]*contact.normal[0]+input.translation[1]*contact.normal[2];
    const double constant=input.pivot_offset*(contact.normal[0]*cosine-contact.normal[2]*sine);
    for (double cx: {low[0],high[0]}) for (double cy: {low[1],high[1]}) for (double cz: {low[2],high[2]}) {
        const double px=cx-input.pivot_offset;
        const double a=contact.normal[0]*px+contact.normal[2]*cz,b=contact.normal[0]*cz-contact.normal[2]*px;
        const double height=contact.normal[1]*(cx*pose[0]+cy*pose[1]+cz*pose[2]);
        before=std::min(before,a*cosine+b*sine+constant+height);
        after=std::min(after,a*std::cos(last)+b*std::sin(last)+constant+height+drift);
        swept=std::min(swept,intervalMinimum(a,b,first,last,drift)+constant+height);
    }
    const auto & normal=contact.normal;
    const double length=std::pow(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2],.5);
    const bool drivable=length>0. && normal[1]/length>=1./std::pow(1.+input.drivable_gradient*input.drivable_gradient,.5);
    return {(after>before+1.e-8 || (drivable && after>=before-1.e-8)) && swept>=before-1.e-8,inside};
}
Result run(const Snapshot & input, WorldEngine & engine)
{
    validate(input);
    Result result;
    if (!input.include_static && !input.pivot_offset && std::hypot(input.translation[0],input.translation[1])<=1.e-9 && input.replacement_reader_available && !input.replacement_active) {
        result.skipped=true; return result;
    }
    const double delta=angleDelta(input.start_yaw,input.end_yaw);
    if (std::abs(delta)<=1.e-8) {result.skipped=true;return result;}
    if (!(input.max_angle_step>0.)) throw std::invalid_argument("Rotation angle step is invalid");
    const double count=std::ceil(std::abs(delta)/input.max_angle_step);
    if (!(count>=1. && count<=input.max_steps)) throw std::invalid_argument("Native world rotation sweep exceeds its bound");
    const auto steps=static_cast<std::uint32_t>(count);
    PreviousContacts previous(input,engine,result);
    // Every slice has the same angular width and pivot. Reuse its analytical
    // envelope, while retaining the exact original world/query order.
    const auto interval=intervalEnvelope(input.body,std::abs(delta)*.5/steps,input.pivot_offset);
    for (std::uint32_t index=0;index<steps;++index) {
        ++result.slices;
        const double lower=static_cast<double>(index)/steps,upper=static_cast<double>(index+1)/steps,middle=(lower+upper)*.5;
        const double yaw=input.start_yaw+delta*middle,first=input.start_yaw+delta*lower,last=input.start_yaw+delta*upper;
        auto position=pivotPosition(input.position,input.start_yaw,yaw,input.pivot_offset);
        position[0]+=input.translation[0]*lower;position[2]+=input.translation[1]*lower;
        const std::array<double,2> travel{{input.translation[0]/steps,input.translation[1]/steps}};
        const double distance=std::hypot(travel[0],travel[1]);
        auto start=pivotPosition(input.position,input.start_yaw,first,input.pivot_offset);
        start[0]+=input.translation[0]*lower;start[2]+=input.translation[1]*lower;
        Snapshot departing=input;departing.position=start;departing.start_yaw=first;departing.end_yaw=last;departing.translation=travel;
        RotationDeparture policy(departing,previous,result);
        const double motion_yaw=distance?std::atan2(travel[0],travel[1]):0.;
        const double speeds[2]={distance?distance:1.e-6,-1.e-6};
        for (unsigned direction=0;direction<(distance?1u:2u);++direction) {
            const auto snapshot=worldSnapshot(input,interval,position,yaw,speeds[direction],distance!=0.,motion_yaw,true);
            ++result.world_calls;
            const auto outcome=engine.run(snapshot,&policy);
            validateOutcome(outcome,result);
            if (outcome.status!=0) {
                result.clear=false;result.blocking_failure=outcome.status<0;
                result.blocking_trace=outcome.trace;return result;
            }
        }
    }
    return result;
}
}
