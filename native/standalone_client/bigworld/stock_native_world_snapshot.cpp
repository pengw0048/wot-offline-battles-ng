#include "pch.hpp"
#include "stock_native_world_snapshot.hpp"
#include <Python.h>

#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace stock_native_world {
namespace {
struct Invalid:std::runtime_error {explicit Invalid(const char * message):std::runtime_error(message){}};
PyObject * dictionary(PyObject * value) {if(!value||!PyDict_CheckExact(value))throw Invalid("Native world DTO requires plain dictionaries");return value;}
PyObject * field(PyObject * value,const char * name) {
    PyObject * result=PyDict_GetItemString(dictionary(value),name);if(!result)throw Invalid("Native world DTO field is missing");return result;
}
Py_ssize_t size(PyObject * value) {
    if(PyTuple_CheckExact(value))return PyTuple_GET_SIZE(value);if(PyList_CheckExact(value))return PyList_GET_SIZE(value);
    throw Invalid("Native world DTO requires plain sequences");
}
PyObject * at(PyObject * value,Py_ssize_t index) {return PyTuple_CheckExact(value)?PyTuple_GET_ITEM(value,index):PyList_GET_ITEM(value,index);}
void width(PyObject * value,Py_ssize_t count){if(size(value)!=count)throw Invalid("Native world DTO sequence width is invalid");}
std::int64_t integer(PyObject * value) {
    if(PyInt_CheckExact(value))return PyInt_AS_LONG(value);
    if(!PyLong_CheckExact(value))throw Invalid("Native world DTO integer is invalid");
    const auto out=PyLong_AsLongLong(value);if(PyErr_Occurred())throw Invalid("Native world DTO integer is outside its domain");return out;
}
std::uint32_t u32(PyObject * value) {const auto out=integer(value);if(out<0||std::uint64_t(out)>0xffffffffULL)throw Invalid("Native world DTO identity is outside its domain");return std::uint32_t(out);}
double scalar(PyObject * value) {
    const double out=PyFloat_CheckExact(value)?PyFloat_AS_DOUBLE(value):double(integer(value));
    if(!std::isfinite(out))throw Invalid("Native world DTO scalar is nonfinite");return out;
}
bool boolean(PyObject * value) {if(value==Py_True)return true;if(value==Py_False)return false;throw Invalid("Native world DTO boolean is invalid");}
Point point(PyObject * value) {width(value,3);return {{scalar(at(value,0)),scalar(at(value,1)),scalar(at(value,2))}};}
Key key(PyObject * value) {width(value,2);const Key result{u32(at(value,0)),u32(at(value,1))};if(result.second>0xffffu)throw Invalid("Native world DTO item exceeds the #1513 slot width");return result;}
std::int64_t material(PyObject * value){return value==Py_None?-1:integer(value);}
MaterialKey identity(PyObject * value) {width(value,3);const auto chunk=u32(at(value,0)),item=u32(at(value,1));if(item>0xffffu)throw Invalid("Native world material identity exceeds the slot width");return {chunk,item,material(at(value,2))};}
std::string string(PyObject * value) {
    if(PyString_CheckExact(value))return std::string(PyString_AS_STRING(value),std::size_t(PyString_GET_SIZE(value)));
    if(!PyUnicode_CheckExact(value))throw Invalid("Native world DTO filename is invalid");
    PyObject * utf8=PyUnicode_AsUTF8String(value);if(!utf8)throw Invalid("Native world DTO filename cannot be encoded");
    const std::string out(PyString_AS_STRING(utf8),std::size_t(PyString_GET_SIZE(utf8)));Py_DECREF(utf8);return out;
}
std::vector<Box> boxes(PyObject * value) {
    std::vector<Box> result;
    for(Py_ssize_t i=0;i<size(value);++i) {
        PyObject * row=at(value,i);width(row,3);Box box;box.center=point(at(row,0));
        PyObject * axes=at(row,1);if(size(axes)!=3)throw Invalid("Native world OBB needs exactly three authored axes");
        for(Py_ssize_t j=0;j<3;++j)box.axes.push_back(point(at(axes,j)));
        const auto mat=material(at(row,2));if(mat!=-1&&(mat<73||mat>=86))throw Invalid("Native world OBB material is invalid");box.material=int(mat);result.push_back(std::move(box));
    }
    return result;
}
std::map<std::int64_t,double> health(PyObject * value) {
    std::map<std::int64_t,double> out;
    for(Py_ssize_t i=0;i<size(value);++i){const auto row=at(value,i);width(row,2);const auto mat=integer(at(row,0));if(mat!=-1&&(mat<73||mat>=86))throw Invalid("Native health material is invalid");if(!out.emplace(mat,scalar(at(row,1))).second)throw Invalid("Native health material is duplicated");}
    return out;
}
std::map<std::int64_t,std::vector<std::int64_t>> dependencies(PyObject * value) {
    std::map<std::int64_t,std::vector<std::int64_t>> out;
    for(Py_ssize_t i=0;i<size(value);++i) {
        const auto row=at(value,i);width(row,2);const auto mat=integer(at(row,0));if(mat<73||mat>=86)throw Invalid("Native dependency module is invalid");
        std::vector<std::int64_t> links;const auto values=at(row,1);if(size(values)>13)throw Invalid("Native dependency span is invalid");
        for(Py_ssize_t j=0;j<size(values);++j){const auto next=integer(at(values,j));if(next<73||next>=86)throw Invalid("Native dependency target is invalid");links.push_back(next);}
        if(!out.emplace(mat,std::move(links)).second)throw Invalid("Native dependency module is duplicated");
    }
    return out;
}
struct Prepared {
    Key key;
    FrozenIdentity identity;
    bool tree=false;
    stock_native_destructible::Parameters parameters;
    PyObject *fracture=nullptr,*touchdown=nullptr;
};
Prepared prepare(PyObject * row) {
    Prepared result;result.key=key(field(row,"key"));auto & proof=result.identity;auto & value=proof.instance;
    const auto kind=integer(field(row,"kind"));if(kind<0||kind>3)throw Invalid("Native world DTO kind is invalid");value.kind=Kind(kind);result.tree=value.kind==Kind::Tree;
    value.filename=string(field(row,"filename"));if(value.filename.empty())throw Invalid("Native world DTO descriptor filename is empty");
    value.boxes=boxes(field(row,"boxes"));proof.local_boxes=boxes(field(row,"local_boxes"));value.origin=point(field(row,"origin"));
    value.scaled_health=health(field(row,"scaled_health"));value.reference_health=health(field(row,"reference_health"));
    value.dependencies=dependencies(field(row,"dependencies"));value.inverse_dependencies=dependencies(field(row,"inverse_dependencies"));
    value.kinetic_correction=scalar(field(row,"kinetic_correction"));value.item_scale=scalar(field(row,"item_scale"));
    value.broad_phase_margin=scalar(field(row,"broad_phase_margin"));if(value.broad_phase_margin<0.)throw Invalid("Native world DTO broadphase margin is invalid");
    if(!(value.item_scale>0.))throw Invalid("Native world DTO item scale is invalid");
    value.tree_health=scalar(field(row,"tree_health"));value.descriptor_valid=boolean(field(row,"descriptor_valid"));
    value.retained_collision=boolean(field(row,"retained_collision"));value.admission=Admission::Pending;value.native_name_exact=false;
    const auto signature=field(row,"signature");width(signature,12);for(unsigned i=0;i<12;++i)proof.locator_signature[i]=integer(at(signature,i));
    proof.quantization=scalar(field(row,"quantization"));if(proof.quantization!=1000.)throw Invalid("Native world DTO locator quantization is invalid");
    proof.canonical_ambiguous=boolean(field(row,"ambiguous"));proof.native_pending=true;proof.category=int(kind);
    if(result.tree&&value.descriptor_valid) {
        const auto parameters=field(row,"tree_parameters");width(parameters,7);
        result.parameters.height=scalar(at(parameters,0));result.parameters.mass=scalar(at(parameters,1));result.parameters.spring_angle=scalar(at(parameters,2));
        result.parameters.stiffness=scalar(at(parameters,3));result.parameters.spring_resist=scalar(at(parameters,4));result.parameters.air_resist=scalar(at(parameters,5));result.parameters.bury_depth=scalar(at(parameters,6));
        result.fracture=field(row,"fracture_variants");result.touchdown=field(row,"touchdown_variants");
    }
    return result;
}
template<class T,class Reader> std::set<T> set(PyObject * value,Reader read) {std::set<T> out;for(Py_ssize_t i=0;i<size(value);++i)out.insert(read(at(value,i)));return out;}
}
bool installWorldSnapshot(PyObject * dto,Registry & registry,NativeScene & scene,stock_native_destructible::NativeBackend & backend) {
    try {
        if(!registry.instances.empty()||!registry.baked_instances.empty())throw Invalid("Native world snapshot requires a fresh space owner");
        const bool available=boolean(field(dto,"catalog_available"));const double unit=scalar(field(dto,"unit_vehicle_mass"));if(!(unit>0.))throw Invalid("Native unit vehicle mass is invalid");
        const auto rows=field(dto,"instances");std::vector<Prepared> prepared;std::set<Key> unique;
        for(Py_ssize_t i=0;i<size(rows);++i){auto item=prepare(at(rows,i));if(!unique.insert(item.key).second)throw Invalid("Native world DTO identity is duplicated");prepared.push_back(std::move(item));}
        const auto destroyed=set<MaterialKey>(field(dto,"broken"),identity),speculative=set<MaterialKey>(field(dto,"speculative"),identity);
        const auto isolated_chunks=set<std::uint32_t>(field(dto,"isolated_chunks"),u32),repairs=set<std::uint32_t>(field(dto,"layout_repairs"),u32);
        const auto isolated_slots=set<Key>(field(dto,"isolated_slots"),key),excluded=set<Key>(field(dto,"excluded_instances"),key);
        std::map<std::uint32_t,std::int64_t> generations;const auto raw_generations=field(dto,"layout_generations");
        for(Py_ssize_t i=0;i<size(raw_generations);++i){auto row=at(raw_generations,i);width(row,2);const auto generation=integer(at(row,1));if(generation<0)throw Invalid("Native layout generation is invalid");generations[u32(at(row,0))]=generation;}
        // The owner is fresh and the entire external DTO is now validated.
        registry.catalog_available=available;registry.unit_vehicle_mass=unit;
        registry.skin.destroyed=destroyed;registry.skin.speculative=speculative;registry.skin.isolated_chunks=isolated_chunks;
        registry.skin.isolated_slots=isolated_slots;registry.skin.excluded_instances=excluded;registry.skin.layout_repairs=repairs;registry.skin.layout_generations=generations;
        for(auto & item:prepared) {
            registry.putBaked(item.key,item.identity.instance);
            if(item.tree&&item.identity.instance.descriptor_valid)backend.installTree(item.key,item.parameters,item.fracture,item.touchdown);
            if(!scene.install(item.key,std::move(item.identity)))throw Invalid("Native world identity could not be staged");
        }
        registry.syncBaked();
        return true;
    } catch(const std::exception & error) {
        if(!PyErr_Occurred())PyErr_SetString(PyExc_ValueError,error.what());return false;
    }
}
}
