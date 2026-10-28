// CPython 2.7 BigWorld extension: patched public pyscript conversions and PY_AUTO
// macros, real DynamicDecalManager methods and production decal bindings.
// @...@ markers are filled by test_stock_render_pipeline_query.py.
#include <Python.h>
#define WOT_STOCK_DECAL_RESOURCES_PORTABLE
#include "stock_decal_resources.hpp"
#include <cassert>
#include <cstdarg>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
#define BW_STATIC_ASSERT( condition, message ) static_assert( condition, #message )
namespace BW { using string = std::string; template<class T> using vector = std::vector<T>; }
typedef int int32;
typedef unsigned uint;

class PyObjectPtr
{
public:
    enum Steal { STEAL_REFERENCE };
    PyObjectPtr() {}
    PyObjectPtr( PyObject * object, Steal ) : object_( object ) {}
    PyObjectPtr( const PyObjectPtr & ) = delete;
    PyObjectPtr & operator=( PyObjectPtr && other ) { std::swap( object_, other.object_ ); return *this; }
    ~PyObjectPtr() { Py_XDECREF( object_ ); }
    PyObject * get() const { return object_; }
private:
    PyObject * object_ = nullptr;
};

namespace Script
{
int setData( PyObject * pObject, int & rInt, const char * varName );
int setData( PyObject * pObject, double & rDouble, const char * varName );
int setData( PyObject * pObject, float & rFloat, const char * varName );
int setData( PyObject * pObject, BW::string & rString, const char * varName );
PyObject * getData( const bool data );
PyObject * getData( const int data );
PyObject * argCountError( const char * fn, int optas, int allas, ... );
template <class C> const char * zeroValueName();
template <> const char * zeroValueName<int>();
template <> const char * zeroValueName<float>();
template <class T> class IsValidRetData
{
public:
    static PyObject * getData( const T & data ) { return Script::getData( data ); }
};
template <class T> PyObject * getRetData( const T & data ) { return Script::IsValidRetData<T>::getData( data ); }
}
@SCRIPT@

// PyModuleMethodLink: the real one is an InitTimeJob that adds a PyMethodDef to the named module.
std::vector<PyMethodDef> & bigWorldMethods() { static std::vector<PyMethodDef> methods; return methods; }
struct PyModuleMethodLink
{
    PyModuleMethodLink( const char * module, const char * name, PyCFunction method, const char * doc )
    {
        assert( std::strcmp( module, "BigWorld" ) == 0 );
        bigWorldMethods().push_back( PyMethodDef{ name, method, METH_VARARGS, doc } );
    }
};
@MACROS@

// Renderer: the real singleton publishes m_type after init; deferred owns trace textures.
struct IRendererPipeline { enum EType { TYPE_DEFERRED_SHADING, TYPE_FORWARD_SHADING }; };
struct StockTraceTextureOwner
{
    std::vector<std::string> names;
    int textureIndex( const BW::string & name ) { names.push_back( name ); return int( names.size() - 1 ); }
};
class Renderer
{
public:
    static Renderer * current;
    static Renderer & instance() { assert( current ); return *current; }
    static Renderer * pInstance() { return current; }
    IRendererPipeline::EType pipelineType() const { assert( inited ); return type; }
    StockTraceTextureOwner * traceTextureOwner() { return type == IRendererPipeline::TYPE_DEFERRED_SHADING ? &traces : nullptr; }
    bool inited = true;
    IRendererPipeline::EType type = IRendererPipeline::TYPE_DEFERRED_SHADING;
    StockTraceTextureOwner traces;
};
Renderer * Renderer::current = nullptr;
struct PyStickerModel
{
    static float angle;
    static void criticalAngle( float radians ) { angle = radians; }
};
float PyStickerModel::angle = 0.f;
void clearStockDynamicDecals() {}
void clearStockVehicleTraceRenderer() {}

class DynamicDecalManager
{
    struct DecalGroup
    {
        float m_lifeTime;
        BW::string m_name;
        DecalGroup( const BW::string & name, float lifeTime ) : m_lifeTime( lifeTime ), m_name( name ) {}
    };
    typedef BW::vector<DecalGroup> DecalGroups;
    DecalGroups m_groups;
    typedef BW::vector<BW::string> TextureMap;
    TextureMap m_textures;
public:
    static DynamicDecalManager & instance() { static DynamicDecalManager value; return value; }
    int addDecalGroup( const BW::string & groupName, float lifeTime );
    int textureIndex( const BW::string & texName );
    friend PyObject * hostState( PyObject *, PyObject * );
};
@MANAGER@
@PRODUCTION@

PyObject * hostSetPipeline( PyObject *, PyObject * args )
{
    int type = 0, inited = 1;
    if (!PyArg_ParseTuple( args, "i|i", &type, &inited )) return NULL;
    static Renderer renderer;
    Renderer::current = type < 0 ? nullptr : &renderer;
    renderer.type = IRendererPipeline::EType( type < 0 ? 0 : type );
    renderer.inited = inited != 0;
    Py_RETURN_NONE;
}
PyObject * hostState( PyObject *, PyObject * )
{
    DynamicDecalManager & manager = DynamicDecalManager::instance();
    PyObject * groups = PyList_New( 0 );
    for (const auto & group : manager.m_groups)
    {
        PyObject * item = Py_BuildValue( "(sf)", group.m_name.c_str(), group.m_lifeTime );
        PyList_Append( groups, item ); Py_DECREF( item );
    }
    PyObject * textures = PyList_New( 0 );
    for (const auto & name : manager.m_textures)
    {
        PyObject * item = PyString_FromString( name.c_str() );
        PyList_Append( textures, item ); Py_DECREF( item );
    }
    static Renderer empty;
    PyObject * traces = PyList_New( 0 );
    for (const auto & name : (Renderer::current ? Renderer::current : &empty)->traces.names)
    {
        PyObject * item = PyString_FromString( name.c_str() );
        PyList_Append( traces, item ); Py_DECREF( item );
    }
    return Py_BuildValue( "{sNsNsNsf}", "groups", groups, "textures", textures, "traces", traces,
        "stickerRadians", PyStickerModel::angle );
}
PyMethodDef hostMethods[] = {
    { "setPipeline", hostSetPipeline, METH_VARARGS, NULL },
    { "state", hostState, METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL } };

PyMODINIT_FUNC initBigWorld()
{
    std::vector<PyMethodDef> & methods = bigWorldMethods();
    static std::vector<PyMethodDef> table( methods );
    table.push_back( PyMethodDef{ NULL, NULL, 0, NULL } );
    Py_InitModule( "BigWorld", table.data() );
    Py_InitModule( "_render_pipeline_host", hostMethods );
}
