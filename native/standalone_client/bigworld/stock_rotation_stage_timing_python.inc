// Independent diagnostic controls; serialization runs only outside an entry.
namespace stock_rotation_profile_binding {
namespace t = stock_rotation_timing;
using Object = engine_query::Object;
Object number(std::uint64_t value) {return Object(PyLong_FromUnsignedLongLong(value));}
void set(PyObject *dict,const char *key,Object value) {
    if(PyDict_SetItemString(dict,key,value.get())<0)throw PythonAllocationFailed();
}
Object snapshotValue(const t::Snapshot &value)
{
    Object out(PyDict_New()),stages(PyDict_New()),counts(PyDict_New()),errors(PyDict_New());
    std::uint64_t accounted=0,error_count=0;
    for(unsigned i=0;i<t::STAGE_COUNT;++i) {
        const auto &row=value.stages[i];accounted+=row.self_ns;
        Object item(Py_BuildValue("{sK,sK,sK}","calls",static_cast<unsigned long long>(row.calls),
            "inclusive_ns",static_cast<unsigned long long>(row.inclusive_ns),"self_ns",static_cast<unsigned long long>(row.self_ns)));
        set(stages.get(),t::name(static_cast<t::Stage>(i)),std::move(item));
    }
    for(unsigned i=0;i<t::COUNTER_COUNT;++i)set(counts.get(),t::name(static_cast<t::Counter>(i)),number(value.counters[i]));
    for(unsigned i=0;i<t::ERROR_COUNT;++i){error_count+=value.errors[i];set(errors.get(),t::name(static_cast<t::Error>(i)),number(value.errors[i]));}
    set(out.get(),"schema",number(1));set(out.get(),"generation",number(value.generation));
    set(out.get(),"active",Object(PyBool_FromLong(value.active)));set(out.get(),"depth",number(value.depth));
    set(out.get(),"max_depth",number(value.max_depth));set(out.get(),"max_entry_depth",number(value.max_entry_depth));
    set(out.get(),"root_count",number(value.root_count));set(out.get(),"root_ns",number(value.root_ns));
    set(out.get(),"clock_calls",number(value.clock_calls));set(out.get(),"stages",std::move(stages));
    set(out.get(),"counters",std::move(counts));set(out.get(),"errors",std::move(errors));
    set(out.get(),"kernel_other_ns",number(value.stages[static_cast<unsigned>(t::Stage::Entry)].self_ns));
    set(out.get(),"accounted_ns",number(accounted));
    set(out.get(),"conservation_error_ns",Object(PyLong_FromLongLong(
        static_cast<long long>(value.root_ns)-static_cast<long long>(accounted))));
    set(out.get(),"valid",Object(PyBool_FromLong(!error_count && accounted==value.root_ns)));
    return out;
}
PyObject *start(PyObject *,PyObject *) {
    if(!t::start()){PyErr_SetString(PyExc_RuntimeError,"Rotation timing start requires an inactive owner thread at depth zero");return NULL;}
    t::Snapshot value;if(!t::snapshot(value))return NULL;return PyLong_FromUnsignedLongLong(value.generation);
}
PyObject *snapshot(PyObject *,PyObject *) {
    try {t::Snapshot value;if(!t::snapshot(value)){PyErr_SetString(PyExc_RuntimeError,"Rotation timing snapshot requires its owner thread at depth zero");return NULL;}
        auto result=snapshotValue(value);Py_INCREF(result.get());return result.get();}
    catch(...){if(!PyErr_Occurred())PyErr_NoMemory();return NULL;}
}
PyObject *stop(PyObject *,PyObject *) {
    if(!t::stop()){PyErr_SetString(PyExc_RuntimeError,"Rotation timing stop requires an active owner thread at depth zero");return NULL;}
    t::Snapshot value;if(!t::snapshot(value))return NULL;return PyLong_FromUnsignedLongLong(value.generation);
}
PyObject *calibrate(PyObject *,PyObject *arg) {
    try {const auto count=PyLong_AsUnsignedLong(arg);if(PyErr_Occurred())return NULL;
        if(!count || count>100000){PyErr_SetString(PyExc_ValueError,"Rotation timing calibration requires 1..100000 iterations");return NULL;}
        t::Calibration value;if(!t::calibrate(static_cast<unsigned>(count),value)){PyErr_SetString(PyExc_RuntimeError,"Rotation timing calibration requires an inactive owner thread at depth zero");return NULL;}
        Object out(Py_BuildValue("{sK,sK,sK,sK}","iterations",static_cast<unsigned long long>(value.iterations),
            "off_ns",static_cast<unsigned long long>(value.off_ns),"on_ns",static_cast<unsigned long long>(value.on_ns),
            "clock_pair_ns",static_cast<unsigned long long>(value.clock_pair_ns)));
        set(out.get(),"measured",snapshotValue(value.measured));Py_INCREF(out.get());return out.get();
    }catch(...){if(!PyErr_Occurred())PyErr_NoMemory();return NULL;}
}
}
