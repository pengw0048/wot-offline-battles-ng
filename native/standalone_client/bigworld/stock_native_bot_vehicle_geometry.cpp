#include "pch.hpp"
#include "stock_native_bot_vehicle_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace stock_native_bot_geometry {
namespace {
struct Ref {
    PyObject * p;
    explicit Ref(PyObject * value):p(value){if(!p)throw std::runtime_error("native vehicle geometry installation failed");}
    ~Ref(){Py_DECREF(p);}
    Ref(const Ref&)=delete;Ref&operator=(const Ref&)=delete;
};
PyObject * value(PyObject * object,const char * name)
{
    if(PyDict_Check(object)) {
        auto p=PyDict_GetItemString(object,name);if(!p)p=Py_None;Py_INCREF(p);return p;
    }
    auto p=PyObject_GetAttrString(object,name);
    if(!p&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();Py_INCREF(Py_None);return Py_None;}
    return p;
}
double scalar(PyObject * p)
{const double v=PyFloat_AsDouble(p);if(PyErr_Occurred()||!std::isfinite(v))throw std::runtime_error("invalid native vehicle geometry number");return v;}
std::optional<double> optional(PyObject * object,const char * key)
{Ref p(value(object,key));return p.p==Py_None?std::nullopt:std::optional<double>(scalar(p.p));}
bool flag(PyObject * object,const char * key,bool fallback)
{Ref p(value(object,key));if(p.p==Py_None)return fallback;const int result=PyObject_IsTrue(p.p);if(result<0)throw std::runtime_error("invalid native vehicle material flag");return result!=0;}
Point point(PyObject * p)
{Point out;for(unsigned i=0;i<3;++i){Ref v(PySequence_GetItem(p,i));out[i]=scalar(v.p);}return out;}
std::string text(PyObject * p)
{if(p==Py_None)return {};if(!PyString_Check(p))throw std::runtime_error("invalid native vehicle geometry text");return std::string(PyString_AS_STRING(p),PyString_GET_SIZE(p));}
BW::MatrixProviderPtr provider(PyObject * source,const char * name)
{
    Ref object(value(source,name));BW::MatrixProviderPtr result;
    if(object.p!=Py_None&&BW::Script::setData(object.p,result,name)!=0)throw std::runtime_error("native vehicle matrix provider unavailable");
    return result;
}
BW::Vector3 native(const Point& p){return BW::Vector3(float(p[0]),float(p[1]),float(p[2]));}
Point point(const BW::Vector3& p){return {{p.x,p.y,p.z}};}
BW::Matrix product(const BW::Matrix& first,const BW::Matrix& second)
{BW::Matrix out;D3DXMatrixMultiply(&out,&first,&second);return out;}
}
bool readVehicle(PyObject * descriptor,PyObject * entity,bool authority,std::uint64_t identity,Vehicle & output)
{
    try {
        Vehicle v;v.identity=identity;v.authority_geometry=authority;
        const char * names[]={"chassis","hull","turret","gun"};
        for(unsigned i=0;i<4;++i) {
            Ref component(value(descriptor,names[i])),tester(value(component.p,"hitTester"));
            Ref model(value(tester.p,"_ModelHitTester__bspModel"));
            auto & c=v.components[i];c.model=BW::stockBspCollisionModelModel(model.p);c.tree=BW::stockBspCollisionModelTree(model.p);
            if(!c.model||!c.tree)throw std::runtime_error("native vehicle collision BSP is not loaded");
            Ref bounds(value(tester.p,"bbox")),lower(PySequence_GetItem(bounds.p,0)),upper(PySequence_GetItem(bounds.p,1));
            c.minimum=point(lower.p);c.maximum=point(upper.p);c.bounds_available=true;
            Ref name(value(component.p,"itemTypeName"));c.name=text(name.p);
            if(c.name.empty())throw std::runtime_error("native vehicle component has no itemTypeName");
            Ref materials(value(component.p,"materials"));
            if(materials.p!=Py_None) {
                if(!PyDict_Check(materials.p))throw std::runtime_error("native vehicle materials are not a mapping");
                Py_ssize_t cursor=0;PyObject * key=nullptr,*raw=nullptr;
                while(PyDict_Next(materials.p,&cursor,&key,&raw)) {
                    const long material=PyInt_AsLong(key);
                    if(PyErr_Occurred()||material<0)throw std::runtime_error("invalid native vehicle material key");
                    Material m;m.kind=int(material);m.armor=optional(raw,"armor");m.vehicle_damage_factor=optional(raw,"vehicleDamageFactor");
                    m.chance_projectile=optional(raw,"chanceToHitByProjectile");m.chance_explosion=optional(raw,"chanceToHitByExplosion");
                    const auto damage_kind=optional(raw,"damageKind");
                    if(damage_kind){if(*damage_kind!=0.&&*damage_kind!=1.)throw std::runtime_error("invalid native material damage channel");m.damage_kind=int(*damage_kind);}
                    m.use_lining=flag(raw,"useAntifragmentationLining",true);
                    m.use_hit_angle=flag(raw,"useHitAngle",true);m.normalize_caliber=flag(raw,"checkCaliberForHitAngleNorm",true);
                    m.ricochet=flag(raw,"mayRicochet",true);m.ricochet_caliber=flag(raw,"checkCaliberForRichet",flag(raw,"checkCaliberForRicochet",true));
                    m.once=flag(raw,"collideOnceOnly",false);
                    Ref extra(value(raw,"extra"));if(extra.p!=Py_None){Ref device(value(extra.p,"name"));m.device=text(device.p);}
                    c.materials[std::uint32_t(material)]=std::move(m);
                }
            }
        }
        Ref chassis(value(descriptor,"chassis")),hull(value(descriptor,"hull")),turret(value(descriptor,"turret")),gun(value(descriptor,"gun"));
        Ref hull_offset(value(chassis.p,"hullPosition")),positions(value(hull.p,"turretPositions")),gun_offset(value(turret.p,"gunPosition"));
        Ref mount(PySequence_GetItem(positions.p,0));v.hull_offset=point(hull_offset.p);v.turret_offset=point(mount.p);v.gun_offset=point(gun_offset.p);
        v.static_turret_yaw=optional(gun.p,"staticTurretYaw");v.static_gun_pitch=optional(gun.p,"staticPitch");
        Ref siege(value(descriptor,"hasSiegeMode"));const int has_siege=PyObject_IsTrue(siege.p);if(has_siege<0)throw std::runtime_error("native vehicle Siege flag");v.has_siege=has_siege!=0;
        if(entity&&entity!=Py_None) {
            Ref ready(value(entity,"isStarted")),detached(value(entity,"isTurretDetached"));
            const int started=PyObject_IsTrue(ready.p),separated=PyObject_IsTrue(detached.p);
            if(started<0||separated<0)throw std::runtime_error("native vehicle lifecycle flag");
            v.ready=started!=0;v.detached=separated!=0;
            Ref appearance(value(entity,"appearance"));
            v.native_turret=provider(appearance.p,"turretMatrix");v.native_gun=provider(appearance.p,"gunMatrix");
            if(v.has_siege&&authority) {
                Ref filter(value(entity,"filter"));v.native_body=provider(filter.p,"bodyMatrix");v.native_ground=provider(filter.p,"groundPlacingMatrix");
                if(!v.native_body||!v.native_ground)throw std::runtime_error("native hydraulic collision matrices unavailable");
            }
        }
        output=std::move(v);return true;
    } catch(const std::exception& error) {
        if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;
    }
}
Frames frames(const Vehicle& v,const Pose& p)
{
    Frames out;out.chassis.setRotate(float(p.yaw),float(p.pitch),float(p.roll));out.chassis.translation(native(p.position));out.body=out.chassis;
    if(p.native_hydraulics&&v.authority_geometry&&v.has_siege&&(p.siege==2||p.siege==3)&&v.native_body&&v.native_ground) {
        BW::Matrix body,ground,inverse;v.native_body->matrix(body);v.native_ground->matrix(ground);
        if(!inverse.invert(ground))throw std::runtime_error("native hydraulic ground matrix is singular");
        out.body=product(product(body,inverse),out.chassis);
    }
    if(!out.body_inverse.invert(out.body)||!out.chassis_inverse.invert(out.chassis))throw std::runtime_error("native vehicle matrix is singular");
    double turret_yaw=v.static_turret_yaw.value_or(p.turret_yaw),gun_pitch=v.static_gun_pitch.value_or(p.gun_pitch);
    // The fixed worker reads the native component providers at the query
    // boundary, independently of its unblended canonical ground placement.
    if(p.native_components&&v.native_turret){BW::Matrix sampled;v.native_turret->matrix(sampled);turret_yaw=sampled.yaw();}
    if(p.native_components&&v.native_gun){BW::Matrix sampled;v.native_gun->matrix(sampled);gun_pitch=sampled.pitch();}
    BW::Matrix hull,turret,gun,rotation;
    hull.setTranslate(-native(v.hull_offset));
    turret.setTranslate(-native(v.hull_offset)-native(v.turret_offset));rotation.setRotateY(-float(turret_yaw));turret.postMultiply(rotation);
    gun.setTranslate(-native(v.gun_offset));rotation.setRotateX(-float(gun_pitch));gun.postMultiply(rotation);gun.preMultiply(turret);
    out.component_inverse[0]=BW::Matrix::identity;out.component_inverse[1]=hull;
    out.component_inverse[2]=turret;out.component_inverse[3]=gun;
    return out;
}
std::vector<Hit> intersections(const Vehicle& v,const Pose& p,const Point& start,const Point& end)
{
    std::vector<Hit> result;if(!v.ready)return result;const auto f=frames(v,p);
    for(unsigned i=0;i<4;++i) {
        const auto& c=v.components[i];if((i>=2&&v.detached)||!c.tree)continue;
        const auto& root_inverse=i==0?f.chassis_inverse:f.body_inverse;
        const auto& root=i==0?f.chassis:f.body;
        const auto local_start=f.component_inverse[i].applyPoint(root_inverse.applyPoint(native(start)));
        const auto local_end=f.component_inverse[i].applyPoint(root_inverse.applyPoint(native(end)));
        auto direction=local_end-local_start;direction.normalise();
        BW::Matrix component;if(!component.invert(f.component_inverse[i]))continue;
        for(const auto& raw:BW::stockBspSegment(*c.tree,local_start,local_end)) {
            Hit h;h.distance=raw.distance;h.angle_cosine=raw.angleCosine;h.component=i;h.material_kind=raw.material;
            const auto local_point=local_start+direction*raw.distance;h.local_point=point(local_point);
            h.world_point=point(root.applyPoint(component.applyPoint(local_point)));
            auto normal=root.applyVector(component.applyVector(BW::Vector3(raw.normal.x,raw.normal.y,raw.normal.z)));normal.normalise();h.world_normal=point(normal);
            const auto material=c.materials.find(raw.material);if(material!=c.materials.end())h.material=&material->second;
            result.push_back(h);
        }
    }
    std::stable_sort(result.begin(),result.end(),[](const Hit& a,const Hit& b){return a.distance<b.distance;});return result;
}
std::optional<std::pair<Point,Point>> worldBounds(const Vehicle& v,const Pose& p,int component)
{
    if(!v.ready)return {};const auto f=frames(v,p);std::optional<std::pair<Point,Point>> result;
    for(unsigned i=0;i<4;++i) {
        if((component>=0&&int(i)!=component)||(i>=2&&v.detached))continue;
        BW::Matrix component_to_root;if(!component_to_root.invert(f.component_inverse[i]))return {};
        const auto to_world=product(component_to_root,i==0?f.chassis:f.body);
        const auto& c=v.components[i];
        for(unsigned corner=0;corner<8;++corner) {
            const BW::Vector3 local(float((corner&1)?c.maximum[0]:c.minimum[0]),float((corner&2)?c.maximum[1]:c.minimum[1]),float((corner&4)?c.maximum[2]:c.minimum[2]));
            const auto q=point(to_world.applyPoint(local));
            if(!result)result=std::make_pair(q,q);
            else for(unsigned axis=0;axis<3;++axis){result->first[axis]=std::min(result->first[axis],q[axis]);result->second[axis]=std::max(result->second[axis],q[axis]);}
        }
    }
    return result;
}
void Vehicles::install(Key key,Vehicle value){actors_[key]=std::move(value);}
void Vehicles::detached(Key key,bool value){const auto it=actors_.find(key);if(it!=actors_.end())it->second.detached=value;}
const Vehicle * Vehicles::find(Key key)const{const auto it=actors_.find(key);return it==actors_.end()?nullptr:&it->second;}
std::vector<Hit> Vehicles::intersections(Key key,const Pose& pose,const Point& start,const Point& end)const
{const auto actor=find(key);return actor?stock_native_bot_geometry::intersections(*actor,pose,start,end):std::vector<Hit>{};}
}
