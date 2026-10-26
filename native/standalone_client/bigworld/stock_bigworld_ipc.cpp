#ifndef WOT_STOCK_BIGWORLD_IPC_CORE_ONLY
#include "pch.hpp"
#include "pyscript/script.hpp"
#endif
#include "stock_bigworld_ipc.hpp"
#include "stock_numeric_format.hpp"

#include <windows.h>

namespace wot
{
StockIpcMutex::~StockIpcMutex()
{
    std::uint32_t error;
    this->close( error );
}

bool StockIpcMutex::close( std::uint32_t & error )
{
    error = 0;
    if (!handle_)
        return true;
    const bool closed = CloseHandle( handle_ ) != FALSE;
    if (!closed)
        error = GetLastError();
    handle_ = nullptr;
    return closed;
}

bool StockIpcMutex::create( const char * name, std::uint32_t & error )
{
    handle_ = CreateMutexA( NULL, FALSE, name );
    error = handle_ ? 0 : GetLastError();
    return handle_ != nullptr;
}

StockIpcMutex::WaitResult StockIpcMutex::acquire( std::uint32_t milliseconds )
{
    const DWORD result = WaitForSingleObject( handle_, milliseconds );
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
        return WAIT_RESULT_ACQUIRED;
    return result == WAIT_FAILED ? WAIT_RESULT_FAILED : WAIT_RESULT_TIMEOUT;
}

bool StockIpcMutex::release( std::uint32_t & error )
{
    const bool released = ReleaseMutex( handle_ ) != FALSE;
    error = released ? 0 : GetLastError();
    return released;
}

std::uint32_t StockIpcMutex::timeoutMilliseconds( float seconds )
{
    return static_cast<std::uint32_t>( static_cast<std::uint64_t>(
        stock_numeric_format::signedInteger( static_cast<double>( seconds ) * 1000.0 ) ) );
}

bool stockIpcErrorMessage( std::uint32_t error, std::string & message )
{
    char text[0x400];
    if (!error || !FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0,
            text, sizeof( text ), NULL ))
        return false;
    message = text;
    return true;
}
}

#ifndef WOT_STOCK_BIGWORLD_IPC_CORE_ONLY
BW_BEGIN_NAMESPACE
int StockBigWorldIpc_token = 1;

namespace
{
// The original leaves this SystemError pending and still returns None/False;
// CPython 2.7 would then surface it at an unrelated later point. Raise it here.
bool raiseSystemError( const char * operation, std::uint32_t error )
{
    std::string text;
    if (!wot::stockIpcErrorMessage( error, text ))
        return false;
    PyErr_Format( PyExc_SystemError, "%s: %s", operation, text.c_str() );
    return true;
}
}

class PyMutex : public PyObjectPlus
{
    Py_Header( PyMutex, PyObjectPlus )
public:
    PyMutex() : PyObjectPlus( &s_type_ ) {}
    PY_METHOD_DECLARE( py_create )
    PY_METHOD_DECLARE( py_close )
    PY_METHOD_DECLARE( py_acquire )
    PY_METHOD_DECLARE( py_release )
    PY_FACTORY_DECLARE()
private:
    wot::StockIpcMutex mutex_;
};

PyObject * PyMutex::pyNew( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
    {
        PyErr_SetString( PyExc_TypeError, "PyMutex() takes no arguments." );
        return NULL;
    }
    return new PyMutex();
}

PyObject * PyMutex::py_create( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
    {
        PyErr_SetString( PyExc_TypeError, "create() expects 1 argument of type BW::string" );
        return NULL;
    }
    BW::string name;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), name, "() argument 1" ) != 0)
        return NULL;
    std::uint32_t error;
    bool raised = !mutex_.close( error ) && raiseSystemError( "PyMutex.close", error );
    if (!mutex_.create( name.c_str(), error ))
        raised = raiseSystemError( "PyMutex.create", error ) || raised;
    if (raised)
        return NULL;
    Py_RETURN_NONE;
}

PyObject * PyMutex::py_close( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
    {
        PyErr_SetString( PyExc_TypeError, "close() takes no arguments." );
        return NULL;
    }
    std::uint32_t error;
    if (!mutex_.close( error ) && raiseSystemError( "PyMutex.close", error ))
        return NULL;
    Py_RETURN_NONE;
}

PyObject * PyMutex::py_acquire( PyObject * args )
{
    if (PyTuple_Size( args ) != 1)
    {
        PyErr_SetString( PyExc_TypeError, "acquire() expects 1 argument of type float" );
        return NULL;
    }
    float seconds = 0.f;
    if (Script::setData( PyTuple_GET_ITEM( args, 0 ), seconds, "() argument 1" ) != 0)
        return NULL;
    // The original waits while holding the GIL; it does not release it.
    switch (mutex_.acquire( wot::StockIpcMutex::timeoutMilliseconds( seconds ) ))
    {
    case wot::StockIpcMutex::WAIT_RESULT_ACQUIRED:
        Py_RETURN_TRUE;
    case wot::StockIpcMutex::WAIT_RESULT_FAILED:
        PyErr_SetString( PyExc_SystemError, "PyMutex.acquire: Can't acquire mutex" );
        return NULL;
    default:
        Py_RETURN_FALSE;
    }
}

PyObject * PyMutex::py_release( PyObject * args )
{
    if (PyTuple_Size( args ) != 0)
    {
        PyErr_SetString( PyExc_TypeError, "release() takes no arguments." );
        return NULL;
    }
    std::uint32_t error;
    if (!mutex_.release( error ) && raiseSystemError( "PyMutex.release", error ))
        return NULL;
    Py_RETURN_NONE;
}

PY_TYPEOBJECT( PyMutex )
PY_BEGIN_METHODS( PyMutex )
    PY_METHOD( create )
    PY_METHOD( close )
    PY_METHOD( acquire )
    PY_METHOD( release )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyMutex )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyMutex, "Mutex", BigWorldIPC )
BW_END_NAMESPACE
#endif
