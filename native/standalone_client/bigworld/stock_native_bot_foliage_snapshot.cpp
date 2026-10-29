#include "stock_native_bot_foliage_snapshot.hpp"
#include <Python.h>
#ifdef copysign
#undef copysign
#endif
#ifdef hypot
#undef hypot
#endif
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_bot_foliage {
namespace {
struct Ref {PyObject*p;explicit Ref(PyObject*v):p(v){if(!p)throw std::runtime_error("Native foliage snapshot read failed");}~Ref(){Py_DECREF(p);}Ref(const Ref&)=delete;};
PyObject* value(PyObject*object,const char*key){
    if(!object)throw std::invalid_argument("Native foliage owner is unavailable");PyObject*p;
    if(PyDict_Check(object)){p=PyDict_GetItemString(object,key);if(!p)p=Py_None;Py_INCREF(p);}
    else {p=PyObject_GetAttrString(object,key);if(!p&&PyErr_ExceptionMatches(PyExc_AttributeError)){PyErr_Clear();p=Py_None;Py_INCREF(p);}}
    return p;
}
double number(PyObject*p){if(!p||p==Py_None||PyBool_Check(p))throw std::invalid_argument("Native foliage number is invalid");const double v=PyFloat_AsDouble(p);if(PyErr_Occurred()||!std::isfinite(v))throw std::invalid_argument("Native foliage number is nonfinite");return v;}
std::size_t index(PyObject*p){Ref exact(PyNumber_Index(p));Ref v(PyNumber_Long(exact.p));const auto n=PyLong_AsUnsignedLongLong(v.p);if(PyErr_Occurred()||PyBool_Check(p)||n>std::numeric_limits<std::size_t>::max())throw std::invalid_argument("Native foliage index is invalid");return std::size_t(n);}
int coordinate(PyObject*p){Ref v(PyNumber_Index(p));const auto n=PyLong_AsLong(v.p);if(PyErr_Occurred()||PyBool_Check(p)||n<std::numeric_limits<int>::min()||n>std::numeric_limits<int>::max())throw std::invalid_argument("Native foliage cell is invalid");return int(n);}
void width(PyObject*p,Py_ssize_t n){if((!PyList_Check(p)&&!PyTuple_Check(p))||PySequence_Size(p)!=n)throw std::invalid_argument("Native foliage sequence width is invalid");}
PyObject* at(PyObject*p,Py_ssize_t i){return PyTuple_Check(p)?PyTuple_GET_ITEM(p,i):PyList_GET_ITEM(p,i);}
Point point(PyObject*p){width(p,3);return {{number(at(p,0)),number(at(p,1)),number(at(p,2))}};}
Key key(PyObject*p){width(p,2);const auto a=index(at(p,0)),b=index(at(p,1));if(a>0xffffffffULL||b>0xffffu)throw std::invalid_argument("Native foliage tree identity is invalid");return {std::uint32_t(a),std::uint32_t(b)};}
native_visibility::Cell cell(PyObject*p){width(p,2);return {coordinate(at(p,0)),coordinate(at(p,1))};}
template<class Read> void each(PyObject*p,Read read){Ref iterator(PyObject_GetIter(p));while(PyObject*raw=PyIter_Next(iterator.p)){Ref row(raw);read(row.p);}if(PyErr_Occurred())throw std::invalid_argument("Native foliage iterable is invalid");}
template<class Read> void dictionary(PyObject*p,Read read){if(!PyDict_Check(p))throw std::invalid_argument("Native foliage map is invalid");Py_ssize_t pos=0;PyObject*k,*v;while(PyDict_Next(p,&pos,&k,&v))read(k,v);}
std::vector<std::size_t> indices(PyObject*p){std::vector<std::size_t> out;each(p,[&](PyObject*v){out.push_back(index(v));});return out;}
native_visibility::Volume volume(PyObject*p){
    native_visibility::Volume v;
    if(PyDict_Check(p)) {v.dynamic=true;Ref center(value(p,"center")),axes(value(p,"half_axes")),strength(value(p,"strength")),radius(value(p,"radius"));v.center=point(center.p);width(axes.p,3);for(unsigned i=0;i<3;++i)v.half_axes[i]=point(at(axes.p,i));v.strength=number(strength.p);v.radius=number(radius.p);}
    else {width(p,10);for(unsigned i=0;i<10;++i)v.static_row[i]=number(at(p,i));}
    return v;
}
bool enabled(PyObject*battle,PyObject*foliage){
    if(foliage==Py_None)return false;Ref disabled(value(battle,"_disabled_optional_features"));if(disabled.p==Py_None)return true;
    Ref name(PyString_FromString("foliage camouflage"));const int contains=PySequence_Contains(disabled.p,name.p);if(contains<0)throw std::invalid_argument("Native foliage feature state is invalid");return contains==0;
}
native_visibility::FoliageSnapshot snapshot(PyObject*battle,PyObject*foliage){
    native_visibility::FoliageSnapshot out;out.enabled=enabled(battle,foliage);if(!out.enabled)return out;
    Ref size(value(foliage,"cell_size")),rows(value(foliage,"instances")),cells(value(foliage,"cells")),inactive(value(foliage,"inactive_instances"));out.cell_size=number(size.p);
    if(out.cell_size<=0.)throw std::invalid_argument("Native foliage cell size is invalid");
    each(rows.p,[&](PyObject*p){out.instances.push_back(volume(p));});
    dictionary(cells.p,[&](PyObject*k,PyObject*v){out.cells.emplace(cell(k),indices(v));});
    each(inactive.p,[&](PyObject*p){out.inactive_instances.insert(index(p));});
    for(const auto&c:out.cells)for(auto i:c.second)if(i>=out.instances.size())throw std::invalid_argument("Native foliage membership is outside its rows");return out;
}
Point cross(const Point&a,const Point&b){return {{a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}};}
double dot(const Point&a,const Point&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double roundFive(double value){
    if(value==0.)return value;int exponent=0;const double fraction=std::frexp(std::abs(value),&exponent);
    if(exponent>40)return value;
    const auto mantissa=std::uint64_t(std::ldexp(fraction,53));
    const std::uint64_t lower=(mantissa&0xffffffffULL)*3125u,upper=(mantissa>>32)*3125u;
    const std::uint64_t shifted=upper<<32,low=lower+shifted,high=(upper>>32)+(low<lower?1u:0u);
    const int shift=48-exponent;std::uint64_t rounded=0;
    if(shift<64){rounded=(low>>shift)|(high<<(64-shift));if((low&((std::uint64_t(1)<<shift)-1))>=(std::uint64_t(1)<<(shift-1)))++rounded;}
    else if(shift==64)rounded=high+(low>=(std::uint64_t(1)<<63)?1u:0u);
    else if(shift==65&&high)rounded=1;
    return std::copysign(double(rounded)/100000.,value);
}
std::array<double,12> signature(const Point&p,const std::array<Point,3>&axes){
    std::array<double,12> out;unsigned n=0;
    for(double v:p)out[n++]=roundFive(v);for(const auto&a:axes)for(double v:a)out[n++]=roundFive(v);return out;
}
}
bool readFoliageSnapshot(PyObject*battle,native_visibility::FoliageSnapshot&output){try{Ref foliage(value(battle,"_foliage"));auto out=snapshot(battle,foliage.p);output=std::move(out);return true;}catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}}
bool readFoliageUpdate(PyObject*battle,offline_visibility::Update&output){
    try {
        offline_visibility::Update out;Ref foliage(value(battle,"_foliage"));if(!enabled(battle,foliage.p)){output=std::move(out);return true;}
        Ref rows(value(foliage.p,"instances")),dirty(value(foliage.p,"native_dirty_instances")),cells(value(foliage.p,"cells")),dirty_cells(value(foliage.p,"native_dirty_cells")),inactive(value(foliage.p,"inactive_instances"));
        std::set<std::size_t> ordered;each(dirty.p,[&](PyObject*p){ordered.insert(index(p));});
        for(auto i:ordered){Ref row(PySequence_GetItem(rows.p,Py_ssize_t(i)));out.instances.emplace_back(i,volume(row.p));}
        each(dirty_cells.p,[&](PyObject*k){auto p=PyDict_GetItem(cells.p,k);out.cells.emplace_back(cell(k),p?indices(p):std::vector<std::size_t>{});});
        each(inactive.p,[&](PyObject*p){out.inactive.insert(index(p));});output=std::move(out);return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool readFoliageTreeState(PyObject*battle,TreeState&output){
    try {
        TreeState out;Ref foliage(value(battle,"_foliage"));out.foliage=snapshot(battle,foliage.p);if(!out.foliage.enabled){output=std::move(out);return true;}
        Ref profiles(value(foliage.p,"fallen_tree_profiles")),active(value(foliage.p,"activated_fallen_trees")),refreshing(value(foliage.p,"refreshing_fallen_trees")),instances(value(foliage.p,"fallen_tree_instances")),cells(value(foliage.p,"fallen_tree_cells"));
        dictionary(profiles.p,[&](PyObject*k,PyObject*v){width(v,2);auto bounds=at(v,0);width(bounds,6);TreeProfile p;for(unsigned i=0;i<3;++i){const double low=number(at(bounds,i)),high=number(at(bounds,i+3));p.center[i]=(low+high)*.5;p.half_sizes[i]=(high-low)*.5;}if(at(v,1)!=Py_None)p.standing=index(at(v,1));out.profiles.emplace(key(k),p);});
        each(active.p,[&](PyObject*p){out.activated.insert(key(p));});each(refreshing.p,[&](PyObject*p){out.refreshing.insert(key(p));});
        dictionary(instances.p,[&](PyObject*k,PyObject*v){out.instances.emplace(key(k),index(v));});
        dictionary(cells.p,[&](PyObject*k,PyObject*v){auto& values=out.cells[key(k)];each(v,[&](PyObject*p){values.push_back(cell(p));});});
        for(const auto&p:out.profiles)if(p.second.standing)for(const auto&c:out.foliage.cells)if(std::find(c.second.begin(),c.second.end(),*p.second.standing)!=c.second.end())out.standing_cells[*p.second.standing].push_back(c.first);
        Ref seen(value(battle,"_fallen_tree_foliage_seen_bodies")),stable(value(battle,"_fallen_tree_foliage_stable")),next(value(battle,"_next_fallen_tree_foliage_refresh"));
        if(seen.p!=Py_None)each(seen.p,[&](PyObject*p){out.seen_bodies.insert(key(p));});
        if(stable.p!=Py_None)dictionary(stable.p,[&](PyObject*k,PyObject*v){width(v,2);width(at(v,0),12);StablePose p;for(unsigned i=0;i<12;++i)p.signature[i]=number(at(at(v,0),i));p.reads=unsigned(index(at(v,1)));out.stable.emplace(key(k),p);});
        if(next.p!=Py_None)out.next_refresh=number(next.p);output=std::move(out);return true;
    }catch(const std::exception&e){if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,e.what());return false;}
}
bool Trees::activate(Key key){if(!state_.foliage.enabled||!state_.profiles.count(key)||!state_.activated.insert(key).second)return false;state_.refreshing.insert(key);state_.next_refresh=0.;state_.seen_bodies.erase(key);state_.stable.erase(key);return true;}
bool Trees::update(Key key,const Point&center,const std::array<Point,3>&axes,std::set<std::size_t>&dirty,std::set<native_visibility::Cell>&changed){
    for(double v:center)if(!std::isfinite(v))throw std::invalid_argument("Native fallen foliage centre is nonfinite");for(const auto&a:axes)for(double v:a)if(!std::isfinite(v))throw std::invalid_argument("Native fallen foliage axis is nonfinite");
    if(std::abs(dot(axes[0],cross(axes[1],axes[2])))<=1.e-9)throw std::invalid_argument("Native fallen foliage volume is degenerate");
    native_visibility::Volume row;row.dynamic=true;row.center=center;row.half_axes=axes;row.strength=.15;
    double x0=INFINITY,z0=INFINITY,x1=-INFINITY,z1=-INFINITY;
    for(double a:{-1.,1.})for(double b:{-1.,1.})for(double c:{-1.,1.}){Point p;for(unsigned i=0;i<3;++i)p[i]=center[i]+a*axes[0][i]+b*axes[1][i]+c*axes[2][i];row.radius=std::max(row.radius,std::hypot(p[0]-center[0],p[2]-center[2]));x0=std::min(x0,p[0]);x1=std::max(x1,p[0]);z0=std::min(z0,p[2]);z1=std::max(z1,p[2]);}
    auto existing=state_.instances.find(key);if(existing!=state_.instances.end()){const auto&old=state_.foliage.instances.at(existing->second);if(old.dynamic&&old.center==row.center&&old.half_axes==row.half_axes&&old.strength==row.strength&&old.radius==row.radius)return false;}
    const auto& profile=state_.profiles.at(key);if(profile.standing&&state_.foliage.inactive_instances.insert(*profile.standing).second){const auto&cells=state_.standing_cells[*profile.standing];changed.insert(cells.begin(),cells.end());}
    std::size_t instance;
    if(existing==state_.instances.end()){instance=state_.foliage.instances.size();state_.foliage.instances.push_back(row);state_.instances[key]=instance;}
    else {instance=existing->second;for(const auto&c:state_.cells[key]){changed.insert(c);const auto found=state_.foliage.cells.find(c);if(found==state_.foliage.cells.end())continue;auto&v=found->second;const auto old=std::find(v.begin(),v.end(),instance);if(old!=v.end())v.erase(old);if(v.empty())state_.foliage.cells.erase(found);}state_.foliage.instances[instance]=row;}
    auto&cells=state_.cells[key];cells.clear();const double size=state_.foliage.cell_size;
    for(int x=int(std::floor(x0/size));x<=int(std::floor(x1/size));++x)for(int z=int(std::floor(z0/size));z<=int(std::floor(z1/size));++z){const native_visibility::Cell c{x,z};state_.foliage.cells[c].push_back(instance);cells.push_back(c);changed.insert(c);}
    dirty.insert(instance);return true;
}
bool Trees::refresh(double now,const std::set<Key>&bodies,TreePoses&source,offline_visibility::Update&output,bool force,bool force_no_animation){
    output={};if(!std::isfinite(now))throw std::invalid_argument("Native fallen foliage clock is invalid");
    if(!state_.foliage.enabled||state_.refreshing.empty()||(!force&&now+1.e-9<state_.next_refresh))return false;state_.next_refresh=now+.1;
    std::set<std::size_t> dirty;std::set<native_visibility::Cell> changed;std::vector<Key> pending(state_.refreshing.begin(),state_.refreshing.end());
    auto settle=[&](Key key){if(state_.instances.count(key))state_.refreshing.erase(key);state_.seen_bodies.erase(key);state_.stable.erase(key);};
    for(const auto&key:pending){
        if(source.waiting(key.first)||!source.loaded(key.first)){state_.stable.erase(key);continue;}
        if(state_.seen_bodies.count(key)&&!bodies.count(key)){settle(key);continue;}
        const auto&profile=state_.profiles.at(key);Point center;std::array<Point,3>axes;if(!source.pose(key,profile.center,profile.half_sizes,center,axes))continue;
        update(key,center,axes,dirty,changed);
        if(bodies.count(key)){state_.seen_bodies.insert(key);state_.stable.erase(key);continue;}
        if(state_.seen_bodies.count(key)||force_no_animation){settle(key);continue;}
        const auto sign=signature(center,axes);auto&stable=state_.stable[key];stable.reads=stable.signature==sign?stable.reads+1:1;stable.signature=sign;if(stable.reads>=3)settle(key);
    }
    for(auto i:dirty)output.instances.emplace_back(i,state_.foliage.instances.at(i));for(const auto&c:changed){const auto found=state_.foliage.cells.find(c);output.cells.emplace_back(c,found==state_.foliage.cells.end()?std::vector<std::size_t>{}:found->second);}output.inactive=state_.foliage.inactive_instances;return !dirty.empty();
}
}
