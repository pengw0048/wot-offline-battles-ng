#define WOT_CLIENT_IMPACT_HOST_BUILD
#define WOT_CLIENT_REPLICA_HOST_BUILD
#include "stock_native_client_state.cpp"
#include "stock_native_bot_armor_law.hpp"
#include "stock_native_bot_internal_geometry.hpp"
namespace BW {
struct Vector3 {float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}Vector3 operator-(const Vector3&o)const{return {x-o.x,y-o.y,z-o.z};}float length()const{return std::sqrt(x*x+y*y+z*z);}};
struct Matrix {Vector3 applyPoint(const Vector3&v)const{return v;}};
}
namespace stock_native_bot_geometry {
using Point=std::array<double,3>;
struct Vehicle {bool ready=true,detached=false;};
struct Pose {Point position{};};
struct Hit {double distance=0.;const stock_native_bot_armor::Material*material=nullptr;};
struct Frames {BW::Matrix chassis_inverse,body_inverse;std::array<BW::Matrix,4>component_inverse;};
Frames frames(const Vehicle&,const Pose&){return {};}
std::vector<Hit>intersections(const Vehicle&,const Pose&,const Point&,const Point&){return {};}
}
#include "stock_native_client_drive_impact.cpp"
namespace {
namespace p=stock_native_bot_python;namespace d=stock_native_client_drive;namespace cs=stock_native_client_state;
PyObject*landing(PyObject*,PyObject*args){try{double speed,x,z,slide;int normal;if(!PyArg_ParseTuple(args,"ddddi",&speed,&x,&z,&slide,&normal))return nullptr;d::PublicationLedger ledger;d::PlayerImpact impact(ledger,{1000,10.,200.,32},{});d::PlayerState state;state.air_lateral_x=x;state.air_lateral_z=z;state.slide_speed=slide;const int damage=impact.landing(state,speed,normal!=0,{},{{0.,1.,0.}});return Py_BuildValue("(idddN)",damage,state.slide_speed,state.air_lateral_x,state.air_lateral_z,cs::write(ledger.landing_impacts));}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyObject*world(PyObject*,PyObject*args){try{PyObject*rows;if(!PyArg_ParseTuple(args,"O",&rows))return nullptr;d::PublicationLedger ledger;wot::offline_bot_step::InternalLayout layout;layout.valid=true;d::PlayerImpact impact(ledger,{1000,10.,200.,32},std::move(layout));stock_native_bot_geometry::Vehicle geometry;impact.geometry(geometry,{});auto out=p::list();for(Py_ssize_t i=0;i<p::size(rows);++i){auto row=p::item(rows,i);const auto values=p::numbers<8>(row.get());d::PlayerState state;state.airborne=values[0]!=0.;d::ContactTrace trace;if(values[1])trace.point={{0.,0.,0.}};trace.normal={{values[2],values[3],values[4]}};const double speed=impact.world(state,trace,values[5],values[6],values[7]);p::append(out.get(),p::number(speed));}p::Ref result(PyTuple_New(2));PyTuple_SET_ITEM(result.get(),0,out.release());PyTuple_SET_ITEM(result.get(),1,cs::write(ledger.landing_impacts));return result.release();}catch(const std::exception&e){PyErr_SetString(PyExc_RuntimeError,e.what());return nullptr;}}
PyMethodDef methods[]={{"landing",landing,METH_VARARGS,""},{"world",world,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_impact_test(){Py_InitModule("client_impact_test",methods);}
