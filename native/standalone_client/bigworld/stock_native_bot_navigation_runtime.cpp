#include "stock_native_bot_navigation_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <stdexcept>
#include <sstream>

namespace stock_native_bot_navigation {
namespace {
const std::array<double,5> lanes{-10.,-5.,0.,5.,10.};
double distance(const Point&a,const Point&b){const double x=a[0]-b[0],z=a[2]-b[2];return std::sqrt(x*x+z*z);}
void keyPart(std::ostringstream&out,const std::string&s){out<<s.size()<<':'<<s;}
std::string identity(std::initializer_list<std::string> values){std::ostringstream out;for(const auto&v:values)keyPart(out,v);return out.str();}
std::string pointIdentity(const Point&p){std::ostringstream out;out<<std::hexfloat<<p[0]<<','<<p[1]<<','<<p[2];return out.str();}
std::string cellIdentity(Cell c){return std::to_string(c[0])+","+std::to_string(c[1]);}
bool routeMode(const Strategy&s){return s.mode=="route"||s.mode=="advance";}
}
std::vector<double> laneFallbacks(double offset)
{if(offset>=7.5)return {10.,5.,0.};if(offset<=-7.5)return {-10.,-5.,0.};if(offset>0.)return {5.,0.};if(offset<0.)return {-5.,0.};return {0.};}
std::vector<double> rowFallbacks(double offset)
{return std::abs(offset)<1.e-9?std::vector<double>{0.}:std::vector<double>{offset,offset*.5,0.};}
std::vector<double> laneLayout(int count)
{
    count=std::max(1,count);if(count==1)return {0.};if(count==2)return {-5.,5.};if(count==3)return {-5.,0.,5.};if(count==4)return {-10.,-5.,5.,10.};
    std::vector<double> result;for(int n=0;n<count;++n)result.push_back(lanes[((2*n+1)*5)/(2*count)]);return result;
}
std::vector<double> rowLayout(int count)
{
    count=std::max(1,count);if(count==1)return {0.};const int rows=std::min(5,count);std::vector<double> values;
    for(int n=0;n<rows;++n)values.push_back(-4.+8.*n/double(rows-1));if(count==rows)return values;
    std::vector<double> result;for(int n=0;n<count;++n)result.push_back(values[int(std::round(n*double(rows-1)/double(count-1)))]);return result;
}
NavigationRuntime::NavigationRuntime(Navigator&nav):navigator_(nav),grid_(nav.grid){}
std::vector<std::int64_t> NavigationRuntime::cohort(std::int64_t id,const LaneGroup&group,const LaneGate&gate)
{
    std::vector<std::int64_t> result;
    for(auto&entry:actors){auto&peer=entry.second;if(!peer.alive||peer.team!=group.first)continue;
        std::optional<std::string> route;std::optional<LaneGate> peer_gate;
        if(peer.lane_group==group&&peer.lane_gate){route=peer.lane_group->second;peer_gate=peer.lane_gate;}
        else{auto order=orders.find(entry.first);if(order!=orders.end()){
            route=order->second.route_id;if(order->second.route_index&&order->second.route_join)peer_gate=LaneGate{*order->second.route_index,*order->second.route_join};}
            else if(!orders.empty())continue;}
        if(!route&&orders.empty()){
            if(!peer.local_route_id.empty())route=peer.local_route_id;
            if(peer.local_route_index&&peer.local_route_join)peer_gate=LaneGate{*peer.local_route_index,*peer.local_route_join};else if(entry.first==id)peer_gate=gate;}
        if(route&&*route==group.second&&peer_gate==gate)result.push_back(entry.first);}
    if(actors.count(id)&&std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);
    std::sort(result.begin(),result.end(),[&](auto a,auto b){return std::tie(actors.at(a).slot,a)<std::tie(actors.at(b).slot,b);});return result;
}
Vec NavigationRuntime::laneForward(const NavigationActor&actor,const Point&goal,const Strategy&strategy)const
{
    const int index=strategy.route_index.value_or(0);if(actor.local_route_id==strategy.route_id&&!actor.local_route.empty()){
        std::optional<Vec> first,second;if(index>0&&std::size_t(index)<actor.local_route.size()){first=actor.local_route[index-1];second=actor.local_route[index];}
        else if(actor.local_route.size()>1){first=actor.local_route[0];second=actor.local_route[1];}
        if(first&&second){const double dx=(*second)[0]-(*first)[0],dz=(*second)[1]-(*first)[1],length=std::hypot(dx,dz);if(length>=.25)return {dx/length,dz/length};}}
    const auto anchor=strategy.route_anchor.value_or(actor.position);const double dx=goal[0]-anchor[0],dz=goal[2]-anchor[2],length=std::hypot(dx,dz);
    return length<.25?Vec{std::sin(actor.yaw),std::cos(actor.yaw)}:Vec{dx/length,dz/length};
}
std::pair<double,double> NavigationRuntime::laneBinding(std::int64_t id,const LaneGroup&group,const Point&goal,const Strategy&strategy)
{
    auto found=actors.find(id);if(found==actors.end())return {0.,0.};auto&own=found->second;LaneGate gate{strategy.route_index.value_or(0),strategy.route_join.value_or(false)};
    auto order=orders.find(id);if(order!=orders.end()&&order->second.route_id==group.second&&order->second.route_index&&order->second.route_join)
        gate={*order->second.route_index,*order->second.route_join};
    if(own.lane_group==group){if(gate.second&&own.lane_gate!=gate){own.lane_gate=gate;own.lane_forward=laneForward(own,goal,strategy);own.lane_segment.reset();own.lane_offset=own.lane_row=0.;own.lane_goal.reset();}
        return {own.lane_desired,own.row_desired};}
    auto peers=cohort(id,group,gate);if(peers.empty())return {0.,0.};
    for(auto peer_id:peers){auto&peer=actors.at(peer_id);if(peer.lane_group!=group||peer.lane_gate!=gate||!peer.lane_origin)peer.lane_origin=peer.position;}
    const auto forward=laneForward(own,goal,strategy);const Vec lateral{-forward[1],forward[0]};
    auto projection=[&](std::int64_t peer,const Vec&axis){const auto&p=*actors.at(peer).lane_origin;return p[0]*axis[0]+p[2]*axis[1];};
    std::sort(peers.begin(),peers.end(),[&](auto a,auto b){return std::make_tuple(projection(a,lateral),actors.at(a).slot,a)<std::make_tuple(projection(b,lateral),actors.at(b).slot,b);});
    const auto layout=laneLayout(int(peers.size()));std::map<double,std::vector<std::int64_t>> members;for(std::size_t n=0;n<peers.size();++n)members[layout[n]].push_back(peers[n]);
    std::map<std::int64_t,std::pair<double,double>> assignments;
    for(double lane:lanes){auto list=members[lane];std::sort(list.begin(),list.end(),[&](auto a,auto b){return std::make_tuple(projection(a,forward),actors.at(a).slot,a)<std::make_tuple(projection(b,forward),actors.at(b).slot,b);});
        auto rows=rowLayout(int(list.size()));for(std::size_t n=0;n<list.size();++n)assignments[list[n]]={lane,rows[n]};}
    std::set<std::pair<double,double>> occupied;
    for(auto peer:peers){auto&p=actors.at(peer);if(p.lane_group==group&&p.lane_gate==gate)occupied.insert({p.lane_desired,p.row_desired});}
    const std::array<double,5> row_choices{-4.,-2.,0.,2.,4.};
    if(!occupied.empty())for(auto peer:peers){if(actors.at(peer).lane_group==group)continue;const auto expected=assignments[peer];
        std::vector<std::pair<double,double>> available;for(double lane:lanes){bool used=false;for(const auto&v:occupied)if(v.first==lane)used=true;if(!used)available.push_back({lane,0.});}
        bool reuse=available.empty();if(reuse)for(double lane:lanes)for(double row:row_choices)if(!occupied.count({lane,row}))available.push_back({lane,row});
        auto score=[&](const std::pair<double,double>&v){double nearest=std::numeric_limits<double>::infinity();for(const auto&used:occupied)
                nearest=std::min(nearest,(v.first-used.first)*(v.first-used.first)+(v.second-used.second)*(v.second-used.second));
            const double dx=v.first-expected.first,dz=v.second-expected.second;
            return std::make_tuple(reuse?-nearest:0.,dx*dx+dz*dz,std::abs(dx),std::abs(dz),
                std::find(lanes.begin(),lanes.end(),v.first)-lanes.begin(),std::find(row_choices.begin(),row_choices.end(),v.second)-row_choices.begin());};
        auto desired=available.empty()?std::pair<double,double>{0.,0.}:*std::min_element(available.begin(),available.end(),[&](const auto&a,const auto&b){return score(a)<score(b);});
        assignments[peer]=desired;occupied.insert(desired);}
    for(auto peer_id:peers){auto&peer=actors.at(peer_id);if(peer.lane_group==group)continue;const auto desired=assignments[peer_id];
        peer.lane_group=group;peer.lane_gate=gate;peer.lane_desired=desired.first;peer.row_desired=desired.second;peer.lane_forward=forward;peer.lane_segment.reset();peer.lane_offset=peer.lane_row=0.;peer.lane_goal.reset();}
    return {own.lane_desired,own.row_desired};
}
std::optional<Vec> NavigationRuntime::legForward(const NavigationActor&actor,const Point&position,const Point&goal,const Strategy&strategy,bool joining)const
{
    if(joining&&actor.lane_forward){const double length=std::hypot((*actor.lane_forward)[0],(*actor.lane_forward)[1]);if(length>=.25)return Vec{(*actor.lane_forward)[0]/length,(*actor.lane_forward)[1]/length};}
    auto anchor=strategy.route_anchor.value_or(position);double x=goal[0]-anchor[0],z=goal[2]-anchor[2],length=std::hypot(x,z);
    if(length<.25){x=goal[0]-position[0];z=goal[2]-position[2];length=std::hypot(x,z);}if(length<.25)return {};return Vec{x/length,z/length};
}
std::optional<std::array<double,4>> NavigationRuntime::overflow(const NavigationActor&actor,const Point&p,double yaw)const
{
    const auto&b=grid_.map.bounds;if(!grid_.map.has_bounds||b[0]>=b[2]||b[1]>=b[3])return {};
    for(double value:b)if(!std::isfinite(value))return {};const double sine=std::abs(std::sin(yaw)),cosine=std::abs(std::cos(yaw));
    const double length=std::max(.5,actor.half_length),width=std::max(.3,actor.half_width),x=cosine*width+sine*length,z=sine*width+cosine*length;
    return std::array<double,4>{std::max(0.,b[0]+x-p[0]),std::max(0.,p[0]+x-b[2]),std::max(0.,b[1]+z-p[2]),std::max(0.,p[2]+z-b[3])};
}
std::optional<Point> NavigationRuntime::safeLaneGoal(const NavigationActor&actor,const Point&goal,Vec forward,double lateral,double row,double now)
{
    if(std::abs(lateral)<1.e-9&&std::abs(row)<1.e-9)return goal;
    const double x=goal[0]-forward[1]*lateral+forward[0]*row,z=goal[2]+forward[0]*lateral+forward[1]*row;
    if(std::hypot(x-goal[0],z-goal[2])+1.5>13.+1.e-9)return {};auto y=grid_.ground(x,z,goal[1]);if(!y)return {};Point candidate{x,*y,z};
    if(grid_.pointHazard(goal,7)||grid_.pointHazard(candidate,7)||grid_.segmentHazard(goal,candidate,7)||!grid_.drySegment(goal,candidate,now))return {};
    auto bounds=overflow(actor,candidate,std::atan2(forward[0],forward[1]));if(!bounds)return {};for(double value:*bounds)if(value>1.e-6)return {};return candidate;
}
std::pair<Point,std::pair<int,int>> NavigationRuntime::laneGoal(std::int64_t id,const Point&position,const Point&goal,const Strategy&strategy,double now,bool joining)
{
    auto found=actors.find(id);if(found==actors.end())return {goal,{0,0}};auto&actor=found->second;LaneGroup group{actor.team,strategy.route_id};
    auto desired=laneBinding(id,group,goal,strategy);auto segment=std::make_tuple(group,strategy.route_index.value_or(0),std::optional<bool>(joining));
    if(actor.lane_segment!=segment){actor.lane_segment=segment;actor.lane_offset=desired.first;actor.lane_row=joining?desired.second:0.;actor.lane_goal.reset();}
    auto forward=legForward(actor,position,goal,strategy,joining);if(!forward){actor.lane_offset=actor.lane_row=0.;actor.lane_goal=goal;return {goal,{0,0}};}
    bool reject=false;auto state=navigator_.states.find(id);if(state!=navigator_.states.end()&&state->second.status=="blocked"&&actor.lane_goal&&state->second.planned_goal)
        reject=distance(*actor.lane_goal,*state->second.planned_goal)<.25;
    bool first=true;std::set<std::pair<double,double>> seen;
    for(double row:rowFallbacks(actor.lane_row))for(double offset:laneFallbacks(actor.lane_offset)){
        const auto identity=std::make_pair(std::round(offset*1.e6)/1.e6,std::round(row*1.e6)/1.e6);if(!seen.insert(identity).second)continue;
        if(reject&&first){first=false;continue;}first=false;auto candidate=safeLaneGoal(actor,goal,*forward,offset,row,now);if(!candidate)continue;
        actor.lane_offset=offset;actor.lane_row=row;actor.lane_goal=candidate;return {*candidate,{int(std::round(offset*1000.)),int(std::round(row*1000.))}};}
    actor.lane_offset=actor.lane_row=0.;actor.lane_goal=goal;return {goal,{0,0}};
}
Point NavigationRuntime::laneTarget(std::int64_t id,const Point&position,const Point&goal,const Point&selected,const Strategy&strategy,double now)
{
    auto found=actors.find(id);const double dx=selected[0]-position[0],dz=selected[2]-position[2];if(found==actors.end()||std::hypot(dx,dz)<.25)return selected;auto&actor=found->second;
    LaneGroup group{actor.team,strategy.route_id};const auto desired=laneBinding(id,group,goal,strategy);auto segment=std::make_tuple(group,strategy.route_index.value_or(0),std::optional<bool>{});
    if(actor.lane_segment!=segment){actor.lane_segment=segment;actor.lane_offset=desired.first;}
    auto nav=navigator_.states.find(id);if(nav!=navigator_.states.end()&&nav->second.controlled_shallow_target){actor.lane_offset=0.;return selected;}
    auto anchor=strategy.route_anchor.value_or(position);double rx=goal[0]-anchor[0],rz=goal[2]-anchor[2],length=std::hypot(rx,rz);
    if(length<.25){rx=dx;rz=dz;length=std::hypot(rx,rz);}if(length<.25)return selected;const double lx=-rz/length,lz=rx/length;
    for(double offset:laneFallbacks(actor.lane_offset)){
        if(std::abs(offset)<1.e-9){actor.lane_offset=0.;return selected;}
        const double x=selected[0]+lx*offset,z=selected[2]+lz*offset,cx=x-position[0],cz=z-position[2];
        if(std::hypot(cx,cz)<=1.5+1.e-9||dx*cx+dz*cz<=1.e-9)continue;auto y=grid_.ground(x,z,selected[1]);if(!y)continue;const Point candidate{x,*y,z};
        if(navigator_.segmentPenalized(id,position,candidate,now)||grid_.pointHazard(candidate,7)||grid_.segmentHazard(position,candidate,7)||!grid_.drySegment(position,candidate,now))continue;
        auto bounds=overflow(actor,candidate,std::atan2(cx,cz));if(!bounds||std::any_of(bounds->begin(),bounds->end(),[](double v){return v>1.e-6;}))continue;
        actor.lane_offset=offset;return candidate;}
    actor.lane_offset=0.;return selected;
}
std::optional<Point> NavigationRuntime::radioGoal(std::int64_t id,const Point&position,const Point&goal,const Strategy&strategy)
{
    if(!strategy.move_area)return {};auto found=actors.find(id);if(found==actors.end())return {};auto&actor=found->second;
    const auto key=std::make_pair(strategy.team_command,*strategy.move_area);if(actor.radio_goal&&actor.radio_goal->first==key)return actor.radio_goal->second;
    const auto&area=*strategy.move_area;const auto minimum=grid_.cellFor({area[0],position[1],area[1]}),maximum=grid_.cellFor({area[2],position[1],area[3]});
    std::optional<std::tuple<bool,double,Cell,Point>> best;
    for(int z=minimum[1];z<=maximum[1];++z)for(int x=minimum[0];x<=maximum[0];++x){const Cell cell{x,z};auto height=grid_.height(cell);if(!height)continue;const auto point=grid_.pointFor(cell,*height);
        if(!(area[0]<=point[0]&&point[0]<=area[2]&&area[1]<=point[2]&&point[2]<=area[3])||grid_.pointHazard(point,3))continue;
        const auto candidate=std::make_tuple(grid_.pointHazard(point,4),distance(point,goal),cell,point);if(!best||candidate<*best)best=candidate;}
    std::optional<Point> result;if(best)result=std::get<3>(*best);actor.radio_goal=std::make_pair(key,result);return result;
}
std::vector<std::pair<Point,double>> NavigationRuntime::physicalOccupancy(const NavigationActor&own)const
{
    std::vector<std::pair<Point,double>> result;
    for(const auto&entry:actors){const auto&peer=entry.second;if(peer.id==own.id)continue;auto order=orders.find(peer.id);
        if(peer.alive&&!(order!=orders.end()&&order->second.parking_phase=="waiting")&&!peer.spg)continue;
        result.push_back({peer.position,std::hypot(peer.collision_half_width,peer.collision_half_length)});}
    for(const auto&human:humans)if(human.world_pose&&human.participating)result.push_back({human.position,std::hypot(human.collision_half_width,human.collision_half_length)});
    return result;
}
std::optional<Point> NavigationRuntime::occupiedTarget(std::int64_t id,const Point&position,const Point&goal,double now,bool direct)
{
    auto found=actors.find(id);if(found==actors.end()||std::hypot(goal[0]-position[0],goal[2]-position[2])>32.)return {};auto&actor=found->second;
    const OccupiedKey key{grid_.cellFor(goal),direct};auto cached=actor.occupied_cache.find(key);
    if(cached!=actor.occupied_cache.end()&&now<cached->second.until){if(cached->second.blocked)actor.route_wreck_blocked=true;if(!direct)actor.occupied_route_point=cached->second.blocked;return cached->second.result;}
    const double radius=std::hypot(actor.half_length,actor.half_width);auto height=grid_.height(grid_.cellFor(goal));
    if(!height){actor.occupied_cache[key]={now+1.,{},false};return {};}
    auto bodies=physicalOccupancy(actor);bodies.erase(std::remove_if(bodies.begin(),bodies.end(),[&](const auto&body){return std::abs(body.first[1]-*height)>std::max(3.,radius);}),bodies.end());
    auto occupied=[&](const Point&p){return std::any_of(bodies.begin(),bodies.end(),[&](const auto&body){return std::hypot(p[0]-body.first[0],p[2]-body.first[2])<radius+body.second+.5;});};
    auto corridor=[&](const Point&p){const double dx=p[0]-position[0],dz=p[2]-position[2],length=std::max(.01,dx*dx+dz*dz);
        for(const auto&body:bodies){const double bx=body.first[0]-position[0],bz=body.first[2]-position[2],f=std::max(0.,std::min(1.,(bx*dx+bz*dz)/length));
            if(std::hypot(bx-dx*f,bz-dz*f)<radius+body.second+.5&&bx*dx+bz*dz>0.)return false;}return true;};
    std::optional<Point> result;const bool blocked=occupied(goal);
    if(blocked){if(actor.occupied_leg&&actor.occupied_leg->first==key&&distance(position,actor.occupied_leg->second)>1.5&&!occupied(actor.occupied_leg->second)&&
            corridor(actor.occupied_leg->second)&&(!direct||grid_.drySegment(position,actor.occupied_leg->second,now)))result=actor.occupied_leg->second;
        const double dx=goal[0]-position[0],dz=goal[2]-position[2],length=std::max(.01,std::hypot(dx,dz));const Vec forward{dx/length,dz/length};
        for(double reach:{8.,12.}){if(result)break;for(const auto&offset:std::array<Vec,4>{{{-forward[1],forward[0]},{forward[1],-forward[0]},forward,{-forward[0],-forward[1]}}}){
            const double x=goal[0]+offset[0]*reach,z=goal[2]+offset[1]*reach;auto y=grid_.height(grid_.cellFor({x,*height,z}));if(!y)continue;Point point{x,*y,z};
            if(std::hypot(x-position[0],z-position[2])<=1.5||occupied(point)||!corridor(point)||grid_.pointHazard(point,7)||(direct&&!grid_.drySegment(position,point,now)))continue;
            result=point;actor.occupied_leg=std::make_pair(key,point);break;}}
        actor.route_wreck_blocked=true;}
    if(!direct)actor.occupied_route_point=blocked;if(actor.occupied_cache.size()>=4)actor.occupied_cache.clear();actor.occupied_cache[key]={now+1.,result,blocked};return result;
}
std::string NavigationRuntime::spgIdentity(const InitialSPG&plan)
{return identity({plan.catalog,plan.map,plan.zone,plan.cell,pointIdentity(plan.point)});}
TargetResult NavigationRuntime::target(std::int64_t id,const Point&position,Point goal,const Strategy&strategy,double now)
{
    auto found=actors.find(id);if(found==actors.end())throw std::invalid_argument("navigation actor missing");auto&actor=found->second;
    TargetResult result;actor.route_wreck_blocked=false;actor.occupied_route_point=false;const bool stop=!routeMode(strategy);
    std::optional<Point> occupied;if(routeMode(strategy)&&!strategy.throttle)occupied=occupiedTarget(id,position,goal,now);
    if((routeMode(strategy)||strategy.mode=="parking_approach")&&!strategy.throttle){const auto route_index=strategy.route_index.value_or(0);auto&receipt=actor.wreck_receipt;
        if(!receipt||std::get<0>(*receipt)!=strategy.route_id||std::get<1>(*receipt)!=route_index||std::get<2>(*receipt)!=goal||std::get<3>(*receipt)!=grid_.hull_revision||now>=std::get<4>(*receipt)){
            Path remaining{position};auto nav=navigator_.states.find(id);if(nav!=navigator_.states.end()&&nav->second.path_key){auto path=navigator_.paths.find(*nav->second.path_key);
                if(path!=navigator_.paths.end()){const auto&points=*path->second.path;const auto start=std::min(nav->second.index,points.size());remaining.insert(remaining.end(),points.begin()+start,points.end());}}
            const bool blocked=grid_.pathCrossesStaticHull({position,goal})||grid_.pathCrossesStaticHull({strategy.route_anchor.value_or(position),goal})||grid_.pathCrossesStaticHull(remaining);
            receipt=std::make_tuple(strategy.route_id,route_index,goal,grid_.hull_revision,now+1.,blocked);}
        actor.route_wreck_blocked=std::get<5>(*receipt)||actor.occupied_route_point;}
    if(occupied){goal=*occupied;actor.route_wreck_blocked=true;}
    if(strategy.move_area){auto grounded=radioGoal(id,position,goal,strategy);if(!grounded){result.target=position;result.stop_at_target=true;return result;}goal=*grounded;}
    PathKey key;std::optional<Point> anchor;
    if(actor.spg&&(strategy.mode=="artillery_hold"||strategy.mode=="artillery_deploy"))key={"spg_initial",identity({std::to_string(id),spgIdentity(*actor.spg)}),{}, {}};
    else if(strategy.mode=="base_defense")key={"local",identity({"base_defense",strategy.defense_base}),{},id};
    else if(routeMode(strategy)||strategy.mode=="hold"){
        if(strategy.route_join.value_or(false))anchor=strategy.route_anchor;std::pair<int,int> lane{0,0};
        if(routeMode(strategy)&&anchor){auto shifted=laneGoal(id,position,goal,strategy,now,true);goal=shifted.first;lane=shifted.second;}
        if(anchor)key={"route_join",identity({std::to_string(actor.team),strategy.route_id,std::to_string(strategy.route_index.value_or(0)),std::to_string(lane.first),std::to_string(lane.second)}),{},id};
        else key={"route",identity({std::to_string(actor.team),strategy.route_id,std::to_string(strategy.route_index.value_or(0))}),{}, {}};
    }else key={"local",identity({strategy.mode,strategy.target?std::to_string(*strategy.target):"none"}),{},id};
    if(occupied)key={"local",identity({"occupied_route",strategy.route_id,std::to_string(strategy.route_index.value_or(0)),cellIdentity(grid_.cellFor(goal))}),{},id};
    const double horizon=std::max(grid_.map.cell_size*2.,std::abs(actor.speed)*1.5);const bool direct_close=distance(position,goal)<=15.;
    bool direct=direct_close&&!navigator_.segmentPenalized(id,position,goal,now)&&grid_.drySegment(position,goal,now);
    const bool movement=(!strategy.throttle||*strategy.throttle>0.)&&!actor.traffic_waiting&&!actor.artillery_intent&&!actor.artillery_reproof;
    auto retained=navigator_.retainedTarget(id,position,goal,key,now,movement,actor.yaw);Point target;
    if(retained){target=*retained;direct=false;}
    else if(direct)target=navigator_.observeDirect(id,position,goal,key,now,movement).value_or(goal);
    else{auto navigation_anchor=anchor;if(key.kind=="route")navigation_anchor=strategy.route_anchor.value_or(position);
        target=navigator_.nextTarget(id,position,goal,key,now,navigation_anchor,{},horizon,movement);}
    result.stop_at_target=stop&&((direct&&target==goal)||(!direct&&navigator_.targetTerminal(id)));const Point selected=target;
    if(!retained&&routeMode(strategy)&&!anchor)target=laneTarget(id,position,goal,target,strategy,now);
    if(!retained&&routeMode(strategy)&&!strategy.throttle){auto bypass=occupiedTarget(id,position,target,now,true);if(bypass){target=*bypass;result.stop_at_target=false;}}
    if(movement&&target!=selected)navigator_.rememberRuntimeTarget(id,position,goal,target,now);
    auto state=navigator_.states.find(id);result.status=state==navigator_.states.end()?"pending":state->second.status;
    if(movement)result.progress=navigator_.driverProgressTarget(id,position,target,now);result.target=target;return result;
}
bool NavigationRuntime::recoveryAllowed(std::int64_t id,const Point&position,int grinds)const
{
    if(grid_.inside(position[0],position[2])&&!grid_.height(grid_.cellFor(position)))return true;auto state=navigator_.states.find(id);
    return grinds>0||(state!=navigator_.states.end()&&(state->second.hard_episode||state->second.blocked_tracker));
}
}
