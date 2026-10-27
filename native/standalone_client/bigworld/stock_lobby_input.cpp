#include "pch.hpp"
#include "stock_lobby_input.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE

/**
 *	#1513 006f2e50: BigWorld.wg_getLangCode() -> str. LoginView polls it every
 *	0.1 s and forwards a changed value to as_setKeyboardLangS.
 */
PyObject * py_wg_getLangCode( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
        return Script::argCountError( "wg_getLangCode", 0, 0 );
    std::string code;
    if (!wot::stock_lobby_input::languageCode( code ))
    {
        // The original reads an unfilled buffer here; report the failed query instead.
        PyErr_Format( PyExc_RuntimeError, "wg_getLangCode: GetLocaleInfoW failed (%lu)",
            GetLastError() );
        return NULL;
    }
    return PyString_FromStringAndSize( code.data(), static_cast<Py_ssize_t>( code.size() ) );
}
PY_MODULE_FUNCTION( wg_getLangCode, BigWorld )

BW_END_NAMESPACE
