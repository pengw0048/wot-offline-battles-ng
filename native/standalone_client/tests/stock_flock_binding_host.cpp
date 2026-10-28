// Compiles the production stock_flock_manager.cpp against bounded engine and
// Python fakes: a synchronous Script::call, mutable matrix providers, a
// controllable camera space counter and frame clock. It drives provider
// registration through trigger callbacks; it is not the engine ABI.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define Py_Header( T, B ) public: static int s_type_;
#define PY_FACTORY_DECLARE() static PyObject * pyNew( PyObject * );
#define PY_AUTO_METHOD_DECLARE( ... )
#define PY_RW_ATTRIBUTE_DECLARE( member, name ) \
    void setCallback( PyObject * value ) { member = PyObjectPtr( value ); }
#define PY_TYPEOBJECT( T ) int T::s_type_ = 0;
#define PY_BEGIN_METHODS( ... )
#define PY_METHOD( ... )
#define PY_END_METHODS( ... )
#define PY_BEGIN_ATTRIBUTES( ... )
#define PY_ATTRIBUTE( ... )
#define PY_END_ATTRIBUTES( ... )
#define PY_FACTORY_NAMED( ... )
#define Py_INCREF( ... )
#define INFO_MSG( ... )

namespace BW {
typedef std::uint32_t uint32;
typedef int SpaceID;
const SpaceID NULL_SPACE_ID = 0;
template <class T> using vector = std::vector<T>;
struct Vector2 { float x, y; };
struct Vector3 { float x, y, z; };
struct Matrix
{
    float m[16];
    Vector3 applyToOrigin() const { return Vector3{ m[12], m[13], m[14] }; }
};
class MatrixProvider
{
public:
    virtual ~MatrixProvider() {}
    virtual void matrix( Matrix & ) const = 0;
};
typedef std::shared_ptr<MatrixProvider> MatrixProviderPtr;

struct PyObject { long value = 0; std::function<void( int )> call; };
class PyObjectPlus : public PyObject { public: explicit PyObjectPlus( void * ) {} };
class PyObjectPtr
{
public:
    PyObjectPtr() {}
    explicit PyObjectPtr( PyObject * object ) : object_( object ) {}
    explicit operator bool() const { return object_ != nullptr; }
    PyObject * getObject() const { return object_; }
private:
    PyObject * object_ = nullptr;
};
PyObject * PyTuple_New( int ) { return new PyObject(); }
PyObject * PyInt_FromLong( long value ) { PyObject * object = new PyObject(); object->value = value; return object; }
void PyTuple_SetItem( PyObject * tuple, int, PyObject * item ) { tuple->value = item->value; delete item; }
namespace Script
{
void call( PyObject * function, PyObject * args, const char * )
{
    const int id = int( args->value );
    delete args;
    function->call( id );
}
}
template <class T> class SmartPointer
{
public:
    SmartPointer( T * object ) : object_( object ) {}
    T * operator->() const { return object_; }
private:
    T * object_;
};

class AABB
{
public:
    Vector3 minBounds() const { return Vector3{ -1000.f, -50.f, -1000.f }; }
    Vector3 maxBounds() const { return Vector3{ 1000.f, 300.f, 1000.f }; }
};
class ClientSpace
{
public:
    AABB bounds() const { return AABB(); }
    SpaceID id() const { return 7; }
};
typedef std::shared_ptr<ClientSpace> ClientSpacePtr;
uint32 g_cameraSpaceChanges = 1;
namespace DeprecatedSpaceHelpers
{
ClientSpacePtr cameraSpace() { static ClientSpacePtr space = std::make_shared<ClientSpace>(); return space; }
uint32 cameraSpaceChanges() { return g_cameraSpaceChanges; }
}
struct App
{
    static App & instance() { static App app; return app; }
    double now = 0.0;
    double getGameTimeFrameStart() const { return now; }
};
namespace Moo
{
struct RenderContext { uint32 frame = 0; uint32 frameTimestamp() const { return frame; } };
RenderContext & rc() { static RenderContext context; return context; }
}
class BaseCamera
{
public:
    Matrix invView() const { Matrix matrix = {}; matrix.m[12] = 100.f; matrix.m[14] = 100.f; return matrix; }
};
typedef std::shared_ptr<BaseCamera> BaseCameraPtr;
struct ClientCamera { BaseCameraPtr camera() { static BaseCameraPtr camera = std::make_shared<BaseCamera>(); return camera; } };
struct CameraApp
{
    static CameraApp & instance() { static CameraApp app; return app; }
    ClientCamera & clientCamera() { static ClientCamera camera; return camera; }
};
}

#include "stock_flock_manager.cpp"

namespace {
struct Provider : BW::MatrixProvider
{
    float x, z;
    mutable int samples = 0;
    Provider( float x, float z ) : x( x ), z( z ) {}
    void matrix( BW::Matrix & matrix ) const override
    {
        ++samples;
        matrix.m[12] = x;
        matrix.m[14] = z;
    }
};

// One frame of WorldApp::tick: the WG queue sees a later frame and time.
void frame()
{
    BW::App::instance().now += 1.0;
    BW::Moo::rc().frame += 1;
    BW::tickStockFlockManagers();
}

struct Scene
{
    BW::WGFlockManager owner;
    std::shared_ptr<Provider> a = std::make_shared<Provider>( 50.f, 50.f );
    std::shared_ptr<Provider> b = std::make_shared<Provider>( 150.f, 50.f );
    std::shared_ptr<Provider> c = std::make_shared<Provider>( 250.f, 50.f );
    std::vector<int> triggered;
    std::function<void( int )> reenter;
    BW::PyObject callback;

    Scene()
    {
        BW::g_cameraSpaceChanges = 1;
        callback.call = [this]( int id ) {
            triggered.push_back( id );
            if (reenter) reenter( id );
        };
        owner.setCallback( &callback );
        owner.addFlock( BW::Vector2{ 50.f, 50.f }, 1, 20.f, BW::Vector2{ 8.f, 35.f }, 400.f );
        owner.addFlock( BW::Vector2{ 150.f, 50.f }, 2, 20.f, BW::Vector2{ 8.f, 35.f }, 400.f );
        owner.addFlock( BW::Vector2{ 250.f, 50.f }, 3, 20.f, BW::Vector2{ 8.f, 35.f }, 400.f );
    }

    void start()
    {
        owner.start( 1.f );
        frame();
    }

    void report( const char * name ) const
    {
        std::printf( "%s a=%d b=%d c=%d triggered=", name, a->samples, b->samples, c->samples );
        for (size_t i = 0; i < triggered.size(); ++i) std::printf( "%s%d", i ? "," : "", triggered[i] );
        std::printf( "\n" );
    }
};
}

int main()
{
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.start();
        scene.report( "keep" );
    }
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.owner.vehicleLeave( scene.b );
        scene.start();
        scene.report( "leave-before" );
    }
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.reenter = [&]( int ) { scene.owner.vehicleLeave( scene.b ); };
        scene.start();
        scene.report( "leave-during" );
    }
    {
        // A left and re-entered source is a new registration for the next sweep.
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.reenter = [&]( int id ) {
            if (id != 1) return;
            scene.owner.vehicleLeave( scene.b );
            scene.owner.vehicleEnter( scene.b );
        };
        scene.start();
        scene.report( "reenter-during" );
        frame();
        scene.report( "reenter-next" );
    }
    {
        // Two registrations of one provider: leave erases the first only.
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.owner.vehicleEnter( scene.b );
        scene.start();
        scene.report( "duplicate-keep" );
    }
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.owner.vehicleEnter( scene.b );
        scene.reenter = [&]( int ) { scene.owner.vehicleLeave( scene.b ); };
        scene.start();
        scene.report( "duplicate-leave-during" );
    }
    {
        // Leaving the current source does not revoke later ones.
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.reenter = [&]( int ) { scene.owner.vehicleLeave( scene.a ); };
        scene.start();
        scene.report( "leave-self" );
    }
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.reenter = [&]( int id ) { if (id == 1) scene.owner.vehicleEnter( scene.c ); };
        scene.start();
        scene.report( "enter-during" );
        frame();
        scene.report( "enter-next" );
    }
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.reenter = [&]( int ) { scene.owner.stop(); };
        scene.start();
        scene.report( "stop-during" );
        frame();
        scene.report( "stop-next" );
    }
    {
        Scene scene;
        scene.owner.vehicleEnter( scene.a );
        scene.owner.vehicleEnter( scene.b );
        scene.reenter = [&]( int ) { ++BW::g_cameraSpaceChanges; };
        scene.start();
        scene.report( "space-during" );
    }
    return 0;
}
