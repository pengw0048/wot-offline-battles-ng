#ifndef WOT_STOCK_TRAJECTORY_PORTABLE
#include "pch.hpp"
#include "stock_trajectory_drawer.hpp"
#include "stock_battle_scene_queries.hpp"
#include "moo/device_callback.hpp"
#include "moo/effect_material.hpp"
#include "moo/moo_utilities.hpp"
#include "moo/render_context.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#endif
#include "wot/stock_trajectory_drawer.hpp"
#include <memory>
#include <cmath>
#include <stdexcept>

BW_BEGIN_NAMESPACE
int StockTrajectoryDrawer_token=1;
namespace {
namespace trajectory=wot::stock_trajectory;
wot::Vector3 portable(const Vector3& v) {return {v.x,v.y,v.z};}
Vector3 native(wot::Vector3 v) {return Vector3(v.x,v.y,v.z);}
std::array<float,4> rgba(const Vector4& v) {return {{v.x,v.y,v.z,v.w}};}

struct AbandonedTrajectoryUpdate {};

class TrajectoryDrawer : public Moo::DeviceCallback {
public:
    trajectory::State state;
    PyObjectPtr callback;
    Moo::EffectMaterialPtr material;
    bool closed=false;
    std::uint64_t epoch=0;
    ~TrajectoryDrawer() {close();}
    void close() {++epoch;closed=true;state.visible=false;callback=NULL;material=NULL;}
    void createManagedObjects() override {
        if (!closed && !Moo::createEffect(material,"system/materials/geometrics.fx"))
            ERROR_MSG("TrajectoryDrawer: original geometrics effect could not be loaded\n");
    }
    void deleteManagedObjects() override {material=NULL;}

    // f5ce80/f5cae0: call each selected collider with the segment and a None
    // filter, accept its tuple's first distance only if nearer than the current
    // one. Keep the chosen callback/list alive across Python re-entry.
    bool collide(wot::Vector3 a,wot::Vector3 b,PyObject* colliders,std::uint64_t accepted,wot::Vector3& hit) {
        Vector3 start=native(a),end=native(b),direction=end-start;
        float nearest=direction.length();
        direction.normalise();
        bool found=false;
        const Py_ssize_t count=colliders?PySequence_Fast_GET_SIZE(colliders):0;
        for (Py_ssize_t i=0;!closed && i<count;++i) {
            PyObject* collider=PySequence_Fast_GET_ITEM(colliders,i);
            PyObjectPtr args(PyTuple_New(3),PyObjectPtr::STEAL_REFERENCE);
            if (!args) {PyErr_Print();continue;}
            PyTuple_SET_ITEM(args.get(),0,Script::getData(start));
            PyTuple_SET_ITEM(args.get(),1,Script::getData(end));
            Py_INCREF(Py_None);PyTuple_SET_ITEM(args.get(),2,Py_None);
            if (!PyTuple_GET_ITEM(args.get(),0) || !PyTuple_GET_ITEM(args.get(),1)) {
                PyErr_Print();continue;
            }
            PyObjectPtr result(PyObject_CallObject(collider,args.get()),PyObjectPtr::STEAL_REFERENCE);
            if (closed || epoch!=accepted) {if (PyErr_Occurred()) PyErr_Print();throw AbandonedTrajectoryUpdate();}
            if (!result) {PyErr_Print();continue;}
            if (!PyTuple_Check(result.get()) || PyTuple_GET_SIZE(result.get())==0) continue;
            float distance;
            if (Script::setData(PyTuple_GET_ITEM(result.get(),0),distance)!=0) {PyErr_Print();continue;}
            if (distance<nearest) {nearest=distance;hit=portable(start+direction*distance);found=true;}
        }
        if (closed || epoch!=accepted) throw AbandonedTrajectoryUpdate();
        // f5c9a0 shortens the static segment after a dynamic hit.
        const Vector3 limited=found?native(hit):end;
        const float distance=stockGunMarkerSegmentCollision(start,limited);
        if (distance>=0.f) {
            Vector3 segment=limited-start;segment.normalise();
            hit=portable(start+segment*distance);found=true;
        }
        return found;
    }
    void update(const Vector3& target,const Vector3& start,const Vector3& velocity,float interval) {
        const std::uint64_t accepted=++epoch;
        PyObjectPtr colliders;
        PyObjectPtr selected=callback;
        if (selected) {
            Vector3 direction=velocity;direction.normalise();
            const Vector3 end=start+direction*state.maxDistance;
            PyObjectPtr args(PyTuple_New(2),PyObjectPtr::STEAL_REFERENCE);
            if (!args) {PyErr_Print();return;}
            PyTuple_SET_ITEM(args.get(),0,Script::getData(start));
            PyTuple_SET_ITEM(args.get(),1,Script::getData(end));
            if (!PyTuple_GET_ITEM(args.get(),0) || !PyTuple_GET_ITEM(args.get(),1)) {PyErr_Print();return;}
            PyObjectPtr result(PyObject_CallObject(selected.get(),args.get()),PyObjectPtr::STEAL_REFERENCE);
            if (!result) PyErr_Print();
            else {
                // 006f5c80 copies each item into a retained vector. Snapshot a
                // mutable list before any collider can re-enter Python and
                // change it; PySequence_Fast alone may retain the same list.
                colliders=PyObjectPtr(PySequence_Tuple(result.get()),PyObjectPtr::STEAL_REFERENCE);
                if (!colliders) PyErr_Print();
            }
        }
        if (closed || epoch!=accepted) return;
        trajectory::SegmentQuery query=[this,&colliders,accepted](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
            return collide(a,b,colliders.get(),accepted,hit);
        };
        trajectory::BoundaryQuery bounds=[](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
            ClientSpacePtr space=DeprecatedSpaceHelpers::cameraSpace();
            if (!space) return false;
            const AABB box=space->bounds();
            return trajectory::leave_bounds(portable(box.minBounds()),portable(box.maxBounds()),a,b,hit);
        };
        try {state.update(portable(target),portable(start),portable(velocity),interval,query,bounds);}
        catch (const AbandonedTrajectoryUpdate&) {}
    }
    void tick(float dt) {
        if (closed || !state.configured || !state.visible) return;
        Moo::RenderContext& rc=Moo::rc();
        const Vector3 direction=rc.invView().applyVector(rc.camera().nearPlanePoint(state.aim[0],state.aim[1]));
        const Vector3 origin=rc.invView().applyToOrigin();
        trajectory::SegmentQuery query=[](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
            const Vector3 start=native(a),end=native(b);
            const float distance=stockGunMarkerSegmentCollision(start,end);
            if (distance<0.f) return false;
            Vector3 direction=end-start;direction.normalise();
            hit=portable(start+direction*distance);return true;
        };
        state.advance(dt,query,portable(origin),portable(origin+direction*10000.f));
    }
    bool draw() {
        if (closed || !state.configured || !state.visible) return true;
        if (!material || !material->setTechnique("standard") ||
            !material->properties().select(*material->pEffect())) return false;
        ID3DXEffect* effect=material->pEffect()->pEffect();
        const D3DXMATRIX identity(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1);
        const D3DXVECTOR4 white(1,1,1,1);
        // 006f6a22 -> a22ca0/a22970: original Geometrics standard technique.
        if (FAILED(effect->SetMatrix("world",&identity)) ||
            FAILED(effect->SetVector("diffuseColor",&white)) ||
            FAILED(effect->SetBool("zTestDisable",true)) ||
            FAILED(effect->SetBool("zWriteEnable",false)) ||
            FAILED(effect->SetInt("blendMode",0)) ||
            FAILED(effect->SetBool("doubleSided",true)) ||
            FAILED(effect->SetBool("useWireframe",false)) ||
            FAILED(effect->SetTexture("colorTexture",NULL)) ||
            FAILED(effect->SetInt("colorWriteAlpha",0)) ||
            FAILED(effect->SetFloat("outputAlphaMultiplier",1.f))) return false;
        if (!material->begin()) return false;
        bool success=true;
        static_assert(sizeof(trajectory::Vertex)==16,"#1513 XYZD stride");
        for (uint32 pass=0;pass<material->numPasses();++pass) {
            if (!material->beginPass(pass)) {success=false;continue;}
            // The pass may install a vertex declaration: set XYZD after it,
            // in the original 006f6ab8 -> 006f6b84 order.
            Moo::rc().setFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE);
            // Exactly six XYZD vertices and three independent lines (006f6b76).
            if (FAILED(Moo::rc().drawPrimitiveUP(D3DPT_LINELIST,3,state.vertices.data(),16))) success=false;
            if (!material->endPass()) success=false;
        }
        if (!material->end()) success=false;
        return success;
    }
};
std::shared_ptr<TrajectoryDrawer> owner;
// The original type 01a0e990 derives from PyObjectPlus. Use its real layout,
// attribute dispatch and non-constructible Python type instead of a C wrapper.
class WGTrajectoryDrawer : public PyObjectPlus {
    Py_Header(WGTrajectoryDrawer,PyObjectPlus)
public:
    explicit WGTrajectoryDrawer(std::shared_ptr<TrajectoryDrawer> selected):
        PyObjectPlus(&s_type_),value(std::move(selected)) {}
    std::shared_ptr<TrajectoryDrawer> value;
    PY_METHOD_DECLARE(py_setParams)
    PY_METHOD_DECLARE(py_setColors)
    PY_METHOD_DECLARE(py_setGetDynamicCollidersCallback)
    PY_METHOD_DECLARE(py_setStrategicMode)
    PY_METHOD_DECLARE(py_update)
    PY_DEFERRED_ATTRIBUTE_DECLARE(visible)
};
PyObject* singleton=NULL;
TrajectoryDrawer* live(PyObject* object) {
    WGTrajectoryDrawer* wrapper=static_cast<WGTrajectoryDrawer*>(object);
    if (!wrapper->value || wrapper->value->closed) {
        PyErr_SetString(PyExc_RuntimeError,"TrajectoryDrawer owner has been closed");return NULL;
    }
    return wrapper->value.get();
}
PyObject* getVisible(PyObject* object,void*) {
    TrajectoryDrawer* value=live(object);return value?Script::getData(value->state.visible):NULL;
}
int setVisible(PyObject* object,PyObject* visible,void*) {
    TrajectoryDrawer* value=live(object);if (!value) return -1;
    if (!visible) {PyErr_SetString(PyExc_TypeError,"Cannot delete visible");return -1;}
    return Script::setData(visible,value->state.visible,"visible");
}
PyObject* setParams(PyObject* object,PyObject* args) {
    TrajectoryDrawer* value=live(object);if (!value) return NULL;
    PyObject *maximum,*gravity,*aim;float distance;Vector3 acceleration;Vector2 offset;
    if (!PyArg_UnpackTuple(args,"setParams",3,3,&maximum,&gravity,&aim) ||
        Script::setData(maximum,distance) || Script::setData(gravity,acceleration) ||
        Script::setData(aim,offset)) return NULL;
    ++value->epoch;
    value->state.maxDistance=distance;value->state.gravity=portable(acceleration);
    value->state.aim={{offset.x,offset.y}};value->state.configured=true;Py_RETURN_NONE;
}
PyObject* setColors(PyObject* object,PyObject* args) {
    TrajectoryDrawer* value=live(object);if (!value) return NULL;
    PyObject *a,*b,*c;Vector4 good,blocked,missing;
    if (!PyArg_UnpackTuple(args,"setColors",3,3,&a,&b,&c) || Script::setData(a,good) ||
        Script::setData(b,blocked) || Script::setData(c,missing)) return NULL;
    value->state.set_colours(rgba(good),rgba(blocked),rgba(missing));Py_RETURN_NONE;
}
PyObject* setCallback(PyObject* object,PyObject* args) {
    TrajectoryDrawer* value=live(object);if (!value) return NULL;
    PyObject* callback;
    if (!PyArg_UnpackTuple(args,"setGetDynamicCollidersCallback",1,1,&callback)) return NULL;
    value->callback=callback==Py_None?NULL:callback;Py_RETURN_NONE;
}
PyObject* setStrategicMode(PyObject* object,PyObject* args) {
    TrajectoryDrawer* value=live(object);if (!value) return NULL;
    PyObject* strategic;
    if (!PyArg_UnpackTuple(args,"setStrategicMode",1,1,&strategic) ||
        Script::setData(strategic,value->state.strategic)) return NULL;
    Py_RETURN_NONE;
}
PyObject* update(PyObject* object,PyObject* args) {
    TrajectoryDrawer* value=live(object);if (!value) return NULL;
    // This reference survives a callback that finalises the GUI owner.
    std::shared_ptr<TrajectoryDrawer> selected=static_cast<WGTrajectoryDrawer*>(object)->value;
    PyObject *a,*b,*c,*d;Vector3 target,start,velocity;float interval;
    if (!PyArg_UnpackTuple(args,"update",4,4,&a,&b,&c,&d) || Script::setData(a,target) ||
        Script::setData(b,start) || Script::setData(c,velocity) || Script::setData(d,interval)) return NULL;
    try {selected->update(target,start,velocity,interval);}
    catch (const std::exception& error) {PyErr_SetString(PyExc_ValueError,error.what());return NULL;}
    Py_RETURN_NONE;
}
PyObject* WGTrajectoryDrawer::py_setParams(PyObject* args) {return setParams(this,args);}
PyObject* WGTrajectoryDrawer::py_setColors(PyObject* args) {return setColors(this,args);}
PyObject* WGTrajectoryDrawer::py_setGetDynamicCollidersCallback(PyObject* args) {return setCallback(this,args);}
PyObject* WGTrajectoryDrawer::py_setStrategicMode(PyObject* args) {return setStrategicMode(this,args);}
PyObject* WGTrajectoryDrawer::py_update(PyObject* args) {return update(this,args);}
PyObject* WGTrajectoryDrawer::pyGet_visible() {return getVisible(this,NULL);}
int WGTrajectoryDrawer::pySet_visible(PyObject* value) {return setVisible(this,value,NULL);}
PY_TYPEOBJECT(WGTrajectoryDrawer)
PY_BEGIN_METHODS(WGTrajectoryDrawer)
    PY_METHOD(update)
    PY_METHOD(setParams)
    PY_METHOD(setColors)
    PY_METHOD(setGetDynamicCollidersCallback)
    PY_METHOD(setStrategicMode)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(WGTrajectoryDrawer)
    PY_ATTRIBUTE(visible)
PY_END_ATTRIBUTES()
} // namespace

bool initialiseStockTrajectoryDrawer() {
    if (owner) return true;
    if (PyType_Ready(&WGTrajectoryDrawer::s_type_)<0) return false;
    WGTrajectoryDrawer* wrapper=NULL;
    try {
        std::shared_ptr<TrajectoryDrawer> created=std::make_shared<TrajectoryDrawer>();
        wrapper=new WGTrajectoryDrawer(created);
        created->createManagedObjects();
        owner=created;singleton=wrapper;return true;
    } catch (const std::bad_alloc&) {
        Py_XDECREF(wrapper);PyErr_NoMemory();return false;
    } catch (const std::exception& error) {
        Py_XDECREF(wrapper);
        PyErr_SetString(PyExc_RuntimeError,error.what());return false;
    }
}
void finaliseStockTrajectoryDrawer() {
    if (owner) owner->close();
    owner.reset();
    PyObject* previous=singleton;singleton=NULL;Py_XDECREF(previous);
}
void tickStockTrajectoryDrawer(float dt) {if (owner) owner->tick(dt);}
bool drawStockTrajectoryDrawer() {return !owner || owner->draw();}
static PyObject* py_wg_trajectory_drawer(PyObject* args) {
    if (!PyArg_ParseTuple(args,":wg_trajectory_drawer")) return NULL;
    if (!singleton) {PyErr_SetString(PyExc_RuntimeError,"TrajectoryDrawer owner is unavailable");return NULL;}
    Py_INCREF(singleton);return singleton;
}
PY_MODULE_FUNCTION(wg_trajectory_drawer,BigWorld)

// f5c5b0/f5db50: the stock ArtyAimingSystem consumes (time, point, velocity)
// or None. The masks are narrowed to uint16 by f5bf40's Ccb_ constructor.
static PyObject* py_wg_simulateProjectileTrajectory(PyObject* args) {
    PyObject *a,*b,*c,*d,*e,*skip=NULL,*required=NULL;
    Vector3 start,velocity,gravity;float step,epsilon;int skipFlags=0x80,requiredFlags=0;
    if (!PyArg_UnpackTuple(args,"wg_simulateProjectileTrajectory",5,7,&a,&b,&c,&d,&e,&skip,&required) ||
        Script::setData(a,start) || Script::setData(b,velocity) || Script::setData(c,gravity) ||
        Script::setData(d,step) || Script::setData(e,epsilon) ||
        (skip && Script::setData(skip,skipFlags)) || (required && Script::setData(required,requiredFlags))) return NULL;
    trajectory::SegmentQuery scene=[skipFlags,requiredFlags](wot::Vector3 a,wot::Vector3 b,wot::Vector3& hit) {
        const Vector3 start=native(a),end=native(b);
        const float distance=stockGunMarkerSegmentCollision(start,end,
            WorldTriangle::Flags(skipFlags),WorldTriangle::Flags(requiredFlags));
        if (distance<0.f) return false;
        Vector3 direction=end-start;direction.normalise();
        hit=portable(start+direction*distance);return true;
    };
    trajectory::SimulatedHit hit;
    try {
        if (!trajectory::simulate(portable(start),portable(velocity),portable(gravity),step,epsilon,scene,hit))
            Py_RETURN_NONE;
    } catch (const std::exception& error) {PyErr_SetString(PyExc_ValueError,error.what());return NULL;}
    PyObjectPtr result(PyTuple_New(3),PyObjectPtr::STEAL_REFERENCE);
    if (!result) return NULL;
    PyTuple_SET_ITEM(result.get(),0,Script::getData(hit.time));
    PyTuple_SET_ITEM(result.get(),1,Script::getData(native(hit.point)));
    PyTuple_SET_ITEM(result.get(),2,Script::getData(native(hit.velocity)));
    if (!PyTuple_GET_ITEM(result.get(),0) || !PyTuple_GET_ITEM(result.get(),1) || !PyTuple_GET_ITEM(result.get(),2)) return NULL;
    Py_INCREF(result.get());return result.get();
}
PY_MODULE_FUNCTION(wg_simulateProjectileTrajectory,BigWorld)
BW_END_NAMESPACE
