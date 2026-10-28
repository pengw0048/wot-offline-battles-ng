#include "pch.hpp"
#include "stock_flock_manager.hpp"
#include "stock_flock_field.hpp"

#include "app.hpp"
#include "camera_app.hpp"
#include "client_camera.hpp"

#include "camera/base_camera.hpp"
#include "moo/render_context.hpp"
#include "pyscript/pyobject_plus.hpp"
#include "pyscript/script.hpp"
#include "pyscript/script_math.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"

#include <algorithm>
#include <cstring>

BW_BEGIN_NAMESPACE

/**
 *	#1513 BigWorld.WGFlockManager (type 01a16d80, tp_new 008a2440): triggers
 *	FlockExotic flocks when a vehicle or a shot comes near them.
 */
class WGFlockManager : public PyObjectPlus
{
    Py_Header( WGFlockManager, PyObjectPlus )

public:
    WGFlockManager() : PyObjectPlus( &s_type_ ) {}
    ~WGFlockManager() { stop(); }

    PY_FACTORY_DECLARE()

    void vehicleEnter( MatrixProviderPtr provider )
    {
        fence();
        providers_.push_back( provider );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, vehicleEnter, ARG( MatrixProviderPtr, END ) )
    void vehicleLeave( MatrixProviderPtr provider );
    PY_AUTO_METHOD_DECLARE( RETVOID, vehicleLeave, ARG( MatrixProviderPtr, END ) )
    void start( float interval );
    PY_AUTO_METHOD_DECLARE( RETVOID, start, ARG( float, END ) )
    void stop();
    PY_AUTO_METHOD_DECLARE( RETVOID, stop, END )
    void addFlock( const Vector2 & position, int id, float triggerRadius,
        const Vector2 & explosionRadius, float respawnTime )
    {
        fence();
        field_.addFlock( wot::StockFlockPoint{ position.x, position.y }, id, triggerRadius,
            wot::StockFlockPoint{ explosionRadius.x, explosionRadius.y }, respawnTime );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, addFlock, ARG( Vector2, ARG( int,
        ARG( float, ARG( Vector2, ARG( float, END ) ) ) ) ) )
    void addActivationPoint( const Vector2 & position )
    {
        fence();
        field_.addActivationPoint( wot::StockFlockPoint{ position.x, position.y } );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, addActivationPoint, ARG( Vector2, END ) )

    PY_RW_ATTRIBUTE_DECLARE( onTriggerCallback_, onTriggerCallback )

    void tick( double now, uint32 frame );

private:
    bool fence();
    void sweep();

    wot::StockFlockField field_;
    BW::vector< MatrixProviderPtr > providers_;
    PyObjectPtr onTriggerCallback_;
    SpaceID startedSpace_ = NULL_SPACE_ID;
};

namespace
{
// Started owners in start order; the WG queue holds one entry per owner.
BW::vector< WGFlockManager * > s_running;

void forget( WGFlockManager * manager )
{
    s_running.erase( std::remove( s_running.begin(), s_running.end(), manager ),
        s_running.end() );
}
}


PY_TYPEOBJECT( WGFlockManager )

PY_BEGIN_METHODS( WGFlockManager )
    PY_METHOD( vehicleEnter )
    PY_METHOD( vehicleLeave )
    PY_METHOD( start )
    PY_METHOD( stop )
    PY_METHOD( addFlock )
    PY_METHOD( addActivationPoint )
PY_END_METHODS()

PY_BEGIN_ATTRIBUTES( WGFlockManager )
    PY_ATTRIBUTE( onTriggerCallback )
PY_END_ATTRIBUTES()

PY_FACTORY_NAMED( WGFlockManager, "WGFlockManager", BigWorld )


PyObject * WGFlockManager::pyNew( PyObject * )
{
    // #1513 008a2440 ignores its arguments.
    return new WGFlockManager();
}


void WGFlockManager::vehicleLeave( MatrixProviderPtr provider )
{
    fence();
    // 008a29a0 erases the first provider with the same identity.
    BW::vector< MatrixProviderPtr >::iterator it =
        std::find( providers_.begin(), providers_.end(), provider );
    if (it != providers_.end()) providers_.erase( it );
}


void WGFlockManager::start( float interval )
{
    fence();
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    if (field_.running() || !space) return;
    const AABB bounds = space->bounds();
    const wot::StockFlockBounds box = { bounds.minBounds().x, bounds.minBounds().y,
        bounds.minBounds().z, bounds.maxBounds().x, bounds.maxBounds().y,
        bounds.maxBounds().z };
    field_.start( interval, &box, DeprecatedSpaceHelpers::cameraSpaceChanges(),
        App::instance().getGameTimeFrameStart(), Moo::rc().frameTimestamp() );
    startedSpace_ = space->id();
    if (std::find( s_running.begin(), s_running.end(), this ) == s_running.end())
        s_running.push_back( this );
}


void WGFlockManager::stop()
{
    // 008a31f0 keeps onTriggerCallback.
    field_.stop();
    providers_.clear();
    forget( this );
}


bool WGFlockManager::fence()
{
    // A run belongs to the camera space assignment it started under.
    if (!field_.stale( DeprecatedSpaceHelpers::cameraSpaceChanges() )) return false;
    ClientSpacePtr space = DeprecatedSpaceHelpers::cameraSpace();
    INFO_MSG( "WGFlockManager: camera space %d replaced by %d; run retired\n",
        startedSpace_, space ? space->id() : NULL_SPACE_ID );
    stop();
    return true;
}


void WGFlockManager::tick( double now, uint32 frame )
{
    if (fence() || !field_.due( now, frame )) return;
    const unsigned generation = field_.generation();
    sweep();
    // A callback that stopped or restarted this owner owns its schedule now.
    if (field_.generation() == generation) field_.reschedule( now, frame );
}


void WGFlockManager::sweep()
{
    // 008a32a0: the camera invView translation selects the 3x3 cell window.
    field_.advance();
    BaseCameraPtr camera = CameraApp::instance().clientCamera().camera();
    if (!camera) return;
    const Vector3 eye = camera->invView().applyToOrigin();
    const unsigned generation = field_.generation();
    const wot::StockFlockField::Window window =
        field_.window( wot::StockFlockPoint{ eye.x, eye.z } );

    struct Trigger
    {
        WGFlockManager & owner;
        void operator()( int id )
        {
            // 008a2c90 calls onTriggerCallback(id) and prints its errors.
            PyObjectPtr callback = owner.onTriggerCallback_;
            if (!callback) return;
            PyObject * args = PyTuple_New( 1 );
            PyTuple_SetItem( args, 0, PyInt_FromLong( id ) );
            // Script::call steals both references.
            Py_INCREF( callback.getObject() );
            Script::call( callback.getObject(), args, "" );
            owner.fence();
        }
    } trigger = { *this };
    // Hold the providers; a callback may leave, enter or stop.
    const BW::vector< MatrixProviderPtr > providers = providers_;
    for (size_t i = 0; i < providers.size(); ++i)
    {
        // 008a32a0 reads each provider's current matrix; a None entry has none.
        if (!providers[i]) continue;
        Matrix matrix;
        memset( &matrix, 0, sizeof( matrix ) );
        providers[i]->matrix( matrix );
        const Vector3 position = matrix.applyToOrigin();
        if (!field_.test( wot::StockFlockPoint{ position.x, position.z }, false,
                window, generation, trigger ))
            return;
    }
    const std::vector< wot::StockFlockPoint > points = field_.takeActivationPoints();
    for (size_t i = 0; i < points.size(); ++i)
        if (!field_.test( points[i], true, window, generation, trigger ))
            return;
}


void tickStockFlockManagers()
{
    if (s_running.empty()) return;
    const double now = App::instance().getGameTimeFrameStart();
    const uint32 frame = Moo::rc().frameTimestamp();
    // Callbacks may stop, restart or release any owner.
    BW::vector< SmartPointer< WGFlockManager > > running;
    for (size_t i = 0; i < s_running.size(); ++i)
        running.push_back( s_running[i] );
    for (size_t i = 0; i < running.size(); ++i)
        running[i]->tick( now, frame );
}

BW_END_NAMESPACE
