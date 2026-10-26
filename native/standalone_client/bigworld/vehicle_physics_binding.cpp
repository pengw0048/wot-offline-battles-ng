#include "pch.hpp"
#include "vehicle_physics_binding.hpp"
#include "stock_scene_collision.hpp"

#include "pyscript/script_math.hpp"
#include "wot/vehicle_physics.hpp"

#include <memory>
#include <cmath>
#include <d3dx9math.h>
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
class StockPhysicsConfigValue
{
public:
    StockPhysicsConfigValue( PyObject * value, std::string path ) :
        value_( value ), path_( std::move( path ) ) {}

    StockPhysicsConfigValue child( const char * key ) const
    {
        if (!this->dictionary()) return StockPhysicsConfigValue( NULL, path_ );
        PyObject * value = PyDict_GetItemString( value_.get(), key );
        if (!value)
            PyErr_Format( PyExc_ValueError, "%s: dictionary has no field '%s'", path_.c_str(), key );
        return StockPhysicsConfigValue( value, path_ + "." + key );
    }
    bool dictionary() const
    {
        if (!value_) return false;
        if (PyDict_Check( value_.get() )) return true;
        PyErr_Format( PyExc_TypeError, "%s: object is not a dictionary", path_.c_str() );
        return false;
    }
    bool floating( float & result ) const
    {
        if (!value_) return false;
        PyNumberMethods * number = Py_TYPE( value_.get() )->tp_as_number;
        if (!PyNumber_Check( value_.get() ) || !number || !number->nb_float)
        {
            PyErr_Format( PyExc_TypeError, "%s: object is not floating", path_.c_str() );
            return false;
        }
        const double value = PyFloat_AsDouble( value_.get() );
        if (PyErr_Occurred()) return false;
        result = static_cast<float>( value );
        return true;
    }
    bool integral( std::int32_t & result ) const
    {
        if (!value_) return false;
        PyNumberMethods * number = Py_TYPE( value_.get() )->tp_as_number;
        if (!PyNumber_Check( value_.get() ) || !number || !number->nb_int)
        {
            PyErr_Format( PyExc_TypeError, "%s: object is not integral", path_.c_str() );
            return false;
        }
        const long value = PyInt_AsLong( value_.get() );
        if (PyErr_Occurred()) return false;
        // The original ABI is Win32 long, even in this portable reader probe.
        if (value < INT32_MIN || value > INT32_MAX)
        {
            PyErr_Format( PyExc_OverflowError, "%s: integer does not fit the native long", path_.c_str() );
            return false;
        }
        result = static_cast<std::int32_t>( value );
        return true;
    }
    bool boolean( bool & result ) const
    {
        if (!value_) return false;
        if (!PyBool_Check( value_.get() ))
        {
            PyErr_Format( PyExc_TypeError, "%s: object is not boolean", path_.c_str() );
            return false;
        }
        result = value_.get() == Py_True;
        return true;
    }
    bool sequence( std::vector<float> & output ) const
    {
        if (!value_) return false;
        if (!PySequence_Check( value_.get() ))
        {
            PyErr_Format( PyExc_TypeError, "%s: object is not a sequence", path_.c_str() );
            return false;
        }
        const Py_ssize_t count = PySequence_Size( value_.get() );
        if (count < 0) return false;
        std::vector<float> result;
        result.reserve( static_cast<size_t>( count ) );
        for (Py_ssize_t index = 0; index < count; ++index)
        {
            PyObjectPtr item( PySequence_GetItem( value_.get(), index ), PyObjectPtr::STEAL_REFERENCE );
            float number;
            if (!StockPhysicsConfigValue( item.get(), path_ + "[]" ).floating( number )) return false;
            result.push_back( number );
        }
        output.swap( result );
        return true;
    }
    template<size_t Count>
    bool fixed( std::array<float, Count> & output ) const
    {
        if (!value_) return false;
        if (!PySequence_Check( value_.get() ))
        {
            PyErr_Format( PyExc_TypeError, "%s: object is not a sequence", path_.c_str() );
            return false;
        }
        const Py_ssize_t count = PySequence_Size( value_.get() );
        if (count < 0) return false;
        if (count != static_cast<Py_ssize_t>( Count ))
        {
            PyErr_Format( PyExc_ValueError, "%s: sequence has the wrong item count", path_.c_str() );
            return false;
        }
        for (size_t index = 0; index < Count; ++index)
        {
            PyObjectPtr item( PySequence_GetItem( value_.get(), static_cast<Py_ssize_t>( index ) ),
                PyObjectPtr::STEAL_REFERENCE );
            if (!StockPhysicsConfigValue( item.get(), path_ + "[]" ).floating( output[index] )) return false;
        }
        return true;
    }
    bool vector( wot::Vector3 & output ) const
    {
        if (!value_) return false;
        if (!PySequence_Check( value_.get() ))
        {
            PyErr_Format( PyExc_TypeError, "%s: object is not a sequence", path_.c_str() );
            return false;
        }
        const Py_ssize_t count = PySequence_Size( value_.get() );
        if (count < 0) return false;
        if (count != 3)
        {
            PyErr_Format( PyExc_ValueError, "%s: vector requires three items", path_.c_str() );
            return false;
        }
        // f83d20 obtains and validates all three objects before converting any.
        std::array<PyObjectPtr, 3> items;
        for (size_t index = 0; index < 3; ++index)
        {
            items[index] = PyObjectPtr( PySequence_GetItem( value_.get(), static_cast<Py_ssize_t>( index ) ),
                PyObjectPtr::STEAL_REFERENCE );
            if (!items[index]) return false;
        }
        for (const auto & item : items)
        {
            PyNumberMethods * number = Py_TYPE( item.get() )->tp_as_number;
            if (!PyNumber_Check( item.get() ) || !number || !number->nb_float)
            {
                PyErr_Format( PyExc_TypeError, "%s: vector item is not floating", path_.c_str() );
                return false;
            }
        }
        std::array<float, 3> values;
        for (size_t index = 0; index < 3; ++index)
            if (!StockPhysicsConfigValue( items[index].get(), path_ + "[]" ).floating( values[index] ))
                return false;
        output = { values[0], values[1], values[2] };
        return true;
    }
    PyObjectPtr items() const
    {
        if (!this->dictionary()) return PyObjectPtr();
        // f81c30 snapshots PyDict_Next order before converting entry values.
        return PyObjectPtr( PyDict_Items( value_.get() ), PyObjectPtr::STEAL_REFERENCE );
    }
    std::string byteStringName() const
    {
        // The native dispatch does not constrain mode-key types. A byte name
        // is retained as optional diagnostic metadata, never as a mode index.
        return value_ && PyString_Check( value_.get() ) ?
            std::string( PyString_AS_STRING( value_.get() ), PyString_GET_SIZE( value_.get() ) ) : std::string();
    }
private:
    PyObjectPtr value_;
    std::string path_;
};

bool readStockPhysicsBody( const StockPhysicsConfigValue & value, wot::VehicleBodyConfiguration & result )
{
    return value.child( "gravity" ).floating( result.gravity ) &&
        value.child( "fullMass" ).floating( result.full_mass ) &&
        value.child( "hullInertiaFactors" ).vector( result.hull_inertia_factors ) &&
        value.child( "hullSize" ).vector( result.hull_size ) &&
        value.child( "hullCOMShiftY" ).floating( result.hull_com_shift_y ) &&
        value.child( "hullBoxOffsetZ" ).floating( result.hull_box_offset_z ) &&
        value.child( "turretTopPos" ).vector( result.turret_top_position ) &&
        value.child( "turretTopWidth" ).floating( result.turret_top_width ) &&
        value.child( "smplFwMaxSpeed" ).floating( result.simple_forward_max_speed ) &&
        value.child( "smplBkMaxSpeed" ).floating( result.simple_backward_max_speed ) &&
        value.child( "smplRotSpeed" ).floating( result.simple_rotation_speed ) &&
        value.child( "angVelocityFactor" ).floating( result.angular_velocity_factor ) &&
        value.child( "allowedRPMExcess" ).floating( result.allowed_rpm_excess ) &&
        value.child( "overspeedResistFactor" ).floating( result.overspeed_resistance_factor ) &&
        value.child( "isRotationAroundCenter" ).boolean( result.rotation_around_center ) &&
        value.child( "centerRotationFwdSpeed" ).floating( result.center_rotation_forward_speed ) &&
        value.child( "movementRevertSpeed" ).floating( result.movement_revert_speed ) &&
        value.child( "comFrictionYOffs" ).floating( result.com_friction_y_offset ) &&
        value.child( "comSideFriction" ).floating( result.com_side_friction ) &&
        value.child( "enableSabilization" ).boolean( result.stabilization_enabled ) &&
        value.child( "gimlet" ).child( "wPushedRot" ).floating( result.gimlet_pushed_rotation ) &&
        value.child( "gimlet" ).child( "wPushedHB" ).floating( result.gimlet_pushed_handbrake );
}

bool readStockPhysicsShape( const StockPhysicsConfigValue & value, wot::VehicleShapeConfiguration & result )
{
    return value.child( "useComplexForm" ).boolean( result.use_complex_form ) &&
        value.child( "auxClearance" ).floating( result.auxiliary_clearance ) &&
        value.child( "tankBoardAngle" ).floating( result.tank_board_angle ) &&
        value.child( "tankAftChamferFraction" ).floating( result.tank_aft_chamfer_fraction ) &&
        value.child( "tankFrontChamferFraction" ).floating( result.tank_front_chamfer_fraction ) &&
        value.child( "terrBoardAngle" ).floating( result.terrain_board_angle ) &&
        value.child( "terrFrontChamferFraction" ).floating( result.terrain_front_chamfer_fraction ) &&
        value.child( "terrAftChamferFraction" ).floating( result.terrain_aft_chamfer_fraction );
}

bool readStockPhysicsGroundSurface( const StockPhysicsConfigValue & value, wot::VehicleGroundSurfaceConfiguration & result )
{
    return value.child( "dirtCumulationRate" ).floating( result.dirt_cumulation_rate ) &&
        value.child( "dirtReleaseRate" ).floating( result.dirt_release_rate ) &&
        value.child( "dirtSideVelocity" ).floating( result.dirt_side_velocity ) &&
        value.child( "maxDirt" ).floating( result.max_dirt ) &&
        value.child( "hbComSideFriction" ).floating( result.handbrake_com_side_friction ) &&
        value.child( "sideFriction" ).floating( result.side_friction ) &&
        value.child( "fwdFriction" ).floating( result.forward_friction ) &&
        value.child( "rollingFriction" ).floating( result.rolling_friction ) &&
        value.child( "hbSideFrictionAddition" ).floating( result.handbrake_side_friction_addition ) &&
        value.child( "rotationFactor" ).floating( result.rotation_factor );
}

bool readStockPhysicsGrounds( const StockPhysicsConfigValue & value,
    std::vector<wot::VehicleGroundConfiguration> & output )
{
    PyObjectPtr entries = value.items();
    if (!entries) return false;
    const Py_ssize_t count = PyList_GET_SIZE( entries.get() );
    for (Py_ssize_t index = 0; index < count; ++index)
    {
        PyObject * pair = PyList_GET_ITEM( entries.get(), index );
        StockPhysicsConfigValue key( PyTuple_GET_ITEM( pair, 0 ), "grounds.key" );
        StockPhysicsConfigValue item( PyTuple_GET_ITEM( pair, 1 ), "grounds.value" );
        wot::VehicleGroundConfiguration result;
        if (!key.integral( result.material_kind ) ||
            !readStockPhysicsGroundSurface( item.child( "firm" ), result.firm ) ||
            !readStockPhysicsGroundSurface( item.child( "medium" ), result.medium ) ||
            !readStockPhysicsGroundSurface( item.child( "soft" ), result.soft )) return false;
        output.push_back( result );
    }
    return true;
}

bool readStockPhysicsChassisBase( const StockPhysicsConfigValue & value, wot::VehicleChassisConfiguration & result )
{
    return value.child( "chassisMass" ).floating( result.chassis_mass ) &&
        value.child( "chassisMassFraction" ).floating( result.chassis_mass_fraction ) &&
        value.child( "clearance" ).floating( result.clearance ) &&
        value.child( "trackGaugeFactor" ).floating( result.track_gauge_factor ) &&
        value.child( "wheelDetachOnRoll" ).boolean( result.wheel_detach_on_roll ) &&
        value.child( "trackToBeLockedDelay" ).floating( result.track_to_be_locked_delay ) &&
        value.child( "wheelRadius" ).floating( result.wheel_radius ) &&
        value.child( "wheelInertiaFactor" ).floating( result.wheel_inertia_factor ) &&
        value.child( "wheelFwdInertiaFactor" ).floating( result.wheel_forward_inertia_factor ) &&
        value.child( "stiffness0" ).floating( result.stiffness_zero ) &&
        value.child( "stiffness1" ).floating( result.stiffness_one ) &&
        value.child( "damping" ).floating( result.damping ) &&
        value.child( "wheelRestitution" ).floating( result.wheel_restitution ) &&
        value.child( "wheelPenetration" ).floating( result.wheel_penetration ) &&
        value.child( "wheelUsePseudoContacts" ).boolean( result.wheel_use_pseudo_contacts ) &&
        value.child( "brake" ).floating( result.brake ) &&
        value.child( "rotationBrake" ).floating( result.rotation_brake ) &&
        value.child( "wheelSinkageResistFactor" ).floating( result.wheel_sinkage_resistance_factor ) &&
        value.child( "wheelDryFrictionFactor" ).floating( result.wheel_dry_friction_factor ) &&
        value.child( "sideFrictionConstantRatio" ).floating( result.side_friction_constant_ratio ) &&
        value.child( "roadWheelPositions" ).fixed( result.road_wheel_positions ) &&
        value.child( "stiffnessFactors" ).fixed( result.stiffness_factors ) &&
        value.child( "slopeResistTerrain" ).fixed( result.slope_resistance_terrain ) &&
        value.child( "slopeResistStaticObject" ).fixed( result.slope_resistance_static_object ) &&
        value.child( "slopeResistDynamicObject" ).fixed( result.slope_resistance_dynamic_object ) &&
        value.child( "slopeGripLngTerrain" ).fixed( result.slope_grip_forward_terrain ) &&
        value.child( "slopeGripSdwTerrain" ).fixed( result.slope_grip_side_terrain ) &&
        value.child( "slopeGripLngStaticObject" ).fixed( result.slope_grip_forward_static_object ) &&
        value.child( "slopeGripSdwStaticObject" ).fixed( result.slope_grip_side_static_object ) &&
        value.child( "slopeGripLngDynamicObject" ).fixed( result.slope_grip_forward_dynamic_object ) &&
        value.child( "slopeGripSdwDynamicObject" ).fixed( result.slope_grip_side_dynamic_object ) &&
        value.child( "flatSideFriction" ).boolean( result.flat_side_friction ) &&
        readStockPhysicsGrounds( value.child( "grounds" ), result.grounds );
}

bool readStockPhysicsChassis( const StockPhysicsConfigValue & value,
    wot::VehicleChassisConfiguration & result )
{
    if (!readStockPhysicsChassisBase( value, result )) return false;
    const auto pitch = value.child( "hullAiming" ).child( "pitch" );
    if (!(pitch.child( "correctionCenterZ" ).floating( result.pitch_correction_center_z ) &&
        pitch.child( "correctionSpeed" ).floating( result.pitch_correction_speed ) &&
        pitch.child( "pitchMin" ).floating( result.pitch_min ) &&
        pitch.child( "pitchMax" ).floating( result.pitch_max ) &&
        pitch.child( "correctionStiffness" ).floating( result.pitch_correction_stiffness ) &&
        pitch.child( "correctionDamping" ).floating( result.pitch_correction_damping ) &&
        pitch.child( "correctionScale" ).floating( result.pitch_correction_scale ))) return false;
    const auto yaw = value.child( "hullAiming" ).child( "yaw" );
    return yaw.child( "gimletForce" ).floating( result.yaw_gimlet_force ) &&
        yaw.child( "stiffness" ).floating( result.yaw_stiffness ) &&
        yaw.child( "damping" ).floating( result.yaw_damping ) &&
        yaw.child( "preciseRestitution" ).floating( result.yaw_precise_restitution ) &&
        yaw.child( "dampingYawDist" ).floating( result.yaw_damping_distance ) &&
        yaw.child( "preciseYawDist" ).floating( result.yaw_precise_distance );
}

bool readStockPhysicsEngine( const StockPhysicsConfigValue & value, wot::VehicleEngineConfiguration & result )
{
    return value.child( "engineLoses" ).fixed( result.losses ) &&
        value.child( "smplEnginePower" ).floating( result.simple_engine_power ) &&
        value.child( "engineInertia" ).floating( result.engine_inertia ) &&
        value.child( "smplEngJoinRatio" ).floating( result.simple_engine_join_ratio ) &&
        value.child( "idleChoker" ).floating( result.idle_choker ) &&
        value.child( "rotationChoker" ).floating( result.rotation_choker ) &&
        value.child( "rotationByLockChoker" ).floating( result.rotation_by_lock_choker ) &&
        value.child( "smplMinRPM" ).floating( result.simple_min_rpm ) &&
        value.child( "idleRPM" ).floating( result.idle_rpm ) &&
        value.child( "startRPM" ).floating( result.start_rpm ) &&
        value.child( "engVelMax" ).floating( result.maximum_forward_angular_velocity ) &&
        value.child( "engVelBkMax" ).floating( result.maximum_backward_angular_velocity ) &&
        value.child( "engVelRot" ).floating( result.rotation_angular_velocity );
}

bool readStockPhysicsGimlet( const StockPhysicsConfigValue & value, wot::VehicleGimletConfiguration & result )
{
    return value.child( "wPushedDiag" ).floating( result.pushed_diagonal_weight ) &&
        value.child( "wPushedRot" ).floating( result.pushed_rotation_weight ) &&
        value.child( "wPushedHB" ).floating( result.pushed_handbrake_weight ) &&
        value.child( "pushInContact" ).floating( result.push_in_contact ) &&
        value.child( "pushHB" ).floating( result.push_handbrake ) &&
        value.child( "pushStop" ).floating( result.push_stop ) &&
        value.child( "pushRotOnSpotFixedPeriod" ).floating( result.on_spot_fixed_period ) &&
        value.child( "pushRotOnSpotGrowPeriod" ).floating( result.on_spot_grow_period ) &&
        value.child( "gimletPushOnSpotInit" ).floating( result.on_spot_initial_push ) &&
        value.child( "gimletPushOnSpotFinal" ).floating( result.on_spot_final_push ) &&
        value.child( "pushRotOnMoveFixedPeriod" ).floating( result.on_move_fixed_period ) &&
        value.child( "pushRotOnMoveGrowPeriod" ).floating( result.on_move_grow_period ) &&
        value.child( "gimletPushOnMoveInit" ).floating( result.on_move_initial_push ) &&
        value.child( "gimletPushOnMoveFinal" ).floating( result.on_move_final_push ) &&
        value.child( "gimletVelScaleMin" ).floating( result.minimum_velocity_scale ) &&
        value.child( "gimletVelScaleMax" ).floating( result.maximum_velocity_scale );
}

bool readStockPhysicsGears( const StockPhysicsConfigValue & value,
    std::vector<wot::VehicleFakeGearConfiguration> & result )
{
    std::vector<float> speed, hysteresis, low, high;
    if (!value.child( "switchSpeed" ).sequence( speed ) ||
        !value.child( "switchHysteresis" ).sequence( hysteresis ) ||
        !value.child( "lowRpm" ).sequence( low ) ||
        !value.child( "highRpm" ).sequence( high )) return false;
    if (speed.size() != hysteresis.size() || speed.size() != low.size() || speed.size() != high.size())
    {
        PyErr_SetString( PyExc_ValueError, "fake gearbox sequences must have equal lengths" );
        return false;
    }
    for (size_t index = 0; index < speed.size(); ++index)
        result.push_back( { speed[index], hysteresis[index], low[index], high[index] } );
    return true;
}
}

bool readStockVehiclePhysicsConfiguration( PyObject * object,
    wot::VehiclePhysicsConfiguration & output )
{
    try
    {
        const StockPhysicsConfigValue root( object, "WGVehiclePhysics.configure" );
        wot::VehiclePhysicsConfiguration result{};
        if (!root.child( "vehicleType" ).integral( result.vehicle_type )) return false;
        if (result.vehicle_type != 0)
        {
            PyErr_SetString( PyExc_ValueError, "WGVehiclePhysics.configure requires Tank vehicleType 0" );
            return false;
        }
        const auto modes = root.child( "modes" );
        PyObjectPtr entries = modes.items();
        if (!entries) return false;
        const Py_ssize_t count = PyList_GET_SIZE( entries.get() );
        for (Py_ssize_t index = 0; index < count; ++index)
        {
            PyObject * pair = PyList_GET_ITEM( entries.get(), index );
            const StockPhysicsConfigValue key( PyTuple_GET_ITEM( pair, 0 ), "modes.key" );
            const StockPhysicsConfigValue value( PyTuple_GET_ITEM( pair, 1 ), "modes.value" );
            wot::VehiclePhysicsModeConfiguration mode{};
            mode.name = key.byteStringName();
            if (!value.child( "mode_index" ).integral( mode.mode_index ) ||
                !readStockPhysicsBody( value, mode.body ) ||
                !readStockPhysicsShape( value.child( "shape" ), mode.shape ) ||
                !value.child( "siegeModeAvailable" ).boolean( mode.siege_mode_available ) ||
                !readStockPhysicsChassis( value.child( "chassis" ), mode.chassis ) ||
                !readStockPhysicsEngine( value.child( "engine" ), mode.engine )) return false;
            result.modes.push_back( std::move( mode ) );
        }
        const auto normal = modes.child( "normal" );
        const auto anchor = normal.child( "anchor" );
        const auto rail = normal.child( "rail" );
        if (!anchor.child( "anchorMaxReactionFactor" ).floating( result.anchor_maximum_reaction_factor ) ||
            !anchor.child( "anchorConstFraction" ).floating( result.anchor_constant_fraction ) ||
            !anchor.child( "anchorVelFactor" ).floating( result.anchor_velocity_factor ) ||
            !rail.child( "railFactorInContact" ).floating( result.rail_factor_in_contact ) ||
            !readStockPhysicsGimlet( normal.child( "gimlet" ), result.gimlet ) ||
            !readStockPhysicsGears( root.child( "fakegearbox" ).child( "fwdgears" ), result.forward_gears ) ||
            !readStockPhysicsGears( root.child( "fakegearbox" ).child( "bkwdgears" ), result.backward_gears )) return false;
        output = std::move( result );
        return true;
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

// This wrapper owns precisely one authoritative core. Presentation objects
// retain the wrapper and consume its published outputs; they do not integrate
// their own body or substitute the old offline controller.
class PyWGVehiclePhysics : public PyObjectPlusWithWeakReference
{
    Py_Header( PyWGVehiclePhysics, PyObjectPlusWithWeakReference )

public:
    PyWGVehiclePhysics() : PyObjectPlusWithWeakReference( &s_type_ ),
        collisionProvider_( makeStockGroundCollisionProvider() )
    {
        core_.set_collision_provider( collisionProvider_.get() );
    }

    wot::VehiclePhysics & core() { return core_; }

    // #1513 f50b00 returns the published entity matrix, not the rigid body's
    // centre-of-mass transform. f56240 owns the offset correction.
    PY_RO_ATTRIBUTE_DECLARE( nativeMatrix( core_.entity_transform() ), matrix )
    PY_RO_ATTRIBUTE_DECLARE( nativeVector( core_.linear_velocity() ), velocity )
    PY_RO_ATTRIBUTE_DECLARE( nativeVector( core_.angular_velocity() ), angVelocity )
    PY_RO_ATTRIBUTE_DECLARE( core_.track_state().left_scroll, leftTrackScroll )
    PY_RO_ATTRIBUTE_DECLARE( core_.track_state().right_scroll, rightTrackScroll )

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
    // #1513 creates one scene-obstacle query cache per physics wrapper.
    // Its lifetime encloses the core's non-owning provider reference.
    std::unique_ptr<wot::VehicleCollisionProvider> collisionProvider_;
    wot::VehiclePhysics core_;
};

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

PY_TYPEOBJECT( PyWGVehiclePhysics )
PY_BEGIN_METHODS( PyWGVehiclePhysics )
    PY_METHOD( removeAllDamperSprings )
    PY_METHOD( addDamperSpring )
    PY_METHOD( getPointVelocity )
    PY_METHOD( applyImpulse )
    PY_METHOD( applyImpulseToCoM )
    PY_METHOD( setDamperSpringsLength )
    PY_METHOD( setArenaBounds )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyWGVehiclePhysics )
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
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyWGVehiclePhysics, "WGVehiclePhysics", BigWorld )

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

namespace
{
wot::Vector3 normalizeStockVehicleConvexVector( wot::Vector3 value )
{
    const D3DXVECTOR3 source( value.x, value.y, value.z );
    D3DXVECTOR3 result;
    D3DXVec3Normalize( &result, &source );
    return { result.x, result.y, result.z };
}
}

wot::VehicleTankGeometry buildStockVehicleTankGeometry(
    const wot::VehicleBodyConfiguration & body,
    const wot::VehicleShapeConfiguration & shape,
    const wot::VehicleChassisConfiguration & chassis )
{
    // f8b8d0 calls tankBoardAngle first, then terrBoardAngle. In particular,
    // the embedded shapes use the terrain fields; do not infer their field
    // producers from the two storage owners' names.
    const float tankTangent = std::tan( shape.tank_board_angle * 0.017453292f );
    const float terrainTangent = std::tan( shape.terrain_board_angle * 0.017453292f );
    const wot::VehicleTankShapeSource source = wot::build_vehicle_tank_shape_source(
        body, shape, chassis, terrainTangent, tankTangent );
    D3DXMATRIX local;
    D3DXMatrixTranslation( &local, source.local_translation.x,
        source.local_translation.y, source.local_translation.z );
    const auto transform = [&local]( wot::Vector3 value )
    {
        const D3DXVECTOR3 input( value.x, value.y, value.z );
        D3DXVECTOR4 output;
        D3DXVec3Transform( &output, &input, &local );
        return wot::Vector3{ output.x, output.y, output.z };
    };
    wot::VehicleTankGeometry geometry{};
    wot::VehicleTankConvexSource translated;
    for (size_t index = 0; index != source.polyhedra.size(); ++index)
    {
        const auto & input = source.polyhedra[index];
        if ((index & 1) == 0) translated.vertices.clear();
        // The extended variant appends four top vertices to the same twelve
        // translated base vertices, matching the original SDK call order.
        for (size_t vertex = translated.vertices.size(); vertex != input.vertices.size(); ++vertex)
            translated.vertices.push_back( transform( input.vertices[vertex] ) );
        translated.faces = input.faces;
        geometry.polyhedra[index] = wot::build_vehicle_convex_topology(
            translated, &normalizeStockVehicleConvexVector );
    }
    geometry.has_auxiliary_box = source.has_auxiliary_box;
    if (geometry.has_auxiliary_box)
    {
        const wot::Vector3 a = transform( source.auxiliary_box_endpoints[0] );
        const wot::Vector3 b = transform( source.auxiliary_box_endpoints[1] );
        geometry.auxiliary_box_center = {
            (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f };
        geometry.auxiliary_box_dimensions = { a.x - b.x, a.y - b.y, a.z - b.z };
    }
    return geometry;
}

BW_END_NAMESPACE
