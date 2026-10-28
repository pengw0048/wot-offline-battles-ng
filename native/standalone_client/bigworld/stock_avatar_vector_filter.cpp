#include "pch.hpp"
#include "stock_avatar_vector_filter.hpp"

#include "avatar_filter.hpp"
#include "connection_control.hpp"
#include "connection_model/bw_connection.hpp"
#include "py_avatar_filter.hpp"

#include <algorithm>
#include <cmath>

BW_BEGIN_NAMESPACE

namespace
{
const float PI = 3.14159265f;
const float TWO_PI = 6.28318531f;
const float THREE_PI = 9.42477796f;
// #1513 0x14b250c (3e22f983): 1 / (2 * pi).
const float INVERSE_TWO_PI = 0.15915494f;
const size_t STORED_SAMPLES = 24;
const size_t KEPT_SAMPLES = 8;
// #1513 0x14b78a0 (3f847ae147ae147c), one ulp above 0.01.
const double SAMPLE_SPACING = 0.010000000000000002;

// #1513 00648be0: wrap an angle above pi or at/below -pi by whole turns.
float normaliseAngle( float angle )
{
    if (angle > PI)
    {
        if (angle > THREE_PI)
            return angle - std::floor( angle * INVERSE_TWO_PI + 0.5f ) * TWO_PI;
        return angle - TWO_PI;
    }
    if (-PI < angle || angle != angle)
        return angle;
    if (-THREE_PI < angle)
        return angle + TWO_PI;
    return angle - std::floor( angle * INVERSE_TWO_PI + 0.5f ) * TWO_PI;
}

// Interpolate on the shorter arc between two normalised angles.
float lerpAngle( float from, float to, float t )
{
    if (to > from + PI)
        to = to - TWO_PI;
    else if (from - PI > to)
        to = to + TWO_PI;
    return normaliseAngle( (to - from) * t + from );
}

float atan2Float( float y, float x )
{
    // fpatan rounded to float.
    return static_cast< float >( std::atan2( double( y ), double( x ) ) );
}

// #1513 011bb1a0 with the slerp flag set: spherical blend of direction and
// length, falling back to a straight blend for almost opposite yaws.
Vector3 slerpCartesian( const Vector3 & a, const Vector3 & b, float t )
{
    const float yawA = normaliseAngle( atan2Float( a.x, a.z ) );
    float yawB = normaliseAngle( atan2Float( b.x, b.z ) );
    float turn = yawB - yawA;
    if (0.f > turn)
        turn = turn + TWO_PI;
    turn = turn - PI;
    if (0.4f > std::fabs( turn ))
        return Vector3( a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t );

    const float yaw = lerpAngle( yawA, yawB, t );
    const float ax2 = a.x * a.x, az2 = a.z * a.z;
    const float pitchA = normaliseAngle( -atan2Float( a.y, std::sqrt( ax2 + az2 ) ) );
    const float bx2 = b.x * b.x, bz2 = b.z * b.z;
    const float pitchB = normaliseAngle( -atan2Float( b.y, std::sqrt( bx2 + bz2 ) ) );
    const float lengthA = std::sqrt( (a.y * a.y + ax2) + az2 );
    const float lengthB = std::sqrt( (b.y * b.y + bx2) + bz2 );
    const float pitch = lerpAngle( pitchA, pitchB, t );
    const float length = (lengthB - lengthA) * t + lengthA;
    // The public double-precision Vector3::setPitchYaw, scaled by length.
    const double cosPitch = cosf( pitch );
    const float x = static_cast< float >( double( sinf( yaw ) ) * cosPitch );
    const float y = sinf( -pitch );
    const float z = static_cast< float >( double( cosf( yaw ) ) * cosPitch );
    return Vector3( x * length, y * length, z * length );
}
}


StockAvatarVectorLatency & stockAvatarVectorLatency()
{
    static StockAvatarVectorLatency latency = { 0.0, 0.0, 0.0 };
    return latency;
}


StockAvatarVectorChannel::StockAvatarVectorChannel() :
    output_( 0.f, 0.f, 0.f ),
    interpolationType_( 0 )
{
    samples_.reserve( STORED_SAMPLES );
}


void StockAvatarVectorChannel::sync( double time, const Vector3 & value )
{
    while (!samples_.empty() && SAMPLE_SPACING > time - samples_.back().time)
        samples_.pop_back();
    const Sample sample = { value, time };
    samples_.push_back( sample );
    if (samples_.size() >= STORED_SAMPLES)
    {
        std::copy( samples_.end() - KEPT_SAMPLES, samples_.end(), samples_.begin() );
        samples_.resize( KEPT_SAMPLES );
    }

    // Every channel feeds one shared estimate of how late its samples arrive.
    double now = 0.0;
    if (!stockAvatarVectorServerTime( now ))
        return;
    StockAvatarVectorLatency & shared = stockAvatarVectorLatency();
    const double behind = now - time;
    if (behind > shared.maximum)
        shared.maximum = behind;
    const double maximum = shared.maximum;
    const double latency = shared.latency;
    const double elapsed = now - shared.lastUpdate;
    if (!((maximum > latency && elapsed > 0.0) || elapsed > 4.0))
        return;
    const double weight = maximum > latency ? 0.8 : 0.2;
    double blend = elapsed * 0.25;
    blend = 0.0 > blend ? 0.0 : (blend < 1.0 ? blend : 1.0);
    blend = blend * weight;
    shared.lastUpdate = now;
    shared.maximum = 0.0;
    shared.latency = (1.0 - blend) * latency + blend * maximum;
}


bool StockAvatarVectorChannel::evaluate( double time )
{
    if (samples_.empty())
        return false;
    const double target = time - stockAvatarVectorLatency().latency;
    const int last = int( samples_.size() ) - 1;
    int index = last;
    while (index >= 0 && !(target > samples_[index].time))
        --index;
    if (index < 0)
    {
        output_ = samples_.front().value;
        return true;
    }
    if (index == last)
    {
        output_ = samples_.back().value;
        return true;
    }

    const Sample & from = samples_[index];
    const Sample & to = samples_[index + 1];
    double fraction = (target - from.time) / (to.time - from.time);
    fraction = 0.0 > fraction ? 0.0 : (fraction < 1.0 ? fraction : 1.0);
    const float t = static_cast< float >( fraction );
    const Vector3 & a = from.value;
    const Vector3 & b = to.value;
    switch (interpolationType_)
    {
    case 1:
        output_ = slerpCartesian( a, b, t );
        break;
    case 2:
        output_ = Vector3(
            lerpAngle( normaliseAngle( a.x ), normaliseAngle( b.x ), t ),
            lerpAngle( normaliseAngle( a.y ), normaliseAngle( b.y ), t ),
            lerpAngle( normaliseAngle( a.z ), normaliseAngle( b.z ), t ) );
        break;
    case 3:
        output_ = Vector3( (b.x - a.x) * t + a.x,
            lerpAngle( normaliseAngle( a.y ), normaliseAngle( b.y ), t ),
            lerpAngle( normaliseAngle( a.z ), normaliseAngle( b.z ), t ) );
        break;
    default:
        output_ = Vector3( (b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y,
            (b.z - a.z) * t + a.z );
        break;
    }
    return true;
}


void StockAvatarVectorChannel::reset()
{
    output_ = Vector3( 0.f, 0.f, 0.f );
    samples_.clear();
}


StockAvatarVectorChannel * StockAvatarVectorChannels::acquire( int id )
{
    const unsigned index = static_cast< unsigned >( id );
    if (index >= NUM_CHANNELS)
        return NULL;
    if (!channels_[index])
        channels_[index] = std::make_shared< StockAvatarVectorChannel >();
    return channels_[index].get();
}


const StockAvatarVectorChannel * StockAvatarVectorChannels::channel( int id ) const
{
    const unsigned index = static_cast< unsigned >( id );
    return index < NUM_CHANNELS ? channels_[index].get() : NULL;
}


void StockAvatarVectorChannels::sync( int id, const Vector3 & value, double time )
{
    StockAvatarVectorChannel * channel = this->acquire( id );
    if (!channel)
        return;
    // #1513 dereferences a missing connection here; keep the channel unchanged.
    if (time == 0.0 && !stockAvatarVectorLastMessageTime( time ))
        return;
    channel->sync( time, value );
}


Vector3 StockAvatarVectorChannels::get( int id, double time )
{
    StockAvatarVectorChannel * channel = this->acquire( id );
    if (!channel)
        return Vector3( 0.f, 0.f, 0.f );
    channel->evaluate( time );
    return channel->output();
}


void StockAvatarVectorChannels::reset( int id )
{
    if (StockAvatarVectorChannel * channel = this->acquire( id ))
        channel->reset();
}


void StockAvatarVectorChannels::setInterpolationType( int id, int type )
{
    if (StockAvatarVectorChannel * channel = this->acquire( id ))
        channel->interpolationType( type );
}

bool stockAvatarVectorLastMessageTime( double & time )
{
    BWConnection * connection = ConnectionControl::instance().pConnection();
    if (!connection)
        return false;
    time = connection->lastMessageTime();
    return true;
}


bool stockAvatarVectorServerTime( double & time )
{
    BWConnection * connection = ConnectionControl::instance().pConnection();
    if (!connection)
        return false;
    time = connection->serverTime();
    return true;
}


namespace
{
// #1513 logs MF_ASSERT_DEV (subfilter_collection.hpp) and continues with the
// default result when the Python filter has no attached native filter.
void reportDetached( const char * method )
{
    ERROR_MSG( "MF_ASSERT_DEV FAILED: pFilter in PyAvatarFilter::%s\n", method );
}
}


void PyAvatarFilter::syncVector3( int id, const Vector3 & value, double time )
{
    AvatarFilter * filter = this->pAttachedFilter();
    if (!filter)
    {
        reportDetached( "syncVector3" );
        return;
    }
    filter->stockVectorChannels().sync( id, value, time );
}


Vector3 PyAvatarFilter::getVector3( int id, double time )
{
    AvatarFilter * filter = this->pAttachedFilter();
    if (!filter)
    {
        reportDetached( "getVector3" );
        return Vector3( 0.f, 0.f, 0.f );
    }
    return filter->stockVectorChannels().get( id, time );
}


void PyAvatarFilter::resetVector3( int id )
{
    AvatarFilter * filter = this->pAttachedFilter();
    if (!filter)
    {
        reportDetached( "resetVector3" );
        return;
    }
    filter->stockVectorChannels().reset( id );
}


void PyAvatarFilter::setInterpolationType( int id, int type )
{
    AvatarFilter * filter = this->pAttachedFilter();
    if (!filter)
    {
        reportDetached( "setInterpolationType" );
        return;
    }
    filter->stockVectorChannels().setInterpolationType( id, type );
}

BW_END_NAMESPACE
