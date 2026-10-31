#include "stock_native_bot_parking_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace stock_native_bot_parking {
namespace p=stock_native_bot_python;
namespace {
using Bounds=std::array<double,4>;
Bounds cellBounds(const std::string& cell,const Bounds& bounds){
    const std::string rows="ABCDEFGHJK",columns="1234567890";
    if(cell.size()!=2||rows.find(cell[0])==std::string::npos||columns.find(cell[1])==std::string::npos||
       !(bounds[0]<bounds[2]&&bounds[1]<bounds[3]))throw p::Error("Native parking catalog cell is invalid");
    const auto row=rows.find(cell[0]),column=columns.find(cell[1]);
    const double dx=(bounds[2]-bounds[0])/10.,dz=(bounds[3]-bounds[1])/10.;
    return {bounds[0]+column*dx,bounds[3]-(row+1)*dz,bounds[0]+(column+1)*dx,bounds[3]-row*dz};
}
using Value=stock_native_client_state::Value;
using Kind=Value::Kind;
const Value& at(const Value&v,const char*name){return v.resolved().get(name);}
const std::vector<Value>& rows(const Value&v){static const std::vector<Value> none;return v.resolved().kind==Kind::Array?v.resolved().array:none;}
double coordinate(const Value&value){
    const auto&v=value.resolved();
    if(v.kind==Kind::Text){std::size_t end=0;const double n=std::stod(v.text,&end);if(end!=v.text.size()||!std::isfinite(n))throw p::Error("Native parking coordinate is invalid");return n;}
    if(v.kind!=Kind::Integer&&v.kind!=Kind::Number)throw p::Error("Native parking coordinate is invalid");
    const double n=v.numeric(NAN);if(!std::isfinite(n))throw p::Error("Native parking coordinate is invalid");return n;
}
std::string sideFor(int team,const Value&bases){
    if((team!=1&&team!=2)||rows(bases).size()!=2)return {};
    const auto&own=rows(rows(bases)[std::size_t(team-1)]),&enemy=rows(rows(bases)[std::size_t(2-team)]);
    if(own.size()<2||enemy.size()<2)return {};
    const double dx=coordinate(own[0])-coordinate(enemy[0]),dz=coordinate(own[1])-coordinate(enemy[1]);
    if(std::abs(dx)+std::abs(dz)<1.)return {};
    return std::abs(dz)>=std::abs(dx)?(dz>0.?"north":"south"):(dx>0.?"east":"west");
}
bool support(const Value&zone,int team,const stock_native_bot::RoundSeed&seed,const Bounds&bounds){
    const auto&rule=at(zone,"requires_support");if(rule.resolved().kind!=Kind::Object||rule.resolved().object.empty())return true;
    std::vector<Bounds> regions;
    for(const auto&column:rows(at(rule,"columns")))regions.push_back(cellBounds("A"+column.resolved().text,bounds));
    int count=0;
    for(const auto&bot:seed.bots){
        const auto&state=bot.second.state;
        if(at(state,"team").integer!=team)continue;
        if(at(at(state,"profile"),"class_tag").resolved().text=="SPG")continue;
        const auto&points=rows(at(at(state,"route"),"waypoints"));bool matched=false;
        for(std::size_t i=1;i<points.size()&&!matched;++i){
            try{const auto&row=points[i].resolved();const double value=coordinate(row.kind==Kind::Object?row.get("x"):rows(row).at(0));
                for(const auto&region:regions)if(value>=region[0]&&value<=region[2]){matched=true;break;}}
            catch(const std::exception&){}
        }
        count+=matched;
    }
    return count>=at(rule,"minimum").numeric();
}
std::array<int,4> span(const offline_navigation::MapInput&map,const Bounds&bounds){
    return {std::max(0,int(std::ceil((bounds[0]-map.origin_x)/map.cell_size))),
        std::min(map.width-1,int(std::floor((bounds[2]-map.origin_x)/map.cell_size))),
        std::max(0,int(std::ceil((bounds[1]-map.origin_z)/map.cell_size))),
        std::min(map.height-1,int(std::floor((bounds[3]-map.origin_z)/map.cell_size)))};
}
std::vector<std::pair<double,int>> candidates(const offline_navigation::MapInput&map,
    const Parking&parking,const Bounds&bounds,const std::array<double,2>&centre,bool circle,double radius,double clearance){
    const auto range=span(map,bounds);std::vector<std::pair<double,int>> out;
    for(int row=range[2];row<=range[3];++row)for(int col=range[0];col<=range[1];++col){
        const int index=row*map.width+col;if(!parking.usable(index))continue;
        const auto point=parking.point(index);const auto dx=point[0]-centre[0],dz=point[2]-centre[1];
        const double score=circle?std::hypot(dx,dz):dx*dx+dz*dz;
        if(circle&&(score>radius||!parking.clear(index,clearance)))continue;
        out.emplace_back(score,index);
    }
    std::sort(out.begin(),out.end());return out;
}
Plan basePlan(const Value&state,const std::string&name,double clearance){
    Plan out;out.map=name;out.vehicle=at(state,"vehicle").resolved().text;out.clearance=clearance;return out;
}
}
bool readCatalog(const stock_native_bot::RoundSeed&seed,const offline_navigation::MapInput&input,
    const std::array<std::array<int,2>,8>&directions,std::map<std::int64_t,Catalog>&output){try{
    PyObject*graph=seed.graph.get();const Value bases=stock_native_client_state::read(p::value(graph,"bases").get());
    const Value graph_bounds=stock_native_client_state::read(p::value(graph,"bounds").get());
    p::Ref data_module(PyImport_ImportModule("gui.mods.offline_lan_0922.spg_position_data"));
    const Value data=stock_native_client_state::read(p::value(data_module.get(),"CATALOG").get());const Value&entry=at(at(data,"maps"),seed.map.c_str());
    std::map<std::int64_t,Catalog> result;
    for(const auto&bot:seed.bots){
        const auto&state=bot.second.state;const auto&initial=at(state,"_spg_initial");if(initial.resolved().kind==Kind::Null)continue;
        // The native round start publishes catalog plans only; launcher manual plans need a tactics profile.
        if(at(initial,"source").resolved().text=="launcher_manual_v1")throw p::Error("Native parking manual plans need a Bot tactics profile");
        const auto team=int(at(state,"team").integer);
        const auto&shape=rows(at(state,"collision_shape"));if(shape.size()<2)throw p::Error("Native parking collision shape is invalid");
        const double clearance=std::hypot(shape[0].numeric(),shape[1].numeric())+2.5;
        const auto&raw_bounds=rows(at(entry,"bounds"));if(raw_bounds.size()!=4)throw p::Error("Native parking catalog bounds are invalid");
        const Bounds bounds{raw_bounds[0].numeric(),raw_bounds[1].numeric(),raw_bounds[2].numeric(),raw_bounds[3].numeric()};
        auto map=input;map.has_bounds=true;map.bounds=bounds;Parking parking(map,directions);Catalog catalog;catalog.manual=false;
        const auto&published_bounds=rows(graph_bounds);
        if(published_bounds.size()!=4||Bounds{published_bounds[0].numeric(),published_bounds[1].numeric(),published_bounds[2].numeric(),published_bounds[3].numeric()}!=bounds)
            throw p::Error("Native parking catalog and graph bounds differ");
        const auto side=sideFor(team,bases);
        const auto&published=at(at(entry,"spawn_sides"),std::to_string(team).c_str());
        const bool side_valid=!side.empty()&&published.resolved().kind==Kind::Text&&published.resolved().text==side;
        for(const auto&zone:rows(at(entry,"zones"))){const auto zone_name=at(zone,"id").resolved().text;
            for(const auto&cell_value:rows(at(zone,"cells"))){const auto cell=cell_value.resolved().text;const auto box=cellBounds(cell,bounds);catalog.areas[cell]={false,box,0.};
                if(!side_valid||at(zone,"side").resolved().text!=side||!support(zone,team,seed,bounds))continue;
                const auto candidates_=candidates(map,parking,{box[0]+clearance,box[1]+clearance,box[2]-clearance,box[3]-clearance},{(box[0]+box[2])*.5,(box[1]+box[3])*.5},false,0.,clearance);
                std::vector<Point> selected;
                for(const auto&row:candidates_){const auto point=parking.point(row.second);if(std::any_of(selected.begin(),selected.end(),[&](const Point&old){return std::hypot(point[0]-old[0],point[2]-old[2])<clearance*2.+6.;})||!parking.clear(row.second,clearance))continue;
                    selected.push_back(point);Candidate candidate;candidate.index=row.second;candidate.priority=int(at(zone,"priority").numeric());auto&plan=candidate.plan;plan=basePlan(state,seed.map,clearance);
                    plan.catalog=at(data,"revision").resolved().text;plan.zone=zone_name;plan.cell=cell;plan.source=at(zone,"source").resolved().text;plan.side=side;plan.point=point;
                    const auto&enemy=rows(rows(bases).at(std::size_t(2-team)));plan.face={coordinate(enemy.at(0)),point[1],coordinate(enemy.at(1))};
                    const auto&face_cell=at(zone,"face_cell");if(face_cell.resolved().kind==Kind::Text&&!face_cell.resolved().text.empty()){const auto face=cellBounds(face_cell.resolved().text,bounds);plan.face={(face[0]+face[2])*.5,point[1],(face[1]+face[3])*.5};}
                    catalog.candidates.push_back(std::move(candidate));if(selected.size()>=8)break;
                }
            }
        }
        result.emplace(bot.first,std::move(catalog));
    }
    output=std::move(result);return true;
}catch(const std::exception&error){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;}}
}
