#include "stock_motion_rotation_batch.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace stock_motion_rotation;
double scalar() { std::string token; if(!(std::cin>>token))throw std::runtime_error("Missing fixture value"); return std::strtod(token.c_str(),nullptr); }
Point point() { return {{scalar(),scalar(),scalar()}}; }
Snapshot snapshot()
{
    Snapshot value{};value.position=point();value.body.minimum=point();value.body.maximum=point();
    value.start_yaw=scalar();value.end_yaw=scalar();value.pitch=scalar();value.roll=scalar();value.pivot_offset=scalar();
    value.translation={{scalar(),scalar()}};value.max_angle_step=scalar();value.drivable_gradient=scalar();
    value.max_steps=static_cast<std::uint32_t>(scalar());value.include_static=scalar()!=0.;
    value.replacement_reader_available=scalar()!=0.;value.replacement_active=scalar()!=0.;return value;
}
Contact contact() { return {point(),point()}; }
struct Script {int status;bool trace;Contact hit;std::vector<Contact> candidates;};
struct Verdict {std::size_t world,candidate;bool accepted;};
void emitPoint(const Point & p) {std::cout<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<']';}
void emitSnapshot(const WorldSnapshot & s)
{
    std::cout<<'[';
    for(unsigned i=0;i<3;++i)std::cout<<(i?",":"")<<s.position[i];
    std::cout<<','<<s.yaw<<','<<s.speed<<','<<s.dt<<','<<s.pitch<<','<<s.roll<<','<<s.motion_yaw;
    for(double value:s.bounds)std::cout<<','<<value;
    for(double value:s.heights)std::cout<<','<<value;
    std::cout<<','<<s.passive<<','<<s.airborne<<','<<s.exact_footprint<<','<<s.supplied_departing<<']';
}
class ScriptEngine final:public WorldEngine {
public:
    std::vector<Script> scripts;
    std::vector<WorldSnapshot> calls;
    std::vector<Verdict> verdicts;
    WorldResult run(const WorldSnapshot & s,DeparturePolicy * policy)override {
        const std::size_t index=calls.size();calls.push_back(s);
        if(index>=scripts.size())throw std::runtime_error("World fixture exhausted");
        // Copy before policy execution: a lazy proof can re-enter this engine.
        const auto script=scripts[index];bool accepted=true;
        if(policy)for(std::size_t i=0;i<script.candidates.size();++i){const bool value=policy->accepts(script.candidates[i]);verdicts.push_back({index,i,value});if(!value){accepted=false;break;}}
        WorldResult result;result.status=script.status==-99?(accepted?0:1):script.status;
        result.trace.present=script.trace;result.trace.snapshot=s;
        for(unsigned i=0;i<3;++i){result.trace.response.point[i]=script.hit.point[i];result.trace.response.normal[i]=script.hit.normal[i];}
        return result;
    }
};
void emitEngine(const ScriptEngine & engine)
{
    std::cout<<",\"world\":[";
    for(std::size_t i=0;i<engine.calls.size();++i){if(i)std::cout<<',';emitSnapshot(engine.calls[i]);}
    std::cout<<"],\"verdicts\":[";
    for(std::size_t i=0;i<engine.verdicts.size();++i){const auto & v=engine.verdicts[i];if(i)std::cout<<',';std::cout<<'['<<v.world<<','<<v.candidate<<','<<(v.accepted?1:0)<<']';}
    std::cout<<']';
}
// The real world_stage kernel is exercised without Python/BigWorld. This
// fixture exposes only misses; geometry/effect owners remain adapter work.
class MissFrame final:public WorldFrame {
public:
    std::uint32_t rays=0,filters=0;
    int dispatch(std::uint32_t op,const WorldRequest * q,std::uint32_t count,WorldResponse * r)override {
        if(op==WORLD_HORIZONTAL || op==WORLD_GROUND)for(std::uint32_t i=0;i<count;++i){++rays;for(unsigned axis=0;axis<3;++axis){r[i].start[axis]=static_cast<float>(q[i].start[axis]);r[i].end[axis]=static_cast<float>(q[i].end[axis]);}}
        else if(op==WORLD_FILTER)++filters;
        return 1;
    }
    Trace trace()const override{return Trace{};}
};
class MissFactory final:public WorldFrameFactory {
public:
    std::uint32_t worlds=0;
    std::unique_ptr<WorldFrame> begin(const WorldSnapshot &,DeparturePolicy *)override {
        ++worlds;return std::unique_ptr<WorldFrame>(new MissFrame());
    }
};
// A real world_stage trace callback may publish its hit and then abort. The
// adapter must retain that existing trace even though the core returns -1.
class FailureFrame final:public WorldFrame {
    bool fail_trace_;
    Trace trace_;
    WorldResponse hit_{};
public:
    explicit FailureFrame(bool fail_trace):fail_trace_(fail_trace) {}
    int dispatch(std::uint32_t op,const WorldRequest * q,std::uint32_t count,WorldResponse * r)override {
        if(!fail_trace_)return 0;
        if(op==WORLD_HORIZONTAL)for(std::uint32_t i=0;i<count;++i){
            r[i].status=1;r[i].handle=47;r[i].normal[0]=1.;
            for(unsigned axis=0;axis<3;++axis){r[i].start[axis]=q[i].start[axis];r[i].end[axis]=q[i].end[axis];r[i].point[axis]=(q[i].start[axis]+q[i].end[axis])*.5;}
            hit_=r[i];
        }
        if(op==WORLD_TRACE){trace_.present=true;trace_.request=*q;trace_.response=hit_;return 0;}
        return 1;
    }
    Trace trace()const override{return trace_;}
};
class FailureFactory final:public WorldFrameFactory {
    bool fail_trace_;
public:
    std::uint32_t worlds=0;
    explicit FailureFactory(bool fail_trace):fail_trace_(fail_trace) {}
    std::unique_ptr<WorldFrame> begin(const WorldSnapshot &,DeparturePolicy *)override {
        ++worlds;return std::unique_ptr<WorldFrame>(new FailureFrame(fail_trace_));
    }
};
}
int main()
{
    std::cout<<std::setprecision(17);unsigned count;std::cin>>count;
    for(unsigned index=0;index<count;++index){int mode;std::cin>>mode;const auto input=snapshot();ScriptEngine engine;
        if(mode==2){unsigned scripts;std::cin>>scripts;for(unsigned i=0;i<scripts;++i){Script script;script.status=static_cast<int>(scalar());script.trace=scalar()!=0.;script.hit=contact();unsigned candidates;std::cin>>candidates;for(unsigned j=0;j<candidates;++j)script.candidates.push_back(contact());engine.scripts.push_back(script);}}
        Contact probe{};if(mode==1)probe=contact();
        try{
            if(mode==0){const auto result=intervalEnvelope(input.body,input.max_angle_step,input.pivot_offset);std::cout<<"{\"bounds\":[";emitPoint(result.minimum);std::cout<<',';emitPoint(result.maximum);std::cout<<"]}";}
            else if(mode==1){const auto result=departureProof(input,probe);std::cout<<"{\"proof\":["<<(result.improves?1:0)<<','<<(result.inside?1:0)<<"]}";}
            else if(mode==2){const auto result=run(input,engine);std::cout<<"{\"result\":["<<(result.clear?1:0)<<','<<(result.skipped?1:0)<<','<<result.slices<<','<<result.world_calls<<','<<result.previous_world_calls<<','<<result.departure_tests<<','<<result.query_failures<<','<<(result.blocking_failure?1:0)<<"],\"trace\":"<<(result.blocking_trace.present?1:0)<<",\"trace_values\":";if(result.blocking_trace.present){std::cout<<'[';for(unsigned i=0;i<3;++i)std::cout<<(i?",":"")<<result.blocking_trace.response.point[i];for(unsigned i=0;i<3;++i)std::cout<<','<<result.blocking_trace.response.normal[i];std::cout<<']';}else std::cout<<"null";emitEngine(engine);std::cout<<'}';}
            else if(mode==3) {MissFactory factory;WorldStageEngine core(factory);const auto result=run(input,core);std::cout<<"{\"core\":["<<(result.clear?1:0)<<','<<factory.worlds<<"]}";}
            else {FailureFactory factory(mode==4);WorldStageEngine core(factory);const auto result=run(input,core);std::cout<<"{\"failed_core\":["<<(result.clear?1:0)<<','<<factory.worlds<<','<<result.query_failures<<','<<(result.blocking_failure?1:0)<<','<<(result.blocking_trace.present?1:0)<<"]}";}
        }catch(const std::invalid_argument &){std::cout<<"{\"error\":\"invalid\"";emitEngine(engine);std::cout<<'}';}
        catch(const std::exception &){std::cout<<"{\"error\":\"runtime\"";emitEngine(engine);std::cout<<'}';}
        std::cout<<'\n';
    }
}
