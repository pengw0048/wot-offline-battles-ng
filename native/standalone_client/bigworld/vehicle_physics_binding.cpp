#include "pch.hpp"
#include "vehicle_physics_binding.hpp"
#include "stock_scene_collision.hpp"
#include "stock_vehicle_native_math.hpp"

#include "py_entity.hpp"
#include "pyscript/script_math.hpp"
#include "wot/vehicle_physics.hpp"

#include <memory>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <stdexcept>

BW_BEGIN_NAMESPACE

int StockVehiclePhysics_token = 1;

namespace
{
Vector3 nativeVector( wot::Vector3 value )
{
    return Vector3( value.x, value.y, value.z );
}

Matrix nativeMatrix( const wot::VehicleMatrix & value )
{
    Matrix result;
    for (uint32 row = 0; row < 4; ++row)
        for (uint32 column = 0; column < 4; ++column)
            result( row, column ) = value.rows[row][column];
    return result;
}
}

namespace
{
// The original Reader (f49860/f7e600/f81a60/f81c30) accepts dictionary
// subclasses, numeric slots and exact bools. Each borrowed value is pinned
// before conversions can reenter Python or mutate its source dictionary.
//
// A missing or mistyped field throws WGPhysics::PyProxyException, which
// sets no Python error; the readers below catch it exactly where #1513
// does. A CPython error raised by a conversion callback is different: the
// original keeps a garbage value with the exception pending and returns
// True, so the port stops the transaction and surfaces that exception.
struct StockPhysicsProxyFailure
{
    PyObject * type;
    std::string message;
};
struct StockPhysicsPythonFailure {};

void stockPhysicsCheckPython()
{
    if (PyErr_Occurred()) throw StockPhysicsPythonFailure();
}

class StockPhysicsConfigValue
{
public:
    StockPhysicsConfigValue( PyObject * value, std::string path ) :
        value_( value ), path_( std::move( path ) ) {}

    StockPhysicsConfigValue child( const char * key ) const
    {
        this->dictionary();
        PyObject * value = PyDict_GetItemString( value_.get(), key );
        if (!value) fail( PyExc_ValueError, "dictionary has no field '" + std::string( key ) + "'" );
        return StockPhysicsConfigValue( value, path_ + "." + key );
    }
    void dictionary() const
    {
        if (!value_ || !PyDict_Check( value_.get() )) fail( PyExc_TypeError, "object is not a dictionary" );
    }
    float floating() const
    {
        this->requireNumber( 1 );
        const double value = PyFloat_AsDouble( value_.get() );
        stockPhysicsCheckPython();
        return static_cast<float>( value );
    }
    std::int32_t integral() const
    {
        this->requireNumber( 0 );
        const long value = PyInt_AsLong( value_.get() );
        stockPhysicsCheckPython();
        // The original ABI is Win32 long; PyInt_AsLong raises there.
        if (value < INT32_MIN || value > INT32_MAX)
        {
            PyErr_Format( PyExc_OverflowError, "%s: integer does not fit the native long", path_.c_str() );
            throw StockPhysicsPythonFailure();
        }
        return static_cast<std::int32_t>( value );
    }
    bool boolean() const
    {
        if (!value_ || !PyBool_Check( value_.get() )) fail( PyExc_TypeError, "object is not boolean" );
        return value_.get() == Py_True;
    }
    std::vector<float> sequence() const
    {
        // f839c0: any width, converted element by element.
        const Py_ssize_t count = this->sequenceSize();
        std::vector<float> result;
        result.reserve( static_cast<size_t>( count ) );
        for (Py_ssize_t index = 0; index < count; ++index)
            result.push_back( this->item( index ).floating() );
        return result;
    }
    template<size_t Count>
    std::array<float, Count> fixed() const
    {
        // f7e3e0/f80e20/f809e0/f80c00 return a temporary, so the caller's
        // field changes only after every element converts.
        if (this->sequenceSize() != static_cast<Py_ssize_t>( Count ))
            fail( PyExc_ValueError, "sequence has the wrong item count" );
        std::array<float, Count> result;
        for (size_t index = 0; index < Count; ++index)
            result[index] = this->item( static_cast<Py_ssize_t>( index ) ).floating();
        return result;
    }
    wot::Vector3 vector() const
    {
        if (this->sequenceSize() != 3) fail( PyExc_ValueError, "vector requires three items" );
        // f83d20 obtains and validates all three objects before converting any.
        std::array<StockPhysicsConfigValue, 3> items = {{ this->item( 0 ), this->item( 1 ), this->item( 2 ) }};
        for (const auto & item : items) item.requireNumber( 1 );
        std::array<float, 3> values;
        for (size_t index = 0; index < 3; ++index) values[index] = items[index].floating();
        return { values[0], values[1], values[2] };
    }
    PyObjectPtr items() const
    {
        // f81c30 snapshots PyDict_Next order; a non-dictionary iterates empty.
        if (!value_ || !PyDict_Check( value_.get() ))
            return PyObjectPtr( PyList_New( 0 ), PyObjectPtr::STEAL_REFERENCE );
        PyObjectPtr result( PyDict_Items( value_.get() ), PyObjectPtr::STEAL_REFERENCE );
        if (!result) throw StockPhysicsPythonFailure();
        return result;
    }
    std::string byteStringName() const
    {
        // The native dispatch does not constrain mode-key types. A byte name
        // is retained as optional diagnostic metadata, never as a mode index.
        return value_ && PyString_Check( value_.get() ) ?
            std::string( PyString_AS_STRING( value_.get() ), PyString_GET_SIZE( value_.get() ) ) : std::string();
    }
private:
    [[noreturn]] void fail( PyObject * type, const std::string & message ) const
    {
        throw StockPhysicsProxyFailure{ type, path_ + ": " + message };
    }
    void requireNumber( int floatingSlot ) const
    {
        PyNumberMethods * number = value_ ? Py_TYPE( value_.get() )->tp_as_number : NULL;
        if (!value_ || !PyNumber_Check( value_.get() ) || !number ||
            !(floatingSlot ? number->nb_float : number->nb_int))
            fail( PyExc_TypeError, floatingSlot ? "object is not floating" : "object is not integral" );
    }
    Py_ssize_t sequenceSize() const
    {
        if (!value_ || !PySequence_Check( value_.get() )) fail( PyExc_TypeError, "object is not a sequence" );
        const Py_ssize_t count = PySequence_Size( value_.get() );
        stockPhysicsCheckPython();
        return count;
    }
    StockPhysicsConfigValue item( Py_ssize_t index ) const
    {
        PyObjectPtr item( PySequence_GetItem( value_.get(), index ), PyObjectPtr::STEAL_REFERENCE );
        if (!item) throw StockPhysicsPythonFailure();
        return StockPhysicsConfigValue( item.get(), path_ + "[]" );
    }

    PyObjectPtr value_;
    std::string path_;
};

typedef StockPhysicsProxyFailure Caught;

bool readStockPhysicsBody( const StockPhysicsConfigValue & value, wot::VehicleBodyConfiguration & result )
{
    // f846c0: one catch region; the fields read before a failure remain.
    try
    {
        result.gravity = value.child( "gravity" ).floating();
        result.full_mass = value.child( "fullMass" ).floating();
        result.hull_inertia_factors = value.child( "hullInertiaFactors" ).vector();
        result.hull_size = value.child( "hullSize" ).vector();
        result.hull_com_shift_y = value.child( "hullCOMShiftY" ).floating();
        result.hull_box_offset_z = value.child( "hullBoxOffsetZ" ).floating();
        result.turret_top_position = value.child( "turretTopPos" ).vector();
        result.turret_top_width = value.child( "turretTopWidth" ).floating();
        result.simple_forward_max_speed = value.child( "smplFwMaxSpeed" ).floating();
        result.simple_backward_max_speed = value.child( "smplBkMaxSpeed" ).floating();
        result.simple_rotation_speed = value.child( "smplRotSpeed" ).floating();
        result.angular_velocity_factor = value.child( "angVelocityFactor" ).floating();
        result.allowed_rpm_excess = value.child( "allowedRPMExcess" ).floating();
        result.overspeed_resistance_factor = value.child( "overspeedResistFactor" ).floating();
        result.rotation_around_center = value.child( "isRotationAroundCenter" ).boolean();
        result.center_rotation_forward_speed = value.child( "centerRotationFwdSpeed" ).floating();
        result.movement_revert_speed = value.child( "movementRevertSpeed" ).floating();
        result.com_friction_y_offset = value.child( "comFrictionYOffs" ).floating();
        result.com_side_friction = value.child( "comSideFriction" ).floating();
        result.stabilization_enabled = value.child( "enableSabilization" ).boolean();
        result.gimlet_pushed_rotation = value.child( "gimlet" ).child( "wPushedRot" ).floating();
        result.gimlet_pushed_handbrake = value.child( "gimlet" ).child( "wPushedHB" ).floating();
    }
    catch (const Caught &)
    {
        return false;
    }
    return true;
}

void readStockPhysicsShape( const StockPhysicsConfigValue & value, wot::VehicleShapeConfiguration & result )
{
    try // f8fe40
    {
        result.use_complex_form = value.child( "useComplexForm" ).boolean();
        result.auxiliary_clearance = value.child( "auxClearance" ).floating();
        result.tank_board_angle = value.child( "tankBoardAngle" ).floating();
        result.tank_aft_chamfer_fraction = value.child( "tankAftChamferFraction" ).floating();
        result.tank_front_chamfer_fraction = value.child( "tankFrontChamferFraction" ).floating();
        result.terrain_board_angle = value.child( "terrBoardAngle" ).floating();
        result.terrain_front_chamfer_fraction = value.child( "terrFrontChamferFraction" ).floating();
        result.terrain_aft_chamfer_fraction = value.child( "terrAftChamferFraction" ).floating();
    }
    catch (const Caught &) {}
}

void readStockPhysicsTank( const StockPhysicsConfigValue & value, wot::VehiclePhysicsModeConfiguration & mode )
{
    // f8fd60: the shape lookup itself sits outside every catch region.
    if (readStockPhysicsBody( value, mode.body ))
        readStockPhysicsShape( value.child( "shape" ), mode.shape );
    try
    {
        mode.siege_mode_available = value.child( "siegeModeAvailable" ).boolean();
    }
    catch (const Caught &) {}
}

bool readStockPhysicsGroundSurface( const StockPhysicsConfigValue & value, wot::VehicleGroundSurfaceConfiguration & result )
{
    try // f830d0
    {
        result.dirt_cumulation_rate = value.child( "dirtCumulationRate" ).floating();
        result.dirt_release_rate = value.child( "dirtReleaseRate" ).floating();
        result.dirt_side_velocity = value.child( "dirtSideVelocity" ).floating();
        result.max_dirt = value.child( "maxDirt" ).floating();
        result.handbrake_com_side_friction = value.child( "hbComSideFriction" ).floating();
        result.side_friction = value.child( "sideFriction" ).floating();
        result.forward_friction = value.child( "fwdFriction" ).floating();
        result.rolling_friction = value.child( "rollingFriction" ).floating();
        result.handbrake_side_friction_addition = value.child( "hbSideFrictionAddition" ).floating();
        result.rotation_factor = value.child( "rotationFactor" ).floating();
    }
    catch (const Caught &)
    {
        return false;
    }
    return true;
}

bool readStockPhysicsGrounds( const StockPhysicsConfigValue & value, wot::VehicleChassisConfiguration & chassis )
{
    // f83340: an item that fails keeps the table entry, later items continue.
    PyObjectPtr entries = value.items();
    bool complete = true;
    size_t installed = 0;
    const Py_ssize_t count = PyList_GET_SIZE( entries.get() );
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObject * pair = PyList_GET_ITEM( entries.get(), index );
        // The key conversion precedes the per-item region (escapes to f822a0).
        const std::int32_t kind = StockPhysicsConfigValue( PyTuple_GET_ITEM( pair, 0 ), "grounds.key" ).integral();
        const StockPhysicsConfigValue item( PyTuple_GET_ITEM( pair, 1 ), "grounds.value" );
        wot::VehicleGroundSurfaceConfiguration firm, medium, soft;
        bool ok = kind != -1;
        try
        {
            if (ok) ok = readStockPhysicsGroundSurface( item.child( "firm" ), firm );
            if (ok) ok = readStockPhysicsGroundSurface( item.child( "medium" ), medium );
            if (ok) ok = readStockPhysicsGroundSurface( item.child( "soft" ), soft );
        }
        catch (const Caught &)
        {
            complete = false;
            continue;
        }
        if (ok && wot::install_vehicle_ground_settings( chassis.grounds, kind, firm, medium, soft ))
            ++installed;
        else
            complete = false;
    }
    return complete && installed == chassis.grounds.size();
}

bool readStockPhysicsChassisBase( const StockPhysicsConfigValue & value, wot::VehicleChassisConfiguration & result )
{
    try // f822a0
    {
        result.chassis_mass = value.child( "chassisMass" ).floating();
        result.chassis_mass_fraction = value.child( "chassisMassFraction" ).floating();
        result.clearance = value.child( "clearance" ).floating();
        result.track_gauge_factor = value.child( "trackGaugeFactor" ).floating();
        result.wheel_detach_on_roll = value.child( "wheelDetachOnRoll" ).boolean();
        result.track_to_be_locked_delay = value.child( "trackToBeLockedDelay" ).floating();
        result.wheel_radius = value.child( "wheelRadius" ).floating();
        result.wheel_inertia_factor = value.child( "wheelInertiaFactor" ).floating();
        result.wheel_forward_inertia_factor = value.child( "wheelFwdInertiaFactor" ).floating();
        result.stiffness_zero = value.child( "stiffness0" ).floating();
        result.stiffness_one = value.child( "stiffness1" ).floating();
        result.damping = value.child( "damping" ).floating();
        result.wheel_restitution = value.child( "wheelRestitution" ).floating();
        result.wheel_penetration = value.child( "wheelPenetration" ).floating();
        result.wheel_use_pseudo_contacts = value.child( "wheelUsePseudoContacts" ).boolean();
        result.brake = value.child( "brake" ).floating();
        result.rotation_brake = value.child( "rotationBrake" ).floating();
        result.wheel_sinkage_resistance_factor = value.child( "wheelSinkageResistFactor" ).floating();
        result.wheel_dry_friction_factor = value.child( "wheelDryFrictionFactor" ).floating();
        result.side_friction_constant_ratio = value.child( "sideFrictionConstantRatio" ).floating();
        result.road_wheel_positions = value.child( "roadWheelPositions" ).fixed<5>();
        result.stiffness_factors = value.child( "stiffnessFactors" ).fixed<5>();
        result.slope_resistance_terrain = value.child( "slopeResistTerrain" ).fixed<3>();
        result.slope_resistance_static_object = value.child( "slopeResistStaticObject" ).fixed<3>();
        result.slope_resistance_dynamic_object = value.child( "slopeResistDynamicObject" ).fixed<3>();
        result.slope_grip_forward_terrain = value.child( "slopeGripLngTerrain" ).fixed<4>();
        result.slope_grip_side_terrain = value.child( "slopeGripSdwTerrain" ).fixed<4>();
        result.slope_grip_forward_static_object = value.child( "slopeGripLngStaticObject" ).fixed<4>();
        result.slope_grip_side_static_object = value.child( "slopeGripSdwStaticObject" ).fixed<4>();
        result.slope_grip_forward_dynamic_object = value.child( "slopeGripLngDynamicObject" ).fixed<4>();
        result.slope_grip_side_dynamic_object = value.child( "slopeGripSdwDynamicObject" ).fixed<4>();
        result.flat_side_friction = value.child( "flatSideFriction" ).boolean();
        result.grounds_complete = readStockPhysicsGrounds( value.child( "grounds" ), result );
        return result.grounds_complete;
    }
    catch (const Caught &)
    {
        return false;
    }
}

bool readStockPhysicsChassis( const StockPhysicsConfigValue & value, wot::VehicleChassisConfiguration & result )
{
    // f88700 reads hullAiming whatever f822a0 returned.
    const bool base = readStockPhysicsChassisBase( value, result );
    try
    {
        const auto pitch = value.child( "hullAiming" ).child( "pitch" );
        result.pitch_correction_center_z = pitch.child( "correctionCenterZ" ).floating();
        result.pitch_correction_speed = pitch.child( "correctionSpeed" ).floating();
        result.pitch_min = pitch.child( "pitchMin" ).floating();
        result.pitch_max = pitch.child( "pitchMax" ).floating();
        result.pitch_correction_stiffness = pitch.child( "correctionStiffness" ).floating();
        result.pitch_correction_damping = pitch.child( "correctionDamping" ).floating();
        result.pitch_correction_scale = pitch.child( "correctionScale" ).floating();
        const auto yaw = value.child( "hullAiming" ).child( "yaw" );
        result.yaw_gimlet_force = yaw.child( "gimletForce" ).floating();
        result.yaw_stiffness = yaw.child( "stiffness" ).floating();
        result.yaw_damping = yaw.child( "damping" ).floating();
        result.yaw_precise_restitution = yaw.child( "preciseRestitution" ).floating();
        result.yaw_damping_distance = yaw.child( "dampingYawDist" ).floating();
        result.yaw_precise_distance = yaw.child( "preciseYawDist" ).floating();
    }
    catch (const Caught &)
    {
        return false;
    }
    return base;
}

void readStockPhysicsEngine( const StockPhysicsConfigValue & value, wot::VehicleEngineConfiguration & result )
{
    // f7ecd0 has no catch region of its own; f900a0 catches around it.
    result.losses = value.child( "engineLoses" ).fixed<2>();
    result.simple_engine_power = value.child( "smplEnginePower" ).floating();
    result.engine_inertia = value.child( "engineInertia" ).floating();
    result.simple_engine_join_ratio = value.child( "smplEngJoinRatio" ).floating();
    result.idle_choker = value.child( "idleChoker" ).floating();
    result.rotation_choker = value.child( "rotationChoker" ).floating();
    result.rotation_by_lock_choker = value.child( "rotationByLockChoker" ).floating();
    result.simple_min_rpm = value.child( "smplMinRPM" ).floating();
    result.idle_rpm = value.child( "idleRPM" ).floating();
    result.start_rpm = value.child( "startRPM" ).floating();
    result.maximum_forward_angular_velocity = value.child( "engVelMax" ).floating();
    result.maximum_backward_angular_velocity = value.child( "engVelBkMax" ).floating();
    result.rotation_angular_velocity = value.child( "engVelRot" ).floating();
}

void readStockPhysicsGlobals( const StockPhysicsConfigValue & root, wot::VehiclePhysicsConfiguration & result )
{
    // f85c70 inside f900a0's catch region (state 32).
    static const char * const gimletNames[] = { "wPushedDiag", "wPushedRot", "wPushedHB",
        "pushInContact", "pushHB", "pushStop", "pushRotOnSpotFixedPeriod", "pushRotOnSpotGrowPeriod",
        "gimletPushOnSpotInit", "gimletPushOnSpotFinal", "pushRotOnMoveFixedPeriod",
        "pushRotOnMoveGrowPeriod", "gimletPushOnMoveInit", "gimletPushOnMoveFinal",
        "gimletVelScaleMin", "gimletVelScaleMax" };
    try
    {
        const auto normal = root.child( "modes" ).child( "normal" );
        const auto anchor = normal.child( "anchor" );
        wot::VehicleAnchorConfiguration values;
        values.maximum_reaction_factor = anchor.child( "anchorMaxReactionFactor" ).floating();
        values.constant_fraction = anchor.child( "anchorConstFraction" ).floating();
        values.velocity_factor = anchor.child( "anchorVelFactor" ).floating();
        result.anchor = values;
        result.rail_factor_in_contact = normal.child( "rail" ).child( "railFactorInContact" ).floating();
        const auto gimlet = normal.child( "gimlet" );
        for (std::uint32_t field = 0; field != 16; ++field)
        {
            result.gimlet.*wot::vehicle_gimlet_fields[field] = gimlet.child( gimletNames[field] ).floating();
            result.gimlet_fields = field + 1;
        }
    }
    catch (const Caught &) {}
}

std::vector<wot::VehicleFakeGearConfiguration> readStockPhysicsGears( const StockPhysicsConfigValue & value )
{
    const std::vector<float> speed = value.child( "switchSpeed" ).sequence();
    const std::vector<float> hysteresis = value.child( "switchHysteresis" ).sequence();
    const std::vector<float> low = value.child( "lowRpm" ).sequence();
    const std::vector<float> high = value.child( "highRpm" ).sequence();
    if (speed.size() != hysteresis.size() || speed.size() != low.size() || speed.size() != high.size())
        throw Caught{ PyExc_ValueError, "fake gearbox sequences must have equal lengths" };
    std::vector<wot::VehicleFakeGearConfiguration> result;
    for (size_t index = 0; index < speed.size(); ++index)
        result.push_back( { speed[index], hysteresis[index], low[index], high[index] } );
    return result;
}
}

bool readStockVehiclePhysicsConfiguration( PyObject * object,
    wot::VehiclePhysicsConfiguration & output, bool configured )
{
    try
    {
        const StockPhysicsConfigValue root( object, "WGVehiclePhysics.configure" );
        wot::VehiclePhysicsConfiguration result{};
        result.vehicle_type = root.child( "vehicleType" ).integral();
        if (result.vehicle_type != 0)
        {
            // f4fe59 rethrows with no active exception (terminate).
            PyErr_SetString( PyExc_ValueError, "WGVehiclePhysics.configure requires Tank vehicleType 0" );
            return false;
        }
        PyObjectPtr entries = root.child( "modes" ).items();
        const Py_ssize_t count = PyList_GET_SIZE( entries.get() );
        for (Py_ssize_t index = 0; index < count; ++index)
        {
            PyObject * pair = PyList_GET_ITEM( entries.get(), index );
            const StockPhysicsConfigValue key( PyTuple_GET_ITEM( pair, 0 ), "modes.key" );
            const StockPhysicsConfigValue value( PyTuple_GET_ITEM( pair, 1 ), "modes.value" );
            wot::VehiclePhysicsModeConfiguration mode{};
            mode.name = key.byteStringName();
            mode.mode_index = value.child( "mode_index" ).integral();
            readStockPhysicsTank( value, mode );
            // f814d0 sizes the table from the registry when f900a0 constructs it.
            mode.chassis.grounds.assign( wot::vehicle_ground_material_registry().count,
                wot::VehicleGroundSettings() );
            try // f900a0 states 22-23
            {
                readStockPhysicsChassis( value.child( "chassis" ), mode.chassis );
            }
            catch (const Caught &) {}
            try // f900a0 states 29-30
            {
                readStockPhysicsEngine( value.child( "engine" ), mode.engine );
            }
            catch (const Caught &) {}
            result.modes.push_back( std::move( mode ) );
        }
        readStockPhysicsGlobals( root, result );
        if (!configured && result.modes.empty())
        {
            // f84e80 reads the root into a discarded Vehicle::Settings when
            // the Tank holds no mode settings.
            wot::VehicleBodyConfiguration discarded;
            readStockPhysicsBody( root, discarded );
        }
        // f84e80: modes.normal is looked up before its catch region.
        const auto normal = root.child( "modes" ).child( "normal" );
        try
        {
            wot::VehicleFakeGearboxConfiguration gears;
            gears.forward = readStockPhysicsGears( normal.child( "fakegearbox" ).child( "fwdgears" ) );
            gears.backward = readStockPhysicsGears( normal.child( "fakegearbox" ).child( "bkwdgears" ) );
            result.gears = std::move( gears );
        }
        catch (const Caught &) {}
        output = std::move( result );
        return true;
    }
    catch (const StockPhysicsProxyFailure & failure)
    {
        // Uncaught in #1513 as well: it unwinds past the Python method.
        PyErr_SetString( failure.type, failure.message.c_str() );
        return false;
    }
    catch (const StockPhysicsPythonFailure &)
    {
        return false;
    }
    catch (const std::bad_alloc &)
    {
        PyErr_NoMemory();
        return false;
    }
    catch (const std::length_error &)
    {
        PyErr_SetString( PyExc_OverflowError, "Physics configuration sequence exceeds native storage" );
        return false;
    }
}

bool configureStockVehiclePhysics( wot::VehiclePhysics & core, PyObject * object,
    const wot::VehicleNativeMath & math )
{
    // The Reader may reenter Python, including a nested configure on this
    // object. A newer committed epoch wins; the outer result is discarded.
    const std::uint64_t epoch = core.configuration_epoch();
    wot::VehiclePhysicsConfiguration configuration;
    if (!readStockVehiclePhysicsConfiguration( object, configuration, core.tank() != nullptr ))
        return false;
    if (core.configuration_epoch() != epoch)
    {
        PyErr_SetString( PyExc_RuntimeError,
            "WGVehiclePhysics.configure was superseded by a nested configure" );
        return false;
    }
    try
    {
        core.configure( configuration, math );
        return true;
    }
    catch (const std::invalid_argument & error)
    {
        PyErr_SetString( PyExc_ValueError, error.what() );
    }
    catch (const std::length_error &)
    {
        PyErr_SetString( PyExc_OverflowError, "Physics configuration exceeds native storage" );
    }
    catch (const std::bad_alloc &)
    {
        PyErr_NoMemory();
    }
    catch (const std::exception & error)
    {
        PyErr_SetString( PyExc_RuntimeError, error.what() );
    }
    return false;
}

bool setupStockGroundMaterialsMap( PyObject * groups )
{
    // f5fde0: reset the map and store the group count before validating the
    // groups. Any invalid group restores count 1 with an all -1 map.
    auto & registry = wot::vehicle_ground_material_registry();
    registry.groups.fill( -1 );
    const auto invalid = [&registry]()
    {
        PyErr_SetString( PyExc_TypeError,
            "wg_setupGroundMaterialsMap: argument 2 must be a sequence of sequences of matKinds." );
        registry.groups.fill( -1 );
        registry.count = 1;
        return false;
    };
    if (!PySequence_Check( groups )) return invalid();
    const Py_ssize_t count = PySequence_Size( groups );
    if (count < 0) return invalid();
    registry.count = static_cast<std::uint32_t>( count );
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObjectPtr group( PySequence_GetItem( groups, index ), PyObjectPtr::STEAL_REFERENCE );
        if (!group || !PySequence_Check( group.get() )) return invalid();
        const Py_ssize_t size = PySequence_Size( group.get() );
        if (size < 0) return invalid();
        for (Py_ssize_t item = 0; item < size; ++item)
        {
            PyObjectPtr kind( PySequence_GetItem( group.get(), item ), PyObjectPtr::STEAL_REFERENCE );
            if (!kind || !PyInt_Check( kind.get() )) return invalid();
            const unsigned long value = static_cast<unsigned long>( PyInt_AS_LONG( kind.get() ) );
            if (value > 0xff) return invalid();
            registry.groups[value] = static_cast<std::int32_t>( index );
        }
    }
    return true;
}

// This wrapper owns precisely one authoritative core. Presentation objects
// retain the wrapper and consume its published outputs; they do not integrate
// their own body or substitute the old offline controller.
class PyWGVehiclePhysics : public PyObjectPlusWithWeakReference
{
    Py_Header( PyWGVehiclePhysics, PyObjectPlusWithWeakReference )

public:
    PyWGVehiclePhysics() : PyObjectPlusWithWeakReference( &s_type_ ),
        ownerEntityID_( 0 ),
        collisionProvider_( makeStockGroundCollisionProvider() )
    {
        core_.set_collision_provider( collisionProvider_.get() );
    }

    wot::VehiclePhysics & core() { return core_; }

    PyObject * pyGet_owner();
    int pySet_owner( PyObject * value );

    // #1513 f50b00 returns the published entity matrix, not the rigid body's
    // centre-of-mass transform. f56240 owns the offset correction.
    PY_RO_ATTRIBUTE_DECLARE( nativeMatrix( core_.entity_transform() ), matrix )
    PY_RO_ATTRIBUTE_DECLARE( nativeVector( core_.linear_velocity() ), velocity )
    PY_RO_ATTRIBUTE_DECLARE( nativeVector( core_.angular_velocity() ), angVelocity )
    PY_RO_ATTRIBUTE_DECLARE( core_.track_state().left_scroll, leftTrackScroll )
    PY_RO_ATTRIBUTE_DECLARE( core_.track_state().right_scroll, rightTrackScroll )

    PY_METHOD_DECLARE( py_configure )
    PyObject * pyGet_hullCOMZ();
    PY_RO_ATTRIBUTE_SET( hullCOMZ )
    PyObject * pyGet_isFrozen() { return Script::getData( core_.frozen() ); }
    int pySet_isFrozen( PyObject * value );
    PY_RW_ATTRIBUTE_DECLARE( core_.freeze_epsilons[0], freezeAccelEpsilon )
    PY_RW_ATTRIBUTE_DECLARE( core_.freeze_epsilons[1], freezeAngAccelEpsilon )
    PY_RW_ATTRIBUTE_DECLARE( core_.freeze_epsilons[2], freezeVelEpsilon )
    PY_RW_ATTRIBUTE_DECLARE( core_.freeze_epsilons[3], freezeAngVelEpsilon )
    PY_RW_ATTRIBUTE_DECLARE( core_.simulation_y_bound, simulationYBound )

    Vector3 centerOfMass() const
    {
        return nativeVector( core_.center_of_mass() );
    }
    void centerOfMass( Vector3 value )
    {
        core_.set_center_of_mass( { value.x, value.y, value.z } );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( Vector3, centerOfMass, centerOfMass )

    uint32 movementSignals() const { return core_.inputs().movement_signals; }
    void movementSignals( uint32 value )
    {
        wot::VehiclePhysicsInputs inputs = core_.inputs();
        inputs.movement_signals = value;
        core_.set_inputs( inputs );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( uint32, movementSignals, movementSignals )

    uint32 cruiseSignals() const { return core_.inputs().cruise_signals; }
    void cruiseSignals( uint32 value )
    {
        wot::VehiclePhysicsInputs inputs = core_.inputs();
        inputs.cruise_signals = value;
        core_.set_inputs( inputs );
    }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( uint32, cruiseSignals, cruiseSignals )

    bool staticMode() const { return core_.static_mode(); }
    void staticMode( bool value ) { core_.set_static_mode( value ); }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, staticMode, staticMode )

    uint32 visibilityMask() const { return core_.visibility_mask(); }
    void visibilityMask( uint32 value ) { core_.set_visibility_mask( value ); }
    PY_RW_ACCESSOR_ATTRIBUTE_DECLARE( uint32, visibilityMask, visibilityMask )

    void setArenaBounds( Vector2 minimum, Vector2 maximum )
    {
        // #1513 f4ab00 stores both Vector2 inputs without local reordering.
        core_.set_arena_bounds( {{ minimum.x, minimum.y }},
            {{ maximum.x, maximum.y }} );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, setArenaBounds,
        ARG( Vector2, ARG( Vector2, END ) ) )

    void removeAllDamperSprings()
    {
        core_.clear_damper_springs();
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, removeAllDamperSprings, END )

    PyObject * addDamperSpringRecord( PyObject * args )
    {
        PyObject * value;
        if (!PyArg_ParseTuple( args, "O:addDamperSpring", &value ))
            return NULL;
        if (!PyTuple_Check( value ))
        {
            PyErr_SetString( PyExc_TypeError,
                "WGVehiclePhysics.addDamperSpring expects a tuple" );
            return NULL;
        }
        if (PyTuple_GET_SIZE( value ) != 5)
        {
            PyErr_SetString( PyExc_TypeError,
                "addDamperSpring expects a five-element spring record" );
            return NULL;
        }
        Vector3 mount;
        wot::VehicleDamperSpring spring;
        if (Script::setData( PyTuple_GET_ITEM( value, 0 ), mount ) != 0 ||
            Script::setData( PyTuple_GET_ITEM( value, 1 ), spring.carrying_length ) != 0 ||
            Script::setData( PyTuple_GET_ITEM( value, 2 ), spring.indent ) != 0 ||
            Script::setData( PyTuple_GET_ITEM( value, 3 ), spring.left ) != 0 ||
            Script::setData( PyTuple_GET_ITEM( value, 4 ), spring.hard_ratio ) != 0)
            return NULL;
        spring.mount_point = { mount.x, mount.y, mount.z };
        // #1513 f49b60 stores the six floats in the selected side's vector;
        // it does not evaluate a suspension force or clamp these lengths.
        core_.add_damper_spring( spring );
        Py_RETURN_NONE;
    }
    PY_METHOD_DECLARE( py_addDamperSpring )

    Vector3 getPointVelocity( Vector3 point ) const
    {
        return nativeVector( core_.point_velocity( { point.x, point.y, point.z } ) );
    }
    PY_AUTO_METHOD_DECLARE( RETDATA, getPointVelocity, ARG( Vector3, END ) )

    void applyImpulse( Vector3 point, Vector3 impulse )
    {
        core_.apply_impulse( { point.x, point.y, point.z },
            { impulse.x, impulse.y, impulse.z } );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, applyImpulse,
        ARG( Vector3, ARG( Vector3, END ) ) )

    void applyImpulseToCoM( Vector3 impulse )
    {
        core_.apply_impulse_to_com( { impulse.x, impulse.y, impulse.z } );
    }
    PY_AUTO_METHOD_DECLARE( RETVOID, applyImpulseToCoM, ARG( Vector3, END ) )

    PY_METHOD_DECLARE( py_setDamperSpringsLength )

    static PyWGVehiclePhysics * New() { return new PyWGVehiclePhysics(); }
    PY_AUTO_FACTORY_DECLARE( PyWGVehiclePhysics, END )

private:
    // f556b0 retains the weak-reference object, never the Entity itself.
    PyObjectPtr owner_;
    EntityID ownerEntityID_;
    // #1513 creates one scene-obstacle query cache per physics wrapper.
    // Its lifetime encloses the core's non-owning provider reference.
    std::unique_ptr<wot::VehicleCollisionProvider> collisionProvider_;
    wot::VehiclePhysics core_;
};

PyObject * PyWGVehiclePhysics::pyGet_owner()
{
    // f7ca60 reports TypeError even when this owner has not been assigned.
    PyErr_Format( PyExc_TypeError,
        "Sorry, the attribute owner in %s is write-only", this->typeName() );
    return NULL;
}

int PyWGVehiclePhysics::pySet_owner( PyObject * value )
{
    if (!value || value == Py_None)
    {
        PyObjectPtr previous;
        std::swap( owner_, previous );
        // The original clears the reference without resetting its cached ID.
        return 0;
    }
    if (!PyWeakref_Check( value ))
    {
        PyErr_SetString( PyExc_TypeError,
            "WGVehiclePhysics.owner requires a weak reference to an Entity" );
        return -1;
    }
    PyObject * target = PyWeakref_GetObject( value );
    if (!target) return -1;
    if (!PyEntity::Check( target ))
    {
        // f556b0 rejects a dead/non-Entity referent without setting an error.
        // Preserve rejection but supply the exception required by CPython.
        PyErr_SetString( PyExc_TypeError,
            "WGVehiclePhysics.owner requires a live Entity weak reference" );
        return -1;
    }
    PyObjectPtr targetPin( target );
    PyEntity * entity = static_cast<PyEntity *>( target );
    if (entity->isDestroyed())
    {
        PyErr_SetString( PyExc_ValueError,
            "WGVehiclePhysics.owner cannot reference a destroyed Entity" );
        return -1;
    }
    const EntityID identity = entity->entityID();
    PyObjectPtr previous( value );
    std::swap( owner_, previous );
    ownerEntityID_ = identity;
    // Publish both fields before releasing the previous weakref: a subclass
    // finalizer may reenter and install a newer owner during that release.
    return 0;
}

PyObject * PyWGVehiclePhysics::py_addDamperSpring( PyObject * args )
{
    return this->addDamperSpringRecord( args );
}

PyObject * PyWGVehiclePhysics::py_setDamperSpringsLength( PyObject * args )
{
    PyObject * leftValue;
    PyObject * rightValue;
    if (!PyArg_ParseTuple( args, "OO:setDamperSpringsLength", &leftValue, &rightValue ))
        return NULL;
    std::vector<float> lengths[2];
    PyObject * values[] = { leftValue, rightValue };
    for (size_t side = 0; side < 2; ++side)
    {
        PyObjectPtr sequence( PySequence_Fast( values[side],
            "setDamperSpringsLength expects two float sequences" ),
            PyObjectPtr::STEAL_REFERENCE );
        if (!sequence)
            return NULL;
        const Py_ssize_t count = PySequence_Fast_GET_SIZE( sequence.get() );
        lengths[side].reserve( static_cast<size_t>( count ) );
        for (Py_ssize_t index = 0; index < count; ++index)
        {
            float value;
            if (Script::setData( PySequence_Fast_GET_ITEM( sequence.get(), index ), value ) != 0)
                return NULL;
            lengths[side].push_back( value );
        }
    }
    // #1513 validates both side counts before changing either side.
    if (!core_.set_damper_springs_lengths( lengths[0], lengths[1] ))
    {
        PyErr_SetString( PyExc_ValueError,
            "setDamperSpringsLength requires one length per existing spring" );
        return NULL;
    }
    Py_RETURN_NONE;
}

PyObject * PyWGVehiclePhysics::py_configure( PyObject * args )
{
    PyObject * value;
    if (!PyArg_ParseTuple( args, "O:configure", &value ))
        return NULL;
    const wot::VehicleNativeMath * math;
    try
    {
        math = &stock_vehicle_native_math::services();
    }
    catch (const std::exception & error)
    {
        PyErr_SetString( PyExc_RuntimeError, error.what() );
        return NULL;
    }
    // #1513 f4fedd returns AL=1 only after f900a0 completes.
    if (!configureStockVehiclePhysics( core_, value, *math )) return NULL;
    Py_RETURN_TRUE;
}

PyObject * PyWGVehiclePhysics::pyGet_hullCOMZ()
{
    // The original getter dereferences the Tank without a null check.
    if (!core_.tank())
    {
        PyErr_SetString( PyExc_ValueError, "hullCOMZ requires a configured WGVehiclePhysics" );
        return NULL;
    }
    return Script::getData( core_.hull_com_z() );
}

int PyWGVehiclePhysics::pySet_isFrozen( PyObject * value )
{
    bool frozen;
    if (Script::setData( value, frozen, "isFrozen" ) != 0) return -1;
    try
    {
        core_.set_frozen( frozen );
    }
    catch (const std::logic_error & error)
    {
        PyErr_SetString( PyExc_ValueError, error.what() );
        return -1;
    }
    return 0;
}

PY_TYPEOBJECT( PyWGVehiclePhysics )
PY_BEGIN_METHODS( PyWGVehiclePhysics )
    PY_METHOD( removeAllDamperSprings )
    PY_METHOD( addDamperSpring )
    PY_METHOD( getPointVelocity )
    PY_METHOD( applyImpulse )
    PY_METHOD( applyImpulseToCoM )
    PY_METHOD( setDamperSpringsLength )
    PY_METHOD( setArenaBounds )
    PY_METHOD( configure )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyWGVehiclePhysics )
    PY_ATTRIBUTE( owner )
    PY_ATTRIBUTE( matrix )
    PY_ATTRIBUTE( velocity )
    PY_ATTRIBUTE( angVelocity )
    PY_ATTRIBUTE( centerOfMass )
    PY_ATTRIBUTE( movementSignals )
    PY_ATTRIBUTE( cruiseSignals )
    PY_ATTRIBUTE( staticMode )
    PY_ATTRIBUTE( visibilityMask )
    PY_ATTRIBUTE( leftTrackScroll )
    PY_ATTRIBUTE( rightTrackScroll )
    PY_ATTRIBUTE( hullCOMZ )
    PY_ATTRIBUTE( isFrozen )
    PY_ATTRIBUTE( freezeAccelEpsilon )
    PY_ATTRIBUTE( freezeAngAccelEpsilon )
    PY_ATTRIBUTE( freezeVelEpsilon )
    PY_ATTRIBUTE( freezeAngVelEpsilon )
    PY_ATTRIBUTE( simulationYBound )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyWGVehiclePhysics, "WGVehiclePhysics", BigWorld )

PyObject * py_wg_setupGroundMaterialsMap( PyObject * args )
{
    PyObject * groups;
    if (!PyArg_ParseTuple( args, "O:wg_setupGroundMaterialsMap", &groups )) return NULL;
    // The original returns None with the TypeError still set; raise it here.
    if (!setupStockGroundMaterialsMap( groups )) return NULL;
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION( wg_setupGroundMaterialsMap, BigWorld )

wot::VehiclePhysics * unwrapStockVehiclePhysics( PyObject * object )
{
    if (!object || !PyWGVehiclePhysics::Check( object ))
    {
        PyErr_SetString( PyExc_TypeError,
            "Expected a WGVehiclePhysics instance" );
        return NULL;
    }
    return &static_cast<PyWGVehiclePhysics *>( object )->core();
}

BW_END_NAMESPACE
