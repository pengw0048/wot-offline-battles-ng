// Included by the adapted numerical TU after its real WorldFrontier owner.
// One rotation owns independent, synchronously reentrant world frontiers.
namespace stock_rotation_binding {
namespace r = stock_motion_rotation;
using Object = engine_query::Object;
Object takeError()
{
    if(!PyErr_Occurred())return Object();
    PyObject *type=nullptr,*value=nullptr,*traceback=nullptr;
    PyErr_Fetch(&type,&value,&traceback);PyErr_NormalizeException(&type,&value,&traceback);
    Py_XDECREF(type);Py_XDECREF(traceback);
    if(!value)value=PyObject_CallFunction(PyExc_RuntimeError,const_cast<char *>("s"),"Native world stage failed after engine dispatch");
    return Object(value);
}
r::Snapshot snapshot(PyObject *object)
{
    double a[22];world_array(object,a,22);r::Snapshot s{};
    std::copy(a,a+3,s.position.begin());std::copy(a+3,a+6,s.body.minimum.begin());
    std::copy(a+6,a+9,s.body.maximum.begin());
    s.start_yaw=a[9];s.end_yaw=a[10];s.pitch=a[11];s.roll=a[12];s.pivot_offset=a[13];
    s.translation={{a[14],a[15]}};s.max_angle_step=a[16];s.drivable_gradient=a[17];
    if(a[18]<1. || a[18]>1024. || std::floor(a[18])!=a[18])throw Unsupported();
    s.max_steps=static_cast<std::uint32_t>(a[18]);
    for(unsigned i=19;i<22;++i)if(a[i]!=0. && a[i]!=1.)throw Unsupported();
    s.include_static=a[19]!=0.;s.replacement_reader_available=a[20]!=0.;s.replacement_active=a[21]!=0.;
    r::validateSnapshot(s);return s;
}
Object worldRow(const WorldSnapshot &s)
{
    OwnedTuple row(19);
    const double a[19]={s.position[0],s.position[1],s.position[2],s.yaw,s.speed,s.dt,s.pitch,s.roll,s.motion_yaw,
        s.bounds[0],s.bounds[1],s.bounds[2],s.bounds[3],s.heights[0],s.heights[1],double(s.passive),
        double(s.airborne),double(s.exact_footprint),double(s.supplied_departing)};
    for(unsigned i=0;i<19;++i)row.set(i,float_new(a[i]));
    return Object(row.release());
}
Object bodyRow(const WorldSnapshot &s)
{
    return Object(Py_BuildValue("((ddd)(ddd))",s.bounds[0],s.heights[0],-s.bounds[2],s.bounds[1],s.heights[1],s.bounds[3]));
}
class Engine final:public r::WorldEngine {
    PyObject *capabilities_,*tuning_,*factory_;
    Object query_,report_;
public:
    Object blocking_trace;
    Engine(PyObject *caps,PyObject *tuning,PyObject *factory,PyObject *report):
        capabilities_(caps),tuning_(tuning),factory_(factory),
        query_(Object::borrow(PyMethod_GET_SELF(items(caps)[7]))),report_(Object::borrow(report)) {}
    r::WorldResult run(const WorldSnapshot &s,r::DeparturePolicy *policy)override
    {
        Object trace(PyDict_New());r::WorldResult result;Object error;
        try {
            auto row=worldRow(s),bbox=bodyRow(s);
            Object prepared(PyObject_CallFunctionObjArgs(factory_,row.get(),bbox.get(),NULL));
            require_sequence(prepared.get(),2,true);
            if(!PyCallable_Check(items(prepared.get())[0]) || !PyDict_CheckExact(items(prepared.get())[1]))throw Unsupported();
            trace=Object::borrow(items(prepared.get())[1]);
            sim_motion_bridge::WorldFrontier frontier(capabilities_,tuning_,s,policy,true);
            NativeWorldQuery context={items(prepared.get())[0],&frontier,{},false};
            result.status=wot_world_run(&s,native_world_query_dispatch,&context);
            if(!frontier.finish())result.status=-2;
            error=takeError();
        } catch(...) {result.status=-2;error=takeError();}
        // Same per-world failure consumption as native_math.world_run, before
        // the fixed wrapper reports/contains that one world. Do not replay it.
        PyObject *checked=PyObject_CallMethod(query_.get(),const_cast<char *>("raise_failure"),NULL);
        if(checked)Py_DECREF(checked);
        else {auto remembered=takeError();if(!error.get())error=std::move(remembered);}
        if(error.get() || result.status<0) {
            result.status=-2;
            if(!error.get())error=Object(PyObject_CallFunction(PyExc_RuntimeError,const_cast<char *>("s"),"Native world stage failed after engine dispatch"));
            PyObject *reported=PyObject_CallFunctionObjArgs(report_.get(),error.get(),NULL);
            if(reported)Py_DECREF(reported);else PyErr_Clear();
            PyObject *rawText=PyObject_Str(error.get());
            if(!rawText) {PyErr_Clear();rawText=PyString_FromString("Native world error");}
            Object text(rawText);
            if(PyDict_SetItemString(trace.get(),"error",text.get())<0)throw PythonAllocationFailed();
            Object reason(PyString_FromString("native_world_error"));
            if(PyDict_SetItemString(trace.get(),"reason",reason.get())<0)throw PythonAllocationFailed();
        }
        result.trace.snapshot=s;
        PyObject *point=PyDict_GetItemString(trace.get(),"hit"),*normal=PyDict_GetItemString(trace.get(),"normal");
        if(point && normal) {
            // Actual trace vectors/tuples retain the original f32 boundary.
            world_array(point,result.trace.response.point,3);world_array(normal,result.trace.response.normal,3);
            result.trace.present=true;
        }
        if(result.status!=0)blocking_trace=trace;
        return result;
    }
};
PyObject *run(PyObject *,PyObject *args)
{
    bool entered=false;
    try {
        require_sequence(args,4,true);auto **a=items(args);const auto input=snapshot(a[0]);
        if(!PyCallable_Check(a[3]))throw Unsupported();
        require_sequence(a[2],3,true);
        const auto budget=identity(items(a[2])[0]);
        if(budget<0 || budget>1024 || finite_number(items(a[2])[1])<=0. || finite_number(items(a[2])[2])<=0.)throw Unsupported();
        // Full-operation fallback is permitted only before a world factory or
        // business callback runs. Later unsupported rays keep this same kernel.
        auto *proof=stock_offline_query::begin(a[1]);
        if(!proof)return fallback();
        stock_offline_query::end(proof);
        Object module(PyImport_ImportModule("gui.mods.offline_lan_0922.native_math"));
        Object report(PyObject_GetAttrString(module.get(),"report_world_failure"));
        if(!PyCallable_Check(report.get()))throw Unsupported();
        Engine engine(a[1],a[2],a[3],report.get());entered=true;
        const auto value=r::run(input,engine);
        PyObject *trace=value.clear || !engine.blocking_trace.get()?Py_None:engine.blocking_trace.get();
        return Py_BuildValue("(NiiO)",PyBool_FromLong(value.clear),int(value.query_failures),int(value.blocking_failure),trace);
    } catch(const PythonAllocationFailed &) {return NULL;}
      catch(...) {
        if(!entered){PyErr_Clear();return fallback();}
        if(!PyErr_Occurred())PyErr_SetString(PyExc_RuntimeError,"Native rotation failed after entry; no replay is permitted");
        return NULL;
    }
}
PyMethodDef methods[]={{"run",run,METH_VARARGS,"Run one complete prepared rotation; no operation replay after entry."},{NULL,NULL,0,NULL}};
bool initialize()
{
    PyObject *old=PyDict_GetItemString(PyImport_GetModuleDict(),"offline_rotation_world_native");
    if(old) {
        PyObject *raw=PyObject_GetAttrString(old,"run");if(!raw)return false;
        Object entry(raw);
        if(PyCFunction_Check(entry.get()) && PyCFunction_GET_FUNCTION(entry.get())==run)return true;
        PyErr_SetString(PyExc_RuntimeError,"Native rotation module identity changed");return false;
    }
    return Py_InitModule3("offline_rotation_world_native",methods,"Independent synchronous rotation query prototype.")!=NULL;
}
}
