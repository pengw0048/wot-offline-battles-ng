#include "pch.hpp"
#include "pyscript/script.hpp"
#include "offline_fork_methods.hpp"

extern "C" void __cdecl initoffline_math_batch_native(void);

BW_BEGIN_NAMESPACE
namespace
{
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
    }
};

OfflineForkModule s_offlineForkModule;
}
BW_END_NAMESPACE
