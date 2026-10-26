// Real BW DataDescription/DataType, CPython 2 and ClientSpace lifetime probe.
#include "cstdmf/cstdmf_windows.hpp"
#include "cstdmf/allocator.hpp"
#include "cstdmf/log_msg.hpp"
#include "stock_space_data.hpp"
#include "entitydef/data_type.hpp"
#include "resmgr/bwresource.hpp"
#include "pyscript/script.hpp"
#include "space/client_space.hpp"
#include "space/space_manager.hpp"
#include <Python.h>
#include <iostream>
#include <stdexcept>

namespace
{
BW::ClientSpace * callbackSpace = NULL;
void require( bool value, const char * message )
{
    if (!value) throw std::runtime_error( message );
}
void python( const char * code )
{
    require( PyRun_SimpleString( code ) == 0, "Native typed-space Python contract failed" );
}

PyObject * clearDuringPublication( PyObject *, PyObject * )
{
    callbackSpace->clear();
    Py_RETURN_NONE;
}
PyMethodDef clearMethod = { "_clearDuringPublication", clearDuringPublication, METH_NOARGS, NULL };

// An actual empty native ClientSpace owns Scene and SpaceDataMapping. Only
// clear is exercised; rendering/collision operations fail if called, so this
// fixture cannot masquerade as a playable world or a physics result.
class EmptyProbeSpace : public BW::ClientSpace
{
public:
    explicit EmptyProbeSpace( BW::SpaceID id ) : ClientSpace( id ) {}
    unsigned clears = 0;
protected:
    virtual void doClear()
    {
        PyObject * module = PyImport_AddModule( "BigWorld" );
        PyObject * spaces = PyObject_GetAttrString( module, "spaces" );
        PyObject * key = PyInt_FromLong( id() );
        PyObject * value = PyObject_GetItem( spaces, key );
        Py_DECREF( key ); Py_DECREF( spaces );
        const bool removed = value == NULL && PyErr_ExceptionMatches( PyExc_KeyError );
        Py_XDECREF( value );
        require( removed, "Typed space survived into native doClear" );
        PyErr_Clear();
        ++clears;
    }
    virtual bool doAddMapping( GeometryMappingID, BW::Matrix &, const BW::string &, const BW::DataSectionPtr & )
        { throw std::runtime_error( "Geometry is outside the typed-space probe" ); }
    virtual void doDelMapping( GeometryMappingID ) { throw std::runtime_error( "Geometry is outside the typed-space probe" ); }
    virtual BW::EnviroMinder & doEnviro() { throw std::runtime_error( "Environment is outside the typed-space probe" ); }
    virtual float doGetLoadStatus( float ) const { throw std::runtime_error( "Geometry is outside the typed-space probe" ); }
    virtual void doTick( float ) { throw std::runtime_error( "Tick is outside the typed-space probe" ); }
    virtual void doUpdateAnimations( float ) { throw std::runtime_error( "Animation is outside the typed-space probe" ); }
    virtual BW::AABB doGetBounds() const { throw std::runtime_error( "Bounds are outside the typed-space probe" ); }
    virtual BW::Vector3 doClampToBounds( const BW::Vector3 & ) const { throw std::runtime_error( "Bounds are outside the typed-space probe" ); }
    virtual float doCollide( const BW::Vector3 &, const BW::Vector3 &, BW::CollisionCallback & ) const
        { throw std::runtime_error( "Collision is outside the typed-space probe" ); }
    virtual float doCollide( const BW::WorldTriangle &, const BW::Vector3 &, BW::CollisionCallback & ) const
        { throw std::runtime_error( "Collision is outside the typed-space probe" ); }
    virtual float doFindTerrainHeight( const BW::Vector3 & ) const { throw std::runtime_error( "Terrain is outside the typed-space probe" ); }
    virtual const BW::Moo::DirectionalLightPtr & doGetSunLight() const { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
    virtual const BW::Moo::Colour & doGetAmbientLight() const { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
    virtual const BW::Moo::LightContainerPtr & doGetLights() const { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
    virtual bool doGetLightsInArea( const BW::AABB &, BW::Moo::LightContainerPtr * )
        { throw std::runtime_error( "Lighting is outside the typed-space probe" ); }
};
}

int main( int argc, char ** argv )
{
    BW_SYSTEMSTAGE_MAIN();
    BW::LogMsg::automatedTest( true );
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX );
    if (argc < 2 || argc > 3) return 2;
    const bool alternate = argc == 3 && std::string( argv[2] ) == "--alternate";
    const bool invalid = argc == 3 && std::string( argv[2] ) == "--invalid";
    BW::vector<BW::string> paths;
    paths.push_back( argv[1] );
    int outcome = 1;
    Py_NoSiteFlag = 1;
    Py_Initialize();
    try
    {
        require( BW::BWResource::init( paths ), "BWResource initialisation failed" );
        if (invalid)
        {
            require( !BW::initialiseStockSpaceData() && PyErr_Occurred(), "Invalid schema was accepted" );
            PyErr_Clear();
            outcome = 0;
        }
        else
        {
            require( BW::initialiseStockSpaceData(), "Typed-space initialisation failed" );
            python( "import BigWorld, gc\nassert len(BigWorld.spaces)==0\n" );
            BW::ClientSpacePtr first = new EmptyProbeSpace( 9 );
            BW::ClientSpacePtr second = new EmptyProbeSpace( 3 );
            BW::SpaceManager::instance().addSpace( first.get() );
            BW::SpaceManager::instance().addSpace( second.get() );
            require( BW::publishStockSpaceData( 9, 0xffff ), "No-schema space branch failed" );
            python( "assert len(BigWorld.spaces)==0\n" );
            require( BW::publishStockSpaceData( 9, 0 ) && BW::publishStockSpaceData( 3, 0 ), "Typed-space publication failed" );
            PyObject * scope = PyModule_GetDict( PyImport_AddModule( "__main__" ) );
            PyObject * mode = PyBool_FromLong( alternate );
            require( PyDict_SetItemString( scope, "alternate", mode ) == 0, "Cannot set probe mode" );
            Py_DECREF( mode );
            python( R"PY(
spaces=BigWorld.spaces
assert spaces.keys()==[3,9]
assert spaces.items()==[(3,spaces[3]),(9,spaces[9])]
assert spaces.values()==[spaces[3],spaces[9]]
assert spaces.has_key(9) and not spaces.has_key(100)
marker=object();assert spaces.get(100,marker) is marker and spaces.get(100) is None
try: spaces[100]
except KeyError as error: assert error.args==('No space(100) exists.',)
else: raise AssertionError('Missing native space did not raise KeyError')
try: spaces[9]=object()
except TypeError: pass
else: raise AssertionError('Native space mapping allowed assignment')
try: del spaces[9]
except TypeError: pass
else: raise AssertionError('Native space mapping allowed deletion')
spaces.dynamicAttribute='allowed'
assert spaces.dynamicAttribute=='allowed'
held=spaces[9]
assert held.weather==('rain' if alternate else '')
assert held.itemsVisibilityMask==(7 if alternate else 0)
assert not hasattr(held,'spaceID')
assert not hasattr(held,'geometryLoaded') and not hasattr(held,'recorderFragment') and not hasattr(held,'isRecording')
assert not hasattr(held,'privateLoose')
held.geometryLoaded=object();held.dynamicAttribute=[1,2]
assert held.dynamicAttribute==[1,2]
for name,value in (('weather',1),('itemsVisibilityMask',-1),('itemsVisibilityMask',1<<32),('itemsVisibilityMask',None)):
    before=getattr(held,name)
    try: setattr(held,name,value)
    except TypeError as error:
        schema='FixtureSpace' if alternate else 'GeneralSpaceData'
        assert str(error)=='Attempted to set attribute %s on %s to an invalid value.'%(name,schema)
    else: raise AssertionError('Typed property accepted an invalid value')
    assert getattr(held,name)==before
held.itemsVisibilityMask=0xffffffffL
assert held.itemsVisibilityMask==0xffffffffL
held.itemsVisibilityMask=1
assert BigWorld.wg_getSpaceItemsVisibilityMask(9)==0
assert BigWorld.wg_getSpaceItemsVisibilityMask(1000)==0
assert BigWorld.wg_setSpaceItemsVisibilityMask(9,7) is None
assert held.itemsVisibilityMask==1
for function,args in ((BigWorld.wg_getSpaceItemsVisibilityMask,()),(BigWorld.wg_getSpaceItemsVisibilityMask,('bad',)),
                      (BigWorld.wg_setSpaceItemsVisibilityMask,(9,)),(BigWorld.wg_setSpaceItemsVisibilityMask,(9,-1))):
    try: function(*args)
    except (TypeError,OverflowError): pass
    else: raise AssertionError('Legacy wrapper accepted invalid arguments')
)PY" );
            BW::uint32 mask = 123;
            require( BW::stockSpaceItemsVisibilityMask( 9, mask ) && mask == 1, "Actual typed mapping mask differs" );
            python( "held.itemsVisibilityMask=0\n" );
            require( BW::stockSpaceItemsVisibilityMask( 9, mask ) && mask == 0xffffffff, "Native zero-to-all mask differs" );
            python( "del held.itemsVisibilityMask\n" );
            mask = 123;
            require( !BW::stockSpaceItemsVisibilityMask( 9, mask ) && mask == 123 && PyErr_ExceptionMatches( PyExc_AttributeError ),
                "Deleted typed mask silently became a physical result" );
            PyErr_Clear();
            python( "held.itemsVisibilityMask=1\n" );
            BW::retireStockSpaceData( 9, second.get() );
            python( "assert spaces[9] is held\n" );
            first->clear();
            python( "assert spaces.keys()==[3]\nheld.weather='retained after native clear'\n" );
            require( !BW::publishStockSpaceData( 9, 0 ) && PyErr_Occurred(), "Cleared native space was republished" );
            PyErr_Clear();
            second->clear();
            require( static_cast<EmptyProbeSpace *>( first.get() )->clears == 1, "Native clear callback did not execute" );
            first = NULL; second = NULL;
            python( "assert len(spaces)==0\n" );
            // Dropping the replaced PySpace's dictionary can synchronously
            // clear its native space. The new PySpace and native owner must
            // remain pinned, and publication must report the real retirement.
            BW::ClientSpacePtr reentrant = new EmptyProbeSpace( 17 );
            BW::SpaceManager::instance().addSpace( reentrant.get() );
            require( BW::publishStockSpaceData( 17, 0 ), "Reentry fixture publication failed" );
            callbackSpace = reentrant.get();
            PyObject * callback = PyCFunction_New( &clearMethod, NULL );
            require( callback && PyDict_SetItemString( scope, "_clearDuringPublication", callback ) == 0,
                "Cannot install real native-clear callback" );
            Py_DECREF( callback );
            python( "class RetireNativeSpace(object):\n    def __del__(self): _clearDuringPublication()\n"
                    "spaces[17].retire=RetireNativeSpace()\n" );
            require( !BW::publishStockSpaceData( 17, 0 ) && PyErr_ExceptionMatches( PyExc_RuntimeError ),
                "Synchronous native retirement became a successful publication" );
            PyErr_Clear();
            require( static_cast<EmptyProbeSpace *>( reentrant.get() )->clears == 1,
                "Publication did not exercise the synchronous native-clear callback" );
            python( "assert len(spaces)==0\ndel _clearDuringPublication\n" );
            callbackSpace = NULL;
            reentrant = NULL;
            BW::finaliseStockSpaceData();
            python( "assert held.weather=='retained after native clear'\n" );
            require( BW::initialiseStockSpaceData(), "Typed-space restart failed" );
            python( "assert BigWorld.spaces is not spaces and len(BigWorld.spaces)==0\ndel held,spaces\ngc.collect()\n" );
            outcome = 0;
        }
    }
    catch (const std::exception & error)
    {
        if (PyErr_Occurred()) PyErr_Print();
        std::cerr << "Typed-space probe failed: " << error.what() << std::endl;
    }
    BW::finaliseStockSpaceData();
    BW::DataType::clearStaticsForReload();
    Py_Finalize();
    BW::BWResource::fini();
    BW::LogMsg::fini();
    if (!outcome) std::cout << "Real typed-space DataDescription, CPython mapping and ClientSpace ownership passed" << std::endl;
    return outcome;
}
