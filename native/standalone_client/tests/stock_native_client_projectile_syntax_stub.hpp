#pragma once
#include <Python.h>
#include "stock_native_projectile_damage_python.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#include "stock_native_world_pipeline.hpp"
#include <memory>
#include <functional>
#include <cmath>
#define CRITICAL_MSG(...) ((void)0)
namespace BW {
struct Vector3 {float x=0,y=0,z=0;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}Vector3 operator+(const Vector3&o)const{return {x+o.x,y+o.y,z+o.z};}Vector3 operator-(const Vector3&o)const{return {x-o.x,y-o.y,z-o.z};}Vector3 operator*(float s)const{return {x*s,y*s,z*s};}float length()const{return std::sqrt(x*x+y*y+z*z);}void normalise(){const auto n=length();if(n){x/=n;y/=n;z/=n;}}};
struct Matrix {Vector3 offset;void setRotate(float,float,float){}void setRotateY(float){}void setRotateX(float){}void translation(Vector3 value){offset=value;}void postMultiply(const Matrix&before){offset=offset+before.offset;}Vector3 applyPoint(Vector3 v)const{return v+offset;}Vector3 applyVector(Vector3 v)const{return v;}};
struct MatrixProvider {void matrix(Matrix&)const{}};using MatrixProviderPtr=std::shared_ptr<MatrixProvider>;
struct BSPTree{};
namespace Script {struct InitTimeJob {explicit InitTimeJob(int){}virtual ~InitTimeJob()=default;virtual void init()=0;};inline int setData(PyObject*,MatrixProviderPtr&v,const char*){v=std::make_shared<MatrixProvider>();return 0;}inline int setData(PyObject*object,Vector3&out,const char*){float*fields[]={&out.x,&out.y,&out.z};const char*names[]={"x","y","z"};for(unsigned i=0;i<3;++i){PyObject*value=(PyTuple_Check(object)||PyList_Check(object))?PySequence_GetItem(object,i):PyObject_GetAttrString(object,names[i]);if(!value)return -1;const double number=PyFloat_AsDouble(value);Py_DECREF(value);if(PyErr_Occurred())return -1;if(!std::isfinite(number)){PyErr_SetString(PyExc_ValueError,"fixture native Vector3 is nonfinite");return -1;}*fields[i]=float(number);}return 0;}}
}
namespace stock_native_world {class NativeScene:public Scene {public:bool current()const{return true;}};}
namespace stock_native_bot_geometry {
using Point=std::array<double,3>;using Material=stock_native_bot_armor::Material;
struct Component {const BW::BSPTree*tree=nullptr;std::map<std::uint32_t,Material>materials;Point minimum{},maximum{};bool bounds_available=false;};
struct Vehicle {std::array<Component,4>components;Point hull_offset{},turret_offset{},gun_offset{};bool ready=true,detached=false;BW::MatrixProviderPtr native_body,native_ground;};
struct Pose {Point position{};double yaw=0,pitch=0,roll=0,turret_yaw=0,gun_pitch=0;int siege=0;bool native_components=true,native_hydraulics=true;};
struct Hit {double distance=0,angle_cosine=1;unsigned component=1;std::uint32_t material_kind=1;Point local_point{},world_point{},world_normal{};};
bool readVehicle(PyObject*,PyObject*,bool,std::uint64_t,Vehicle&);
std::vector<Hit>intersections(const Vehicle&,const Pose&,const Point&,const Point&);
}
namespace stock_native_projectile_damage {
class GeometryWorld:public DamageWorld {
public:
 GeometryWorld(const stock_native_bot_geometry::Vehicle&,const wot::offline_bot_step::InternalLayout&,const stock_native_bot_geometry::Pose&,std::function<bool(const Point&,const Point&,const Point&)>,std::array<std::optional<unsigned>,2>,std::optional<bool> = {});
 std::vector<BlastComponent>components(const Point&,double)override;
 Point toWorld(unsigned,const Point&)override;
 std::vector<ImpactLayer>vehicleRay(const Point&,const Point&)override;
 bool visible(const Point&,const Point&,const Point&)override;
 std::vector<CriticalLayer>internal(const Target&,const Point&,const Point&,bool,const std::vector<ImpactLayer>&)override;
 std::optional<std::uint64_t>sticker(const Target&,const Point&,const Point&,const std::vector<ImpactLayer>&,const std::optional<ArmorContact>&,int)override;
};
}

namespace stock_native_client_drive {struct Controls {double aim_yaw=0.,gun_pitch=0.;};class PlayerDriver {public:Controls&controls();};}
