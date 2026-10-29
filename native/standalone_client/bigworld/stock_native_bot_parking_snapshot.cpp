#include "stock_native_bot_parking_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace stock_native_bot_parking {
namespace p=stock_native_bot_python;
namespace {
using Bounds=std::array<double,4>;
Py_ssize_t size(PyObject*value){return value==Py_None?0:p::size(value);}
Bounds cellBounds(const std::string& cell,const Bounds& bounds){
    const std::string rows="ABCDEFGHJK",columns="1234567890";
    if(cell.size()!=2||rows.find(cell[0])==std::string::npos||columns.find(cell[1])==std::string::npos||
       !(bounds[0]<bounds[2]&&bounds[1]<bounds[3]))throw p::Error("Native parking catalog cell is invalid");
    const auto row=rows.find(cell[0]),column=columns.find(cell[1]);
    const double dx=(bounds[2]-bounds[0])/10.,dz=(bounds[3]-bounds[1])/10.;
    return {bounds[0]+column*dx,bounds[3]-(row+1)*dz,bounds[0]+(column+1)*dx,bounds[3]-row*dz};
}
double coordinate(PyObject*value){
    if(PyString_Check(value)||PyUnicode_Check(value)){p::Ref number(PyFloat_FromString(value,nullptr));return p::scalar(number.get());}
    if(PyBool_Check(value))throw p::Error("Native parking coordinate is a boolean");
    return p::scalar(value);
}
std::string sideFor(int team,PyObject*bases){
    if((team!=1&&team!=2)||size(bases)!=2)return {};
    auto own=p::item(bases,team-1),enemy=p::item(bases,2-team);
    auto ox=p::item(own.get(),0),oz=p::item(own.get(),1),ex=p::item(enemy.get(),0),ez=p::item(enemy.get(),1);
    const double dx=coordinate(ox.get())-coordinate(ex.get()),dz=coordinate(oz.get())-coordinate(ez.get());
    if(std::abs(dx)+std::abs(dz)<1.)return {};
    return std::abs(dz)>=std::abs(dx)?(dz>0.?"north":"south"):(dx>0.?"east":"west");
}
bool support(PyObject*zone,int team,PyObject*states,const Bounds&bounds){
    auto rule=p::value(zone,"requires_support");if(rule.get()==Py_None||!PyDict_Size(rule.get()))return true;
    auto columns=p::value(rule.get(),"columns");std::vector<Bounds> regions;
    for(Py_ssize_t i=0;i<size(columns.get());++i){auto column=p::item(columns.get(),i);regions.push_back(cellBounds("A"+p::string(column.get()),bounds));}
    int count=0;Py_ssize_t cursor=0;PyObject*key=nullptr,*state=nullptr;
    while(PyDict_Next(states,&cursor,&key,&state)){
        if(p::integral(state,"team")!=team)continue;
        auto profile=p::value(state,"profile"),tag=p::value(profile.get(),"class_tag");
        if(tag.get()!=Py_None&&p::string(tag.get())=="SPG")continue;
        auto route=p::value(state,"route"),points=p::value(route.get(),"waypoints");bool matched=false;
        for(Py_ssize_t i=1;i<size(points.get())&&!matched;++i){
            try{auto row=p::item(points.get(),i);auto x=PyDict_Check(row.get())?p::value(row.get(),"x"):p::item(row.get(),0);const auto value=coordinate(x.get());
                for(const auto&region:regions)if(value>=region[0]&&value<=region[2]){matched=true;break;}}
            catch(const std::exception&){PyErr_Clear();}
        }
        count+=matched;
    }
    return count>=p::integral(rule.get(),"minimum");
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
Plan basePlan(PyObject*state,const std::string&name,double clearance){
    Plan out;out.map=name;auto vehicle=p::value(state,"vehicle");out.vehicle=p::string(vehicle.get());out.clearance=clearance;return out;
}
}
bool readCatalog(PyObject*runtime,const offline_navigation::MapInput&input,
    const std::array<std::array<int,2>,8>&directions,std::map<std::int64_t,Catalog>&output){try{
    auto graph=p::value(runtime,"baked_graph"),map_name=p::value(graph.get(),"map"),profile=p::value(runtime,"_bot_tactics"),states=p::value(runtime,"states");
    const auto name=p::string(map_name.get());auto maps=p::value(profile.get(),"maps"),settings=p::value(maps.get(),name.c_str()),deleted_rows=p::value(settings.get(),"deleted_positions");
    std::set<std::string> deleted;for(Py_ssize_t i=0;i<size(deleted_rows.get());++i){auto row=p::item(deleted_rows.get(),i);deleted.insert(p::string(row.get()));}
    p::Ref config(PyImport_ImportModule("gui.mods.offline_lan_0922.bot_tactics")),data_module(PyImport_ImportModule("gui.mods.offline_lan_0922.spg_position_data"));
    auto data=p::value(data_module.get(),"CATALOG"),entries=p::value(data.get(),"maps"),entry=p::value(entries.get(),name.c_str());
    auto config_maps=p::value(config.get(),"MAPS"),config_entry=p::value(config_maps.get(),name.c_str()),bases=p::value(graph.get(),"bases");
    std::map<std::int64_t,Catalog> result;Py_ssize_t cursor=0;PyObject*key=nullptr,*state=nullptr;
    while(PyDict_Next(states.get(),&cursor,&key,&state)){
        auto initial=p::value(state,"_spg_initial");if(initial.get()==Py_None)continue;
        auto source=p::value(initial.get(),"source");const bool manual=p::string(source.get())=="launcher_manual_v1";const auto team=int(p::integral(state,"team"));
        auto shape=p::value(state,"collision_shape"),width=p::item(shape.get(),0),length=p::item(shape.get(),1);
        const double clearance=std::hypot(p::scalar(width.get()),p::scalar(length.get()))+(manual?2.:2.5);
        auto bounds_value=p::value(manual?config_entry.get():entry.get(),"bounds");const auto bounds=p::numbers<4>(bounds_value.get());
        auto map=input;map.has_bounds=true;map.bounds=bounds;Parking parking(map,directions);Catalog catalog;catalog.manual=manual;
        if(!manual){auto graph_bounds=p::value(graph.get(),"bounds");if(p::numbers<4>(graph_bounds.get())!=bounds)throw p::Error("Native parking catalog and graph bounds differ");}
        auto zones=p::value(manual?settings.get():entry.get(),manual?"positions":"zones");
        const auto side=manual?"team"+std::to_string(team):sideFor(team,bases.get());
        bool side_valid=true;if(!manual){auto sides=p::value(entry.get(),"spawn_sides"),published=p::value(sides.get(),std::to_string(team).c_str());side_valid=!side.empty()&&published.get()!=Py_None&&p::string(published.get())==side;}
        for(Py_ssize_t z=0;z<size(zones.get());++z){auto zone=p::item(zones.get(),z),id=p::value(zone.get(),"id");const auto zone_name=p::string(id.get());
            if(manual){
                if(deleted.count(zone_name)||p::integral(zone.get(),"team")!=team)continue;
                auto point_value=p::value(zone.get(),"point");const auto centre=p::numbers<2>(point_value.get());const double radius=p::numeric(zone.get(),"radius");
                catalog.areas[zone_name]={true,{centre[0],centre[1],0.,0.},radius};
                const auto rows=candidates(map,parking,{centre[0]-radius+2.,centre[1]-radius+2.,centre[0]+radius-2.,centre[1]+radius-2.},centre,true,radius-2.,clearance);
                const double angle=p::numeric(zone.get(),"heading")*(3.14159265358979323846/180.);
                for(const auto&row:rows){Candidate candidate;candidate.index=row.second;candidate.priority=int(p::integral(zone.get(),"priority"));candidate.centre_distance=row.first;
                    auto&plan=candidate.plan;plan=basePlan(state,name,clearance);auto identity=p::value(initial.get(),"catalog");plan.catalog=p::string(identity.get());plan.zone=zone_name;plan.cell="manual";plan.source="launcher_manual_v1";plan.side=side;plan.point=parking.point(row.second);
                    plan.face={std::clamp(plan.point[0]+std::sin(angle)*100.,bounds[0],bounds[2]),plan.point[1],std::clamp(plan.point[2]+std::cos(angle)*100.,bounds[1],bounds[3])};catalog.candidates.push_back(std::move(candidate));}
            }else{
                auto cells=p::value(zone.get(),"cells"),zone_side=p::value(zone.get(),"side");
                for(Py_ssize_t c=0;c<size(cells.get());++c){auto cell_value=p::item(cells.get(),c);const auto cell=p::string(cell_value.get());const auto box=cellBounds(cell,bounds);catalog.areas[cell]={false,box,0.};
                    if(!side_valid||p::string(zone_side.get())!=side||deleted.count("spg_"+std::to_string(team)+"_"+zone_name)||!support(zone.get(),team,states.get(),bounds))continue;
                    const auto rows=candidates(map,parking,{box[0]+clearance,box[1]+clearance,box[2]-clearance,box[3]-clearance},{(box[0]+box[2])*.5,(box[1]+box[3])*.5},false,0.,clearance);
                    std::vector<Point> selected;
                    for(const auto&row:rows){const auto point=parking.point(row.second);if(std::any_of(selected.begin(),selected.end(),[&](const Point&old){return std::hypot(point[0]-old[0],point[2]-old[2])<clearance*2.+6.;})||!parking.clear(row.second,clearance))continue;
                        selected.push_back(point);Candidate candidate;candidate.index=row.second;candidate.priority=int(p::integral(zone.get(),"priority"));auto&plan=candidate.plan;plan=basePlan(state,name,clearance);
                        auto revision=p::value(data.get(),"revision"),origin=p::value(zone.get(),"source"),face_cell=p::value(zone.get(),"face_cell");plan.catalog=p::string(revision.get());plan.zone=zone_name;plan.cell=cell;plan.source=p::string(origin.get());plan.side=side;plan.point=point;
                        auto enemy=p::item(bases.get(),2-team);const auto enemy_point=p::numbers<2>(enemy.get());plan.face={enemy_point[0],point[1],enemy_point[1]};
                        if(face_cell.get()!=Py_None&&!p::string(face_cell.get()).empty()){const auto face=cellBounds(p::string(face_cell.get()),bounds);plan.face={(face[0]+face[2])*.5,point[1],(face[1]+face[3])*.5};}
                        catalog.candidates.push_back(std::move(candidate));if(selected.size()>=8)break;
                    }
                }
            }
        }
        result.emplace(p::integral(state,"id"),std::move(catalog));
    }
    output=std::move(result);return true;
}catch(const std::exception&error){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;}}
}
