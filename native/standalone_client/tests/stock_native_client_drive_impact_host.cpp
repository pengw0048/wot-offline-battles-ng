#define WOT_CLIENT_IMPACT_HOST_BUILD
#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "stock_native_client_state.cpp"
#include "stock_native_bot_armor_law.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#include "stock_hydraulic_matrix_fixture.hpp"

namespace stock_native_bot_geometry {
using Point=std::array<double,3>;
struct Vehicle {bool ready=true,detached=false,has_siege=false;};
struct Pose {Point position{};double yaw=0.,pitch=0.,roll=0.;std::optional<std::array<double,8>>hydraulic_body;bool require_hydraulic_body=false;};
struct Hit {double distance=0.;const stock_native_bot_armor::Material*material=nullptr;};
struct Frames {BW::Matrix chassis_inverse,body_inverse;std::array<BW::Matrix,4>component_inverse;};
BW::Vector3 native(const Point&p){return {float(p[0]),float(p[1]),float(p[2])};}
BW::Matrix product(BW::Matrix a,const BW::Matrix&b){a.postMultiply(b);return a;}
#include "hydraulic_rebase.inc"
Pose last_pose;unsigned unavailable=0;
Frames frames(const Vehicle&v,const Pose&p){last_pose=p;if(v.has_siege&&p.require_hydraulic_body&&!p.hydraulic_body){++unavailable;throw std::runtime_error("test missing applied body");}return {};}
std::vector<Hit>intersections(const Vehicle&v,const Pose&p,const Point&,const Point&){frames(v,p);if(!v.has_siege)return {};static stock_native_bot_armor::Material material;material.device="engineHealth";return {{.1,&material}};}
}
#include "stock_native_client_drive_impact.cpp"
namespace {
namespace p=stock_native_bot_python;namespace d=stock_native_client_drive;namespace cs=stock_native_client_state;
PyObject*landing(PyObject*,PyObject*args){try{double speed,x,z,slide;int normal;if(!PyArg_ParseTuple(args,"ddddi",&speed,&x,&z,&slide,&normal))return nullptr;d::PublicationLedger ledger;d::PlayerImpact impact(ledger,{1000,10.,200.,32},{});d::PlayerState state;state.air_lateral_x=x;state.air_lateral_z=z;state.slide_speed=slide;const int damage=impact.landing(state,speed,normal!=0,{},{{0.,1.,0.}});return Py_BuildValue("(idddN)",damage,state.slide_speed,state.air_lateral_x,state.air_lateral_z,cs::write(ledger.landing_impacts));}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*world(PyObject*,PyObject*args){try{PyObject*rows;if(!PyArg_ParseTuple(args,"O",&rows))return nullptr;d::PublicationLedger ledger;wot::offline_bot_step::InternalLayout layout;layout.valid=true;d::PlayerImpact impact(ledger,{1000,10.,200.,32},std::move(layout));stock_native_bot_geometry::Vehicle geometry;impact.geometry(geometry,{});auto out=p::list();for(Py_ssize_t i=0;i<p::size(rows);++i){auto row=p::item(rows,i);const auto values=p::numbers<8>(row.get());d::PlayerState state;state.airborne=values[0]!=0.;d::ContactTrace trace;if(values[1])trace.point={{0.,0.,0.}};trace.normal={{values[2],values[3],values[4]}};const double speed=impact.world(state,trace,values[5],values[6],values[7]);p::append(out.get(),p::number(speed));}p::Ref result(PyTuple_New(2));PyTuple_SET_ITEM(result.get(),0,out.release());PyTuple_SET_ITEM(result.get(),1,cs::write(ledger.landing_impacts));return result.release();}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*hydraulic(PyObject*,PyObject*){try{namespace geo=stock_native_bot_geometry;d::PublicationLedger ledger;wot::offline_bot_step::InternalLayout layout;layout.valid=true;d::PlayerImpact impact(ledger,{1000,10.,200.,32},layout);geo::Vehicle vehicle;vehicle.has_siege=true;geo::Pose pose;pose.position={1.,2.,3.};pose.require_hydraulic_body=true;pose.hydraulic_body=std::array<double,8>{1.,2.25,3.5,.4,.5,.6,.7,.8};impact.geometry(vehicle,pose);impact.material_segment({},{0.,1.,0.});if(geo::last_pose.hydraulic_body!=pose.hydraulic_body)throw p::Error("Impact query lost its applied body");
 d::PlayerState state;const int damage=impact.landing(state,20.,true,{{1.,2.,3.}},{0.,1.,0.},d::Point{5.,6.,7.});if(!damage||geo::last_pose.hydraulic_body==pose.hydraulic_body||ledger.landing_impacts.array.size()!=1||ledger.landing_impacts.array[0].kind!=cs::Value::Kind::Object)throw p::Error("Same-step impact proposal lost its held body transform");
 geo::Pose missing=pose;missing.hydraulic_body.reset();impact.geometry(vehicle,missing);impact.landing(state,20.,true,{{1.,2.,3.}},{0.,1.,0.},d::Point{5.,6.,7.});if(!geo::unavailable||ledger.landing_impacts.array.size()!=2||ledger.landing_impacts.array[1].kind!=cs::Value::Kind::Number)throw p::Error("Unknown source body lost its landing terminal observation");impact.geometry(vehicle,pose);
 impact.landing(state,20.,true,{{1.,2.,3.}},{0.,1.,0.},pose.position);if(geo::last_pose.hydraulic_body!=pose.hydraulic_body||ledger.landing_impacts.array.size()!=3||ledger.landing_impacts.array[2].kind!=cs::Value::Kind::Object)throw p::Error("Impact body sample did not restore after a proposal");Py_RETURN_TRUE;
 }catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"hydraulic",hydraulic,METH_NOARGS,""},{"landing",landing,METH_VARARGS,""},{"world",world,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_impact_test(){Py_InitModule("client_impact_test",methods);}
