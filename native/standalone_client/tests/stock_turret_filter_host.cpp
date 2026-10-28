// The engine object/collision boundary is bounded here. Type/factory registration,
// turret code and the base receipt/prediction/copy bodies are actual SDK/source.
@SDK_PREFIX@
#include <algorithm>
#include <stdexcept>
#include <cstddef>
@FACTORY_LINK@
@AUTO_PARSE@
@WEAK_HEADER@
@WEAK_SOURCE@

struct Quaternion {float x=0,y=0,z=0,w=1;};
void D3DXQuaternionInverse(Quaternion* out,const Quaternion* value){*out=*value;out->x=-out->x;out->y=-out->y;out->z=-out->z;}
void D3DXQuaternionMultiply(Quaternion* out,const Quaternion* a,const Quaternion* b){
 Quaternion v;v.x=a->w*b->x+a->x*b->w+a->y*b->z-a->z*b->y;
 v.y=a->w*b->y-a->x*b->z+a->y*b->w+a->z*b->x;
 v.z=a->w*b->z+a->x*b->y-a->y*b->x+a->z*b->w;
 v.w=a->w*b->w-a->x*b->x-a->y*b->y-a->z*b->z;*out=v;
}
void D3DXVec3Normalize(Vector3* out,const Vector3* value){*out=*value;out->normalise();}
void D3DXQuaternionRotationAxis(Quaternion* out,const Vector3*,float angle){assert(angle==0.f);*out=Quaternion();}
struct MovementFilter {virtual ~MovementFilter(){};bool copyFrom(const MovementFilter& source){return tryCopyState(source);}protected:virtual bool tryCopyState(const MovementFilter&){return false;}};
struct MovementFilterTarget{};struct Entity:MovementFilterTarget{};
using SpaceID=int;using EntityID=int;using Position3D=Vector3;using Direction3D=Vector3;using uint64=unsigned long long;
struct PyEntity:PyObject{};
template<class T> class WeakPyPtr {
 PyObjectPtr reference_;
public:
 WeakPyPtr()=default;
 explicit WeakPyPtr(T* value){if(value)reference_=PyObjectPtr(PyWeakref_NewRef(value,NULL),PyObjectPtr::STEAL_REFERENCE);}
 T* get()const{PyObject* value=reference_?PyWeakref_GetObject(reference_.get()):Py_None;return value==Py_None?NULL:static_cast<T*>(value);}
};
class PyFilter;
class Filter:public MovementFilter {
 WeakPyPtr<PyFilter> owner_;
public:
 explicit Filter(PyFilter* owner);virtual ~Filter();
 virtual void reset(double){};
 int& environment(){static int value=0;return value;}
protected:const PyFilter* pOwner()const{return owner_.get();}
};
@PY_FILTER_CLASS@;
void registerStockFilterLagDetection(PyFilter*,bool){}
@PY_FILTER_METHODS@
void PyFilter::enableLagDetection(bool){}
double PyFilter::getTimeNow(){return 0;}
Filter::Filter(PyFilter* owner):owner_(owner){}
Filter::~Filter(){if(auto owner=owner_.get())owner->onFilterDestroyed(this);}
@PY_FILTER_TABLES@

struct StockAvatarFilterHelper {
 struct Input {double time=0;};
 struct Waypoint {double time=0;SpaceID spaceID=0;EntityID vehicleID=0;Vector3 position;Quaternion orientation;Input input;};
 struct Listener {virtual ~Listener(){};virtual void onReset()=0;virtual void onWaypoint(double,Waypoint&,Waypoint&)=0;};
 explicit StockAvatarFilterHelper(int&){}
 Listener* listener_=NULL;bool lag=false;
 void listener(Listener* value){listener_=value;}
 bool extrapolating()const{return lag;}
};
@GLOBALS@
@SETTINGS@
@GEOMETRY@;
struct StockEntityFilterMotion {
 Vector3 predictedVelocity,predictedAngularVelocity,predictedAcceleration,predictedAngularAcceleration;
 StockEntityFilterGeometry geometry;WeakPyPtr<PyEntity> owner;
};
using MatrixProviderPtr=PyObjectPtr;
class PyWGEntityFilter;
@ENTITY_FILTER_CLASS@;
class PyWGEntityFilter:public PyFilter {
 Py_Header(PyWGEntityFilter,PyFilter)
public:
 explicit PyWGEntityFilter(PyTypeObject* type=&s_type_);
 static PyWGEntityFilter* New(){return new PyWGEntityFilter();}
 PY_AUTO_FACTORY_DECLARE(PyWGEntityFilter,END)
 StockEntityFilter* native();const StockEntityFilter* native()const;
 virtual bool isLaggingNow()const override;
 void applySettings(StockEntityFilter&);void cacheSettings(const StockEntityFilter&,const StockEntityFilterSettings&);
 const WeakPyPtr<PyEntity>& ownerIdentity()const{return owner_;}
 float maxTimeBetweenInputs()const;void maxTimeBetweenInputs(float value);
 PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(float,maxTimeBetweenInputs,maxTimeBetweenInputs)
 bool segmentMayHitEntity(Vector3 start,Vector3 end,unsigned mode)const;
 PY_AUTO_METHOD_DECLARE(RETDATA,segmentMayHitEntity,ARG(Vector3,ARG(Vector3,ARG(unsigned,END))))
 bool ignoreInputs()const;void ignoreInputs(bool value);
 PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(bool,ignoreInputs,ignoreInputs)
protected:virtual Filter* getNewFilter();
private:
 StockEntityFilterSettings settings_;PyObjectPtr pendingTransfer_,pendingVehicleTransfer_,pendingCallback_;
 PyObjectPtr pendingOffset_;WeakPyPtr<PyEntity> owner_;
};
@BASE_METHODS@
void StockEntityFilter::transfer(const PyWGEntityFilter&,MatrixProviderPtr,bool){throw std::logic_error("Unexpected transfer outside this test boundary");}
void StockEntityFilter::reset(double){onReset();}
void StockEntityFilter::input(double,SpaceID,EntityID,const Position3D&,const Vector3&,const Direction3D&){throw std::logic_error("Unexpected network input outside this test boundary");}
void StockEntityFilter::output(double,MovementFilterTarget&){throw std::logic_error("Unexpected world output outside this test boundary");}
bool StockEntityFilter::getLastInput(double&,SpaceID&,EntityID&,Position3D&,Vector3&,Direction3D&)const{throw std::logic_error("Unexpected input query outside this test boundary");}
bool StockEntityFilter::updateGeometry(const StockAvatarFilterHelper::Waypoint&,const StockAvatarFilterHelper::Waypoint&){return true;}
bool StockEntityFilter::lagEntitiesBlocked()const{throw std::logic_error("Unexpected collision outside this test boundary");}
bool queryStockEntityLagScenePrediction(SpaceID,const Vector3*,const WeakPyPtr<PyEntity>&,Vector3,bool&){throw std::logic_error("Unexpected scene query outside this test boundary");}
@BASE_WAYPOINT@
PY_TYPEOBJECT(PyWGEntityFilter)
PY_BEGIN_METHODS(PyWGEntityFilter)
 PY_METHOD(segmentMayHitEntity)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(PyWGEntityFilter)
 PY_ATTRIBUTE(maxTimeBetweenInputs)
 PY_ATTRIBUTE(ignoreInputs)
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED(PyWGEntityFilter,"WGEntityFilter",BigWorld)
#include "stock_turret_filter.cpp"

std::map<PyFilter*,std::unique_ptr<Filter>> installed;
PyObject* fixture(PyObject*,PyObject* args){
 const char* action;PyObject* value;PyObject* extra=Py_None;
 if(!PyArg_ParseTuple(args,"sO|O",&action,&value,&extra))return NULL;
 if(!PyWGEntityFilter::Check(value)){PyErr_SetString(PyExc_TypeError,"Expected WGEntityFilter");return NULL;}
 auto wrapper=static_cast<PyWGEntityFilter*>(value);
 if(!strcmp(action,"attach")){wrapper->isAttached(true);auto candidate=wrapper->getFilter();if(!candidate)return NULL;installed[wrapper].reset(candidate);Py_RETURN_NONE;}
 if(!strcmp(action,"drop")){installed.erase(wrapper);wrapper->isAttached(false);Py_RETURN_NONE;}
 auto filter=wrapper->native();if(!filter){PyErr_SetString(PyExc_ValueError,"Filter is detached");return NULL;}
 auto motion=filter->motion();
 if(!strcmp(action,"seed")){
  PyObject *a,*b,*c,*d;
  if(!PyArg_ParseTuple(extra,"OOOO",&a,&b,&c,&d)||Script::setData(a,motion->predictedVelocity)||
   Script::setData(b,motion->predictedAngularVelocity)||Script::setData(c,motion->predictedAcceleration)||
   Script::setData(d,motion->predictedAngularAcceleration))return NULL;
  Py_RETURN_NONE;
 }
 if(!strcmp(action,"full")){
  PyObject* result=PyTuple_New(12);unsigned index=0;
  for(const Vector3& vector:{motion->predictedVelocity,motion->predictedAngularVelocity,motion->predictedAcceleration,motion->predictedAngularAcceleration})
   for(unsigned axis=0;axis<3;++axis)PyTuple_SET_ITEM(result,index++,PyFloat_FromDouble(vector[axis]));
  return result;
 }
 if(!strcmp(action,"inspect"))return Py_BuildValue("((fff)(fff)(fff)ii)",motion->predictedVelocity.x,motion->predictedVelocity.y,motion->predictedVelocity.z,motion->predictedAcceleration.x,motion->predictedAcceleration.y,motion->predictedAcceleration.z,motion->predictedAngularAcceleration.x,motion->predictedAngularAcceleration.y,motion->predictedAngularAcceleration.z,dynamic_cast<StockTurretFilter*>(filter)!=NULL,wrapper->isAttached());
 if(!strcmp(action,"reset")){filter->onReset();Py_RETURN_NONE;}
 if(!strcmp(action,"copy")){
  if(!PyWGEntityFilter::Check(extra)||!static_cast<PyWGEntityFilter*>(extra)->native()){PyErr_SetString(PyExc_TypeError,"Expected attached WGEntityFilter");return NULL;}
  return PyBool_FromLong(filter->copyFrom(*static_cast<PyWGEntityFilter*>(extra)->native()));
 }
 StockAvatarFilterHelper::Waypoint current,next;
 current.time=current.input.time=0;next.time=next.input.time=.1;
 if(!strcmp(action,"receipt")){
  Vector3 a,b;float elapsed;int match;
  PyObject *first,*second;
  if(!PyArg_ParseTuple(extra,"OOfi",&first,&second,&elapsed,&match)||Script::setData(first,a)||Script::setData(second,b))return NULL;
  current.position=a;next.position=b;next.time=elapsed;next.input.time=match?elapsed:-1;
  filter->history()->lag=false;filter->onWaypoint(elapsed,current,next);Py_RETURN_NONE;
 }
 if(!strcmp(action,"predict")){
  float elapsed=PyFloat_AsDouble(extra);if(PyErr_Occurred())return NULL;
  current.position=Vector3(0,10,0);next.time=elapsed;filter->history()->lag=true;
  filter->onWaypoint(elapsed,current,next);return Script::getData(next.position);
 }
 PyErr_SetString(PyExc_ValueError,"Unknown fixture action");return NULL;
}
int main(int argc,char** argv){
 assert(argc==2);Py_Initialize();
 PyMethodDef methods[]={{"fixture",fixture,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
 assert(Py_InitModule("BigWorld",methods));PyWGEntityFilter::s_link_pyNew.init();PyWGTurretFilter::s_link_pyNew.init();
 FILE* script=fopen(argv[1],"r");assert(script);int result=PyRun_SimpleFile(script,argv[1]);fclose(script);
 installed.clear();assert(!PyErr_Occurred());Py_Finalize();
 PyWGTurretFilter::s_link_pyNew.fini();PyWGEntityFilter::s_link_pyNew.fini();return result?1:0;
}
