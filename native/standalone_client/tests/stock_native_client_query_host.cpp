#define WOT_CLIENT_QUERY_HOST_BUILD
#include <array>
namespace stock_native_bot_geometry {
struct Vehicle {std::array<double,3> minimum{},maximum{};bool broken=false;};
}
#include "../bigworld/stock_native_client_query.cpp"
namespace stock_native_client_state {
Actor* State::find(const std::string&key){const auto at=actors.find(key);return at==actors.end()?nullptr:at->second.get();}
const Actor* State::find(const std::string&key)const{const auto at=actors.find(key);return at==actors.end()?nullptr:at->second.get();}
namespace {using Handle=std::shared_ptr<State>;constexpr const char*state_name="query.test.state";
void freeState(PyObject*c){delete static_cast<Handle*>(PyCapsule_GetPointer(c,state_name));}}
std::shared_ptr<State> fromBattle(PyObject*b){auto c=stock_native_bot_python::value(b,"_native_client_state");auto*h=static_cast<Handle*>(PyCapsule_GetPointer(c.get(),state_name));if(!h)throw stock_native_bot_python::Error("test state missing");return *h;}
}
namespace stock_native_client_query {namespace {
std::unique_ptr<geo::Vehicle> readGeometry(PyObject*d,PyObject*,bool,std::uint64_t){
    if(p::truth(d,"unloaded"))throw p::Error("test native BSP unavailable");
    auto out=std::make_unique<geo::Vehicle>();out->minimum=p::numbers<3>(field(d,"minimum").get());out->maximum=p::numbers<3>(field(d,"maximum").get());out->broken=p::truth(d,"broken");return out;
}
Point nativeXYZ(PyObject*o){return {p::numeric(o,"x"),p::numeric(o,"y"),p::numeric(o,"z")};}
bool engineOwns(std::int64_t,PyObject*e){return p::truth(e,"owned",true);}
std::optional<Bounds> bounds(const geo::Vehicle&g,const state::Actor&a){
    Bounds b;for(unsigned i=0;i<3;++i){b.minimum[i]=a.current[i]+g.minimum[i];b.maximum[i]=a.current[i]+g.maximum[i];}return b;
}
std::vector<double> hits(const geo::Vehicle&g,const state::Actor&a,const Point&start,const Point&end){
    if(g.broken)throw p::Error("test native BSP failed");const auto b=bounds(g,a);const auto d=entryDistance(*b,start,end);return d?std::vector<double>{*d}:std::vector<double>{};
}
std::optional<double> hullRadius(const geo::Vehicle&g){return std::hypot(g.maximum[0]-g.minimum[0],g.maximum[2]-g.minimum[2])*.5;}
std::optional<double> world_depth;
std::optional<double> scenery(std::int64_t,const Point&,const Point&){return world_depth;}
std::vector<Collision> collisionRows(const geo::Vehicle&g,const state::Actor&a,const Point&start,const Point&end){std::vector<Collision> out;for(double d:hits(g,a,start,end))out.push_back({d,.75,1,7});return out;}
}
const char* componentName(const geo::Vehicle&,unsigned){return "vehicleHull";}
}
namespace {
namespace p=stock_native_bot_python;namespace q=stock_native_client_query;namespace s=stock_native_client_state;
void update(s::Actor&a,PyObject*row){
    a.key=p::string(p::value(row,"key").get());a.engine_id=p::integral(row,"engine_id");a.ready=p::truth(row,"ready",true);a.visible=p::truth(row,"visible",true);a.alive=p::truth(row,"alive",true);a.dead=!a.alive;a.local=p::truth(row,"local");a.detached=p::truth(row,"detached");a.has_current=p::truth(row,"has_current",true);a.native_remote=true;a.descriptor_revision=p::integral(row,"descriptor_revision");
    auto pose=p::value(row,"pose");if(pose.get()!=Py_None)a.current=p::numbers<8>(pose.get());
}
PyObject* state(PyObject*,PyObject*args){PyObject*b,*rows;if(!PyArg_ParseTuple(args,"OO",&b,&rows))return nullptr;try{
    std::shared_ptr<s::State> value;auto c=p::value(b,"_native_client_state");if(c.get()==Py_None){value=std::make_shared<s::State>();auto*h=new std::shared_ptr<s::State>(value);p::Ref cap(PyCapsule_New(h,"query.test.state",s::freeState));if(PyObject_SetAttrString(b,"_native_client_state",cap.get())<0)throw p::Error("test state installation failed");}else value=s::fromBattle(b);
    std::set<std::string> seen;for(Py_ssize_t i=0;i<p::size(rows);++i){auto row=p::item(rows,i);const auto key=p::string(p::value(row.get(),"key").get());seen.insert(key);auto&a=value->actors[key];if(!a)a=std::make_shared<s::Actor>();update(*a,row.get());}
    for(auto i=value->actors.begin();i!=value->actors.end();)if(!seen.count(i->first))i=value->actors.erase(i);else ++i;return p::retain(Py_None).release();
}catch(const std::exception&e){return q::fail(e);}}
PyObject* reset(PyObject*,PyObject*args){PyObject*b;int retired=0;if(!PyArg_ParseTuple(args,"O|i",&b,&retired))return nullptr;try{auto s=s::fromBattle(b);++s->epoch;s->retired=retired!=0;s->actors.clear();return p::retain(Py_None).release();}catch(const std::exception&e){return q::fail(e);}}
PyObject* stats(PyObject*,PyObject*args){PyObject*c;if(!PyArg_ParseTuple(args,"O",&c))return nullptr;try{auto&o=q::owner(c);return Py_BuildValue("(KKKK)",o.cold_installs,o.queries,o.bounds_tests,o.bsp_tests);}catch(const std::exception&e){return q::fail(e);}}
PyObject* world(PyObject*,PyObject*args){PyObject*v;if(!PyArg_ParseTuple(args,"O",&v))return nullptr;try{q::world_depth=v==Py_None?std::nullopt:std::optional<double>(p::scalar(v));return p::retain(Py_None).release();}catch(const std::exception&e){return q::fail(e);}}
PyObject*resource(PyObject*,PyObject*args){PyObject*b;const char*key;if(!PyArg_ParseTuple(args,"Os",&b,&key))return nullptr;try{auto state=s::fromBattle(b);auto*a=state->find(key);if(!a)Py_RETURN_NONE;return Py_BuildValue("(iiO)",q::geometry(*a)!=nullptr,a->has_current,q::descriptor(*a)?q::descriptor(*a):Py_None);}catch(const std::exception&e){return q::fail(e);}}
using GeometryPin=std::shared_ptr<const void>;
void freeGeometryPin(PyObject*c){delete static_cast<GeometryPin*>(PyCapsule_GetPointer(c,"query.test.geometry"));}
PyObject*borrow(PyObject*,PyObject*args){PyObject*b;const char*key;if(!PyArg_ParseTuple(args,"Os",&b,&key))return nullptr;try{auto*a=s::fromBattle(b)->find(key);if(!a||!a->geometry)throw p::Error("Test geometry owner is unavailable");return PyCapsule_New(new GeometryPin(a->geometry),"query.test.geometry",freeGeometryPin);}catch(const std::exception&e){return q::fail(e);}}
PyObject*same(PyObject*,PyObject*args){PyObject*c,*b;const char*key;if(!PyArg_ParseTuple(args,"OOs",&c,&b,&key))return nullptr;try{auto*held=static_cast<GeometryPin*>(PyCapsule_GetPointer(c,"query.test.geometry"));if(!held)throw p::Error("Test geometry lease is invalid");auto*a=s::fromBattle(b)->find(key);return p::boolean(a&&*held==a->geometry).release();}catch(const std::exception&e){return q::fail(e);}}
PyMethodDef methods[]={
{"borrow",borrow,METH_VARARGS,""},{"same_resource",same,METH_VARARGS,""},{"statistics",q::statistics,METH_VARARGS,""},
{"resource",resource,METH_VARARGS,""},{"state",state,METH_VARARGS,""},{"reset",reset,METH_VARARGS,""},{"stats",stats,METH_VARARGS,""},{"world",world,METH_VARARGS,""},
{"create",q::create,METH_VARARGS,""},{"ingest",q::ingest,METH_VARARGS,""},{"candidates",q::candidates,METH_VARARGS,""},{"segment_may_hit",q::segmentMayHit,METH_VARARGS,""},{"collisions",q::collisions,METH_VARARGS,""},{"outline",q::outline,METH_VARARGS,""},{"turrets",q::turrets,METH_VARARGS,""},{"flags",q::flags,METH_VARARGS,""},{"close",q::close,METH_VARARGS,""},{nullptr,nullptr,0,nullptr}};
}
PyMODINIT_FUNC initclient_query_test(){Py_InitModule("client_query_test",methods);}
