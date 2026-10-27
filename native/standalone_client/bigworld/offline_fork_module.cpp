#include "pch.hpp"
#include "pyscript/script.hpp"
#include "cstdmf/main_loop_task.hpp"
#include "stock_compiled_space_mapping.hpp"
#include "stock_offline_platform.hpp"
#include "offline_fork_methods.hpp"

extern "C" void __cdecl initoffline_math_batch_native(void);

BW_BEGIN_NAMESPACE
namespace
{
PyObject * applyMapping( PyObject *, PyObject * )
{
    return PyInt_FromLong( CompiledSpace::applyStockStandardGameplayMapping() );
}

PyObject * restoreMapping( PyObject *, PyObject * )
{
    return PyInt_FromLong( CompiledSpace::restoreStockStandardGameplayMapping() );
}

PyMethodDef runtimeMethods[7] = {};

bool initRuntimeModule()
{
    const PyMethodDef * platform = stockOfflinePlatformMethods();
    for (size_t index = 0; index < 4; ++index)
    {
        if (!platform[index].ml_name) return false;
        runtimeMethods[index] = platform[index];
    }
    if (platform[4].ml_name) return false;
    const PyMethodDef apply = { "apply_standard_gameplay_mask", applyMapping,
        METH_NOARGS, "Capture standard gameplay visibility for local geometry mappings." };
    const PyMethodDef restore = { "restore_standard_gameplay_mask", restoreMapping,
        METH_NOARGS, "Restore all-gameplay admission for subsequent local mappings." };
    runtimeMethods[4] = apply;
    runtimeMethods[5] = restore;
    PyObject * module = Py_InitModule( "offline_instance_guard_native", runtimeMethods );
    return module && PyModule_AddStringConstant( module, "_wot_offline_fork_revision",
        "144619f6eb2925f8da4b13637bf65b8550fc898f" ) == 0;
}

class OfflineForkModule : public Script::InitTimeJob
{
public:
    OfflineForkModule() : Script::InitTimeJob( 1 ) {}

    void init() override
    {
        // Script::init has created the one embedded interpreter; personality
        // imports have not run yet. Never load the old EXE-address sidecar.
        initoffline_math_batch_native();
        PyObject * module = PyDict_GetItemString( PyImport_GetModuleDict(),
            "offline_math_batch_native" );
        bool valid = module != NULL && !PyErr_Occurred();
        for (size_t index = 0; valid && index <
            sizeof(OFFLINE_FORK_METHODS) / sizeof(OFFLINE_FORK_METHODS[0]); ++index)
        {
            PyObject * method = PyObject_GetAttrString( module, OFFLINE_FORK_METHODS[index] );
            valid = method && PyCallable_Check( method );
            Py_XDECREF( method );
        }
        if (valid)
        {
            valid = PyModule_AddStringConstant( module, "_wot_offline_fork_revision",
                "144619f6eb2925f8da4b13637bf65b8550fc898f" ) == 0;
        }
        if (!valid)
        {
            PyErr_Print();
            CRITICAL_MSG( "The pinned offline numerical module could not be initialised\n" );
        }
        if (!initRuntimeModule())
        {
            PyErr_Print();
            CRITICAL_MSG( "The independent offline runtime module could not be initialised\n" );
        }
    }
};

OfflineForkModule s_offlineForkModule;

class OfflinePlatformLifetime : public MainLoopTask
{
public:
    OfflinePlatformLifetime()
    {
        // MainLoopTasks::fini traverses its actual order in reverse. Run after
        // ScriptApp::fini and Py_Finalize, when no Python consumer can reenter.
        // FiniTimeJob's documented negative-rung ordering is not implemented.
        MainLoopTasks::root().add( this, "OfflinePlatformLifetime", "<Script", NULL );
    }
    bool init() override { return true; }
    void fini() override
    {
        finiStockOfflinePlatform();
        CompiledSpace::restoreStockStandardGameplayMapping();
    }
};

OfflinePlatformLifetime s_offlinePlatformLifetime;
}
BW_END_NAMESPACE
