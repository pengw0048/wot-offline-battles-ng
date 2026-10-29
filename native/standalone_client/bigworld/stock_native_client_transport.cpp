#if !defined(WOT_CLIENT_TRANSPORT_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_client_transport.hpp"
#include "stock_native_bot_python.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <utility>

namespace stock_native_client_transport {
namespace p=stock_native_bot_python;
namespace {
PyObject *error_type=nullptr,*static_dict=nullptr,*static_list=nullptr,*pending_key=nullptr,*builtin_round=nullptr;
constexpr const char* baseline_name="offline.client.snapshot.baseline";
constexpr const char* pending_name="offline.client.snapshot.pending";
struct Baseline {
    p::Ref sequence,lineage,actors;
    Baseline(p::Ref a,p::Ref b,p::Ref c):sequence(std::move(a)),lineage(std::move(b)),actors(std::move(c)){}
};
void baselineDestroy(PyObject*c){auto*p=static_cast<Baseline*>(PyCapsule_GetPointer(c,baseline_name));if(p)delete p;else PyErr_Clear();}
void pendingDestroy(PyObject*c){auto*p=static_cast<p::Ref*>(PyCapsule_GetPointer(c,pending_name));if(p)delete p;else PyErr_Clear();}
PyObject* get(PyObject*d,const char*k){auto*v=PyDict_Check(d)?PyDict_GetItemString(d,k):nullptr;return v?v:Py_None;}
bool has(PyObject*d,const char*k){return PyDict_Check(d)&&PyDict_GetItemString(d,k);}
bool string(PyObject*v){return PyUnicode_Check(v)
#if PY_MAJOR_VERSION<3
    ||PyString_Check(v)
#endif
    ;}
bool integer(PyObject*v){return !PyBool_Check(v)&&(PyLong_Check(v)
#if PY_MAJOR_VERSION<3
    ||PyInt_Check(v)
#endif
    );}
bool equal(PyObject*a,PyObject*b){const int n=PyObject_RichCompareBool(a,b,Py_EQ);if(n<0)throw p::Error("Snapshot equality failed");return n!=0;}
void require(bool ok,const char*message){if(!ok)throw p::Error(message);}
void set(PyObject*d,PyObject*k,const p::Ref&v){if(PyDict_SetItem(d,k,v.get())<0)throw p::Error("Snapshot allocation failed");}
void remove(PyObject*d,PyObject*k){if(PyDict_DelItem(d,k)<0)throw p::Error("Missing snapshot field removal");}
bool owned(PyObject*v){return (static_dict&&PyObject_TypeCheck(v,reinterpret_cast<PyTypeObject*>(static_dict)))||(static_list&&PyObject_TypeCheck(v,reinterpret_cast<PyTypeObject*>(static_list)));}
p::Ref clone(PyObject*v,bool preserve_static=false){
    if(preserve_static&&owned(v))return p::retain(v);
    if(v==Py_None||PyBool_Check(v)||integer(v)||PyFloat_Check(v)||string(v))return p::retain(v);
    if(PyDict_Check(v)){auto out=p::dict();Py_ssize_t n=0;PyObject*k=nullptr,*x=nullptr;while(PyDict_Next(v,&n,&k,&x))set(out.get(),k,clone(x,preserve_static));return out;}
    if(PyList_Check(v)||PyTuple_Check(v)){auto out=p::list();for(Py_ssize_t i=0;i<PySequence_Size(v);++i){auto x=p::item(v,i);p::append(out.get(),clone(x.get(),preserve_static));}return out;}
    throw p::Error("Unsupported snapshot JSON value");
}
void ownStatic(PyObject*row){
    for(const char*name:{"outfits","effective_params"}){auto*v=get(row,name);if(v==Py_None||owned(v))continue;
        auto*type=PyDict_Check(v)?static_dict:(PyList_Check(v)?static_list:nullptr);if(!type)continue;
        // Static versions change only at loadout boundaries. Their existing
        // read-only types also own the cold canonicalization memo.
        p::Ref frozen(PyObject_CallFunctionObjArgs(type,v,nullptr));p::set(row,name,frozen);
    }
}
p::Ref sequence(PyObject*v){require(integer(v),"Invalid snapshot sequence");auto zero=p::integer(0);const int n=PyObject_RichCompareBool(v,zero.get(),Py_GT);require(n==1,"Invalid snapshot sequence");return p::retain(v);}
p::Ref lineage(PyObject*wire){
    for(const char*k:{"round_id","authority_epoch"})require(integer(get(wire,k)),"Invalid snapshot lineage");
    require(has(wire,"bot_authority_id")&&(get(wire,"bot_authority_id")==Py_None||integer(get(wire,"bot_authority_id"))),"Invalid snapshot authority");
    require(string(get(wire,"map")),"Invalid snapshot map");
    require(!has(wire,"bot_manifest_revision")||integer(get(wire,"bot_manifest_revision")),"Invalid snapshot manifest revision");
    p::Ref out(PyTuple_New(5));unsigned i=0;for(const char*k:{"round_id","authority_epoch","map","bot_authority_id","bot_manifest_revision"})PyTuple_SET_ITEM(out.get(),i++,p::retain(get(wire,k)).release());return out;
}
p::Ref indexRows(PyObject*rows){
    require(PyList_Check(rows),"Invalid snapshot actor list");auto indexed=p::dict();
    for(Py_ssize_t i=0;i<PyList_GET_SIZE(rows);++i){auto*row=PyList_GET_ITEM(rows,i);require(PyDict_Check(row)&&integer(get(row,"id")),"Invalid snapshot actor row");Py_ssize_t n=0;PyObject*k=nullptr,*v=nullptr;while(PyDict_Next(row,&n,&k,&v))require(string(k),"Invalid snapshot actor key");auto*id=get(row,"id");require(PyDict_Contains(indexed.get(),id)==0,"Duplicate snapshot actor");set(indexed.get(),id,p::retain(row));}return indexed;
}
bool allowedKeys(PyObject*d,std::initializer_list<const char*> names){if(!PyDict_Check(d))return false;std::set<std::string> allowed;for(auto*k:names)allowed.insert(k);Py_ssize_t n=0;PyObject*k=nullptr,*v=nullptr;while(PyDict_Next(d,&n,&k,&v))if(!string(k)||!allowed.count(p::string(k)))return false;return true;}
p::Ref applyRows(PyObject*patch,PyObject*previous){
    require(allowedKeys(patch,{"add","update","remove"}),"Invalid snapshot actor patch");auto empty=p::list();auto*added=has(patch,"add")?get(patch,"add"):empty.get();auto*updates=has(patch,"update")?get(patch,"update"):empty.get();auto*removed=has(patch,"remove")?get(patch,"remove"):empty.get();auto additions=indexRows(added);require(PyList_Check(updates)&&PyList_Check(removed),"Invalid snapshot actor operations");auto indexed=indexRows(previous);p::Ref result(PyDict_Copy(indexed.get())),touched(PySet_New(nullptr));
    for(Py_ssize_t i=0;i<PyList_GET_SIZE(removed);++i){auto*id=PyList_GET_ITEM(removed,i);require(integer(id)&&PySet_Contains(touched.get(),id)==0&&PyDict_Contains(indexed.get(),id)==1,"Invalid snapshot actor removal");require(PySet_Add(touched.get(),id)==0,"Snapshot allocation failed");remove(result.get(),id);}
    Py_ssize_t n=0;PyObject*id=nullptr,*v=nullptr;while(PyDict_Next(additions.get(),&n,&id,&v)){require(PySet_Contains(touched.get(),id)==0&&PyDict_Contains(indexed.get(),id)==0,"Conflicting snapshot actor addition");require(PySet_Add(touched.get(),id)==0,"Snapshot allocation failed");}
    for(Py_ssize_t i=0;i<PyList_GET_SIZE(updates);++i){auto*u=PyList_GET_ITEM(updates,i);require(allowedKeys(u,{"id","set","remove"}),"Invalid snapshot actor update");auto*actor=get(u,"id");require(integer(actor)&&PySet_Contains(touched.get(),actor)==0&&PyDict_Contains(indexed.get(),actor)==1,"Conflicting snapshot actor update");require(PySet_Add(touched.get(),actor)==0,"Snapshot allocation failed");auto empty_dict=p::dict();auto*changed=has(u,"set")?get(u,"set"):empty_dict.get();auto*deleted=has(u,"remove")?get(u,"remove"):empty.get();require(PyDict_Check(changed)&&!has(changed,"id")&&PyList_Check(deleted),"Invalid snapshot field operations");p::Ref deleted_seen(PySet_New(nullptr));
        Py_ssize_t cursor=0;PyObject*k=nullptr,*value=nullptr;while(PyDict_Next(changed,&cursor,&k,&value))require(string(k),"Invalid snapshot field operations");
        p::Ref after(PyDict_Copy(PyDict_GetItem(indexed.get(),actor)));
        for(Py_ssize_t j=0;j<PyList_GET_SIZE(deleted);++j){auto*key=PyList_GET_ITEM(deleted,j);require(string(key)&&p::string(key)!="id"&&PySet_Contains(deleted_seen.get(),key)==0&&PyDict_Contains(changed,key)==0,"Invalid snapshot field operations");require(PySet_Add(deleted_seen.get(),key)==0,"Snapshot allocation failed");remove(after.get(),key);}
        cursor=0;while(PyDict_Next(changed,&cursor,&k,&value))set(after.get(),k,clone(value));ownStatic(after.get());set(result.get(),actor,after);
    }
    auto rows=p::list();for(Py_ssize_t i=0;i<PyList_GET_SIZE(previous);++i){auto*row=PyList_GET_ITEM(previous,i);auto*current=PyDict_GetItem(result.get(),get(row,"id"));if(current)p::append(rows.get(),p::retain(current));}
    for(Py_ssize_t i=0;i<PyList_GET_SIZE(added);++i){auto row=clone(PyList_GET_ITEM(added,i));ownStatic(row.get());p::append(rows.get(),row);}return rows;
}
PyObject* failure(const std::exception&e){PyErr_Clear();PyErr_SetString(error_type?error_type:PyExc_ValueError,e.what());return nullptr;}

// These conversions deliberately preserve the fixed client's numeric-string
// admission rules. Booleans are rejected even though Python subclasses int.
p::Ref exactInt(PyObject*v){require(!PyBool_Check(v),"Invalid runtime integer");if(integer(v))return p::retain(v);p::Ref parsed(PyNumber_Long(v));p::Ref real(PyNumber_Float(v));require(equal(real.get(),parsed.get()),"Invalid runtime integer");return parsed;}
p::Ref finiteFloat(PyObject*v){require(!PyBool_Check(v),"Invalid runtime number");p::Ref out(PyNumber_Float(v));require(std::isfinite(PyFloat_AS_DOUBLE(out.get())),"Invalid runtime number");return out;}
p::Ref equipment(PyObject*state,PyObject*contracts,PyObject*previous){
    require(PyDict_Check(state),"Invalid equipment actor");auto*items=get(state,"equipment_states");if(!p::flag(items))return p::retain(state);auto projected=p::list();bool changed=false;auto*old=get(previous,"equipment_states");const bool old_seq=PyList_Check(old)||PyTuple_Check(old);const bool seq=PyList_Check(items)||PyTuple_Check(items);require(seq,"Invalid equipment state list");bool same=previous!=Py_None;for(const char*k:{"id","vehicle","team","slot"})same=same&&equal(get(state,k),get(previous,k));
    for(Py_ssize_t i=0;i<PySequence_Size(items);++i){auto item=p::item(items,i);require(PyDict_Check(item.get()),"Bot equipment state is not an object");auto*contract=get(item.get(),"equipment");if(PyDict_Check(contract)){p::append(projected.get(),item);continue;}
        bool available=false;Py_ssize_t index=-1;
#if PY_MAJOR_VERSION<3
        const bool compact_integer=PyInt_Check(contract)&&!PyBool_Check(contract);
#else
        const bool compact_integer=PyLong_Check(contract)&&!PyBool_Check(contract);
#endif
        if(compact_integer&&(PyList_Check(contracts)||PyTuple_Check(contracts))&&PySequence_Size(contracts)<=90){
#if PY_MAJOR_VERSION<3
            index=PyInt_AS_LONG(contract);
#else
            index=PyLong_AsSsize_t(contract);
#endif
            if(PyErr_Occurred())PyErr_Clear();else available=index>=0&&index<PySequence_Size(contracts);}
        PyObject*selected=nullptr;
        if(available){selected=PyList_Check(contracts)?PyList_GET_ITEM(contracts,index):PyTuple_GET_ITEM(contracts,index);available=PyDict_Check(selected);}
        if(!available&&same&&old_seq&&PySequence_Size(old)==PySequence_Size(items)){auto*row=PyList_Check(old)?PyList_GET_ITEM(old,i):PyTuple_GET_ITEM(old,i);auto*c=get(row,"equipment");if(PyDict_Check(c)){selected=c;available=true;}}
        require(available,"Bot equipment contract is unavailable");p::Ref restored(PyDict_Copy(selected));auto*tags=get(restored.get(),"tags");if(PyList_Check(tags))p::set(restored.get(),"tags",p::Ref(PySequence_List(tags)));p::Ref value(PyDict_Copy(item.get()));p::set(value.get(),"equipment",restored);p::append(projected.get(),value);changed=true;
    }
    if(!changed)return p::retain(state);p::Ref out(PyDict_Copy(state));p::set(out.get(),"equipment_states",projected);return out;
}
double roundSix(double v){
    // CPython 2.7's correctly rounded decimal operation is a C builtin, not a
    // gameplay callback. Keep its binary halfway behavior exactly.
    if(!builtin_round){
#if PY_MAJOR_VERSION<3
        p::Ref module(PyImport_ImportModule("__builtin__"));
#else
        p::Ref module(PyImport_ImportModule("builtins"));
#endif
        auto fn=p::value(module.get(),"round");require(PyCFunction_Check(fn.get()),"Builtin round was replaced");builtin_round=fn.release();
    }
    p::Ref out(PyObject_CallFunction(builtin_round,const_cast<char*>("di"),v,6));return PyFloat_AsDouble(out.get());
}
p::Ref canonical(PyObject*value){
    require(PyDict_Check(value),"Invalid runtime row");p::Ref out(PyDict_Copy(value));
    static const std::map<std::string,double> bounds={{"x",2000.},{"y",1000.},{"z",2000.},{"pitch",3.14159265358979323846},{"roll",3.14159265358979323846},{"gun_pitch",1.2},{"speed",200.}};
    for(const char*k:{"x","y","z","yaw","pitch","roll","aim_yaw","turret_yaw","gun_pitch","speed"})if(has(out.get(),k)){auto parsed=finiteFloat(get(out.get(),k));auto bound=bounds.find(k);require(bound==bounds.end()||std::abs(PyFloat_AS_DOUBLE(parsed.get()))<=bound->second,"Runtime pose exceeds bounds");p::set(out.get(),k,parsed);}
    for(const char*k:{"id","team","slot","health","max_health","fire_seq","shell_index","movement_dir","rotation_dir","stun_end_server_time_ms"})if(has(out.get(),k))p::set(out.get(),k,exactInt(get(out.get(),k)));
    auto*factors=get(out.get(),"stun_factors");if(p::flag(factors)){static const std::set<std::string> up={"reload","aim_time","bloom_move","bloom_rotation","bloom_turret","dispersion"};require(allowedKeys(factors,{"mobility","traverse","turret_speed","vision","speed","reload","aim_time","bloom_move","bloom_rotation","bloom_turret","dispersion"})&&PyDict_Size(factors)==11,"Invalid stun factor fields");auto normalized=p::dict();Py_ssize_t i=0;PyObject*k=nullptr,*v=nullptr;while(PyDict_Next(factors,&i,&k,&v)){require(integer(v)||PyFloat_Check(v),"Invalid stun number");const double n=PyFloat_AsDouble(v);const bool increasing=up.count(p::string(k))!=0;require(std::isfinite(n)&&n>=(increasing?1.:0.)&&n<=(increasing?100.:1.),"Invalid stun factor");set(normalized.get(),k,p::number(roundSix(n)));}p::set(out.get(),"stun_factors",normalized);}
    if(has(out.get(),"velocity")){auto*v=get(out.get(),"velocity");require((PyList_Check(v)||PyTuple_Check(v))&&PySequence_Size(v)>=3,"Invalid runtime velocity");p::Ref tuple(PyTuple_New(3));for(int i=0;i<3;++i){auto item=p::item(v,i);PyTuple_SET_ITEM(tuple.get(),i,finiteFloat(item.get()).release());}p::set(out.get(),"velocity",tuple);}return out;
}
}

PyObject* configure(PyObject*,PyObject*args){try{PyObject*d=nullptr,*l=nullptr;if(!PyArg_ParseTuple(args,"OO",&d,&l))return nullptr;require(PyType_Check(d)&&PyType_Check(l),"Static snapshot owners must be types");Py_INCREF(d);Py_INCREF(l);Py_XDECREF(static_dict);Py_XDECREF(static_list);static_dict=d;static_list=l;Py_RETURN_NONE;}catch(const std::exception&e){return failure(e);}}
PyObject* decode(PyObject*,PyObject*args){try{
    PyObject*wire=nullptr,*base=nullptr,*pending_arg=Py_False;if(!PyArg_ParseTuple(args,"OO|O",&wire,&base,&pending_arg))return nullptr;const bool pending=p::flag(pending_arg);require(PyDict_Check(wire)&&string(get(wire,"type"))&&p::string(get(wire,"type"))=="snapshot","Invalid snapshot envelope");Baseline*before=nullptr;if(base!=Py_None){before=static_cast<Baseline*>(PyCapsule_GetPointer(base,baseline_name));require(before!=nullptr,"Invalid snapshot baseline");}
    if(!has(wire,"snapshot_delta")&&!has(wire,"snapshot_seq")&&!has(wire,"snapshot_base_seq")){auto full=clone(wire);return PyTuple_Pack(2,full.get(),Py_None);}
    auto*marker=get(wire,"snapshot_delta");require(PyBool_Check(marker),"Invalid snapshot delta marker");auto seq=sequence(get(wire,"snapshot_seq"));if(before)require(PyObject_RichCompareBool(seq.get(),before->sequence.get(),Py_GT)==1,"Snapshot sequence did not advance");auto scope=lineage(wire),actors=p::dict();
    if(marker==Py_True){auto wanted=sequence(get(wire,"snapshot_base_seq"));require(before&&equal(wanted.get(),before->sequence.get())&&equal(scope.get(),before->lineage.get()),"Snapshot delta baseline mismatch");for(const char*name:{"players","bots"})p::set(actors.get(),name,applyRows(get(wire,name),get(before->actors.get(),name)));}
    else {require(!has(wire,"snapshot_base_seq"),"Full snapshot has a delta baseline");for(const char*name:{"players","bots"}){auto indexed=indexRows(get(wire,name));(void)indexed;auto rows=clone(get(wire,name));for(Py_ssize_t i=0;i<PyList_GET_SIZE(rows.get());++i)ownStatic(PyList_GET_ITEM(rows.get(),i));p::set(actors.get(),name,rows);}}
    auto full=p::dict();Py_ssize_t n=0;PyObject*k=nullptr,*v=nullptr;while(PyDict_Next(wire,&n,&k,&v)){const auto name=p::string(k);if(name!="players"&&name!="bots"&&name!="snapshot_delta"&&name!="snapshot_seq"&&name!="snapshot_base_seq")set(full.get(),k,clone(v));}
    if(pending){auto held=p::Ref(PyCapsule_New(new p::Ref(p::retain(actors.get())),pending_name,pendingDestroy));set(full.get(),pending_key,held);}else for(const char*name:{"players","bots"})p::set(full.get(),name,clone(get(actors.get(),name)));
    p::Ref next(PyCapsule_New(new Baseline(std::move(seq),std::move(scope),std::move(actors)),baseline_name,baselineDestroy));return PyTuple_Pack(2,full.get(),next.get());
}catch(const std::exception&e){return failure(e);}}
PyObject* materialize(PyObject*,PyObject*message){try{
    if(!PyDict_Check(message)||!pending_key||PyDict_Contains(message,pending_key)==0)return p::retain(message).release();auto*c=PyDict_GetItem(message,pending_key);auto*actors=static_cast<p::Ref*>(PyCapsule_GetPointer(c,pending_name));require(actors!=nullptr,"Invalid pending snapshot actor holder");p::Ref out(PyDict_Copy(message));remove(out.get(),pending_key);for(const char*name:{"players","bots"})p::set(out.get(),name,clone(get(actors->get(),name),true));return out.release();
}catch(const std::exception&e){return failure(e);}}
PyObject* canonicalRow(PyObject*,PyObject*value){try{return canonical(value).release();}catch(const std::exception&){PyErr_Clear();Py_RETURN_NONE;}}
PyObject* decodeEquipment(PyObject*,PyObject*args){try{PyObject*state=nullptr,*contracts=nullptr,*previous=Py_None;if(!PyArg_ParseTuple(args,"OO|O",&state,&contracts,&previous))return nullptr;return equipment(state,contracts,previous).release();}catch(const std::exception&e){return failure(e);}}
PyObject* canonicalRows(PyObject*,PyObject*args){try{
    PyObject*client=nullptr,*rows=nullptr,*message=Py_None;const char*field=nullptr;if(!PyArg_ParseTuple(args,"OOs|O",&client,&rows,&field,&message))return nullptr;auto previous=p::value(client,"last_snapshot"),indexed=p::dict();const bool same_round=message==Py_None||(PyDict_Check(previous.get())&&equal(get(previous.get(),"round_id"),get(message,"round_id")));const bool same_scope=same_round&&(message==Py_None||equal(get(previous.get(),"authority_epoch"),get(message,"authority_epoch")));
    auto*old=get(previous.get(),field);if(same_round&&(PyList_Check(old)||PyTuple_Check(old)))for(Py_ssize_t i=0;i<PySequence_Size(old);++i){auto row=p::item(old,i);if(!PyDict_Check(row.get()))continue;try{auto id=exactInt(get(row.get(),"id"));set(indexed.get(),id.get(),row);}catch(const std::exception&){PyErr_Clear();}}
    auto out=p::list();long retained=0;for(Py_ssize_t i=0;i<PySequence_Size(rows);++i){auto raw=p::item(rows,i);PyObject*prior=nullptr;try{auto id=exactInt(get(raw.get(),"id"));prior=PyDict_GetItem(indexed.get(),id.get());}catch(const std::exception&){PyErr_Clear();}
        try{auto restored=std::string(field)=="bots"?equipment(raw.get(),get(message,"bot_equipment_contracts"),same_scope&&prior?prior:Py_None):p::retain(raw.get());auto row=canonical(restored.get());p::append(out.get(),row);}catch(const std::exception&){PyErr_Clear();if(prior)p::append(out.get(),p::Ref(PyDict_Copy(prior)));++retained;}}
    return Py_BuildValue("Nl",out.release(),retained);
}catch(const std::exception&e){return failure(e);}}
PyMethodDef* methods(){static PyMethodDef value[]={
    {"configure",configure,METH_VARARGS,"Install cold immutable snapshot types."},
    {"decode",decode,METH_VARARGS,"Decode an ordered snapshot transaction."},
    {"materialize",materialize,METH_O,"Detach the selected actor version."},
    {"canonical_row",canonicalRow,METH_O,"Validate one actor without Python scalar loops."},
    {"canonical_rows",canonicalRows,METH_VARARGS,"Restore equipment and retain invalid actor rows."},
    {"decode_equipment",decodeEquipment,METH_VARARGS,"Restore one immutable equipment contract."},
    {nullptr,nullptr,0,nullptr}};return value;}
void initialize(PyObject*module){if(!pending_key)pending_key=PyObject_CallObject(reinterpret_cast<PyObject*>(&PyBaseObject_Type),nullptr);if(!error_type)error_type=PyErr_NewException(const_cast<char*>("offline_client_transport_native.SnapshotDeltaError"),PyExc_ValueError,nullptr);Py_INCREF(error_type);PyModule_AddObject(module,"SnapshotDeltaError",error_type);}
}
