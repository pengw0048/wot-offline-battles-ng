#include "stock_native_bot_parking_snapshot.hpp"
#include "stock_native_bot_python.hpp"
#include <limits>
namespace p=stock_native_bot_python;
namespace {
PyObject* freeze(PyObject*,PyObject*runtime){try{
    auto graph=p::value(runtime,"baked_graph"),origin=p::value(graph.get(),"origin"),bounds=p::value(graph.get(),"bounds"),heights=p::value(graph.get(),"heights_mm"),links=p::value(graph.get(),"links"),hazards=p::value(graph.get(),"hazards"),directions=p::value(graph.get(),"directions");
    offline_navigation::MapInput map;map.width=int(p::integral(graph.get(),"width"));map.height=int(p::integral(graph.get(),"height"));map.cell_size=p::numeric(graph.get(),"cell_size");const auto zero=p::numbers<2>(origin.get());map.origin_x=zero[0];map.origin_z=zero[1];map.has_bounds=true;map.bounds=p::numbers<4>(bounds.get());
    for(Py_ssize_t i=0;i<p::size(heights.get());++i){auto h=p::item(heights.get(),i),l=p::item(links.get(),i),z=p::item(hazards.get(),i);map.heights.push_back(h.get()==Py_None?std::numeric_limits<double>::quiet_NaN():p::scalar(h.get())/1000.);map.links.push_back(unsigned(p::wide(l.get())));map.hazards.push_back(unsigned(p::wide(z.get())));}
    std::array<std::array<int,2>,8> edges{};for(unsigned i=0;i<8;++i){auto row=p::item(directions.get(),i);const auto d=p::numbers<2>(row.get());edges[i]={int(d[0]),int(d[1])};}
    std::map<std::int64_t,stock_native_bot_parking::Catalog> catalogs;if(!stock_native_bot_parking::readCatalog(runtime,map,edges,catalogs))return nullptr;
    auto out=p::dict();for(const auto&entry:catalogs){auto catalog=p::dict(),areas=p::dict(),candidates=p::list();p::set(catalog.get(),"manual",p::boolean(entry.second.manual));
        for(const auto&area:entry.second.areas){auto row=p::dict();p::set(row.get(),"circle",p::boolean(area.second.circle));p::set(row.get(),"bounds",p::tupleNumbers(area.second.bounds));p::set(row.get(),"radius",p::number(area.second.radius));p::set(areas.get(),area.first.c_str(),row);}
        for(const auto&candidate:entry.second.candidates){const auto&plan=candidate.plan;auto row=p::dict();for(const auto&pair:std::initializer_list<std::pair<const char*,std::string>>{{"catalog",plan.catalog},{"map",plan.map},{"zone",plan.zone},{"cell",plan.cell},{"source",plan.source},{"vehicle",plan.vehicle},{"side",plan.side}})p::set(row.get(),pair.first,p::text(pair.second));p::set(row.get(),"point",p::tupleNumbers(plan.point));p::set(row.get(),"face",p::tupleNumbers(plan.face));p::set(row.get(),"radius",p::number(plan.radius));p::set(row.get(),"clearance",p::number(plan.clearance));auto index=p::integer(candidate.index),priority=p::integer(candidate.priority),distance=p::number(candidate.centre_distance);p::Ref value(PyTuple_Pack(4,row.get(),index.get(),priority.get(),distance.get()));p::append(candidates.get(),value);}
        p::set(catalog.get(),"areas",areas);p::set(catalog.get(),"candidates",candidates);auto id=p::integer(entry.first);if(PyDict_SetItem(out.get(),id.get(),catalog.get())<0)throw p::Error("fixture mapping failed");}
    return out.release();
}catch(const std::exception&error){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return nullptr;}}
PyMethodDef methods[]={{"freeze",freeze,METH_O,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initparking_snapshot_test(){Py_InitModule("parking_snapshot_test",methods);}
