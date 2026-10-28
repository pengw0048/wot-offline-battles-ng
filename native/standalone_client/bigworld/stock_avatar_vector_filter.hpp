#pragma once

#include "cstdmf/bw_namespace.hpp"
#include "cstdmf/bw_vector.hpp"
#include "math/vector3.hpp"
#include <memory>

BW_BEGIN_NAMESPACE

// #1513 Vector3 sub-filter (0x1c bytes, 006803f0): the last output, a time-
// ordered sample history and the interpolation type set by Python.
class StockAvatarVectorChannel
{
public:
    struct Sample
    {
        Vector3 value;
        double time;
    };

    StockAvatarVectorChannel();

    // 00680a20: drop tail samples less than 0.01 s (plus one ulp) behind the
    // new time, append it and keep the newest eight once 24 are stored.
    void sync( double time, const Vector3 & value );
    // 00680540: interpolate at time minus the shared latency into output().
    bool evaluate( double time );
    // 00680be0
    void reset();

    const Vector3 & output() const { return output_; }
    int interpolationType() const { return interpolationType_; }
    void interpolationType( int value ) { interpolationType_ = value; }
    const BW::vector< Sample > & samples() const { return samples_; }

private:
    Vector3 output_;
    BW::vector< Sample > samples_;
    int interpolationType_;
};

// #1513 PyAvatarFilter sub-filter collection (AvatarFilter+0x2f8): ten lazily
// created channels owned by the attached native AvatarFilter.
class StockAvatarVectorChannels
{
public:
    static const unsigned NUM_CHANNELS = 10;

    // 0065d850: time 0 means the connection's lastMessageTime.
    void sync( int id, const Vector3 & value, double time );
    // 0065d780: an unknown channel yields the zero vector.
    Vector3 get( int id, double time );
    // 0065d7f0
    void reset( int id );
    // 0065d820
    void setInterpolationType( int id, int type );

    const StockAvatarVectorChannel * channel( int id ) const;

private:
    // 0065d620: identifiers above 9 (unsigned) are rejected.
    StockAvatarVectorChannel * acquire( int id );

    std::shared_ptr< StockAvatarVectorChannel > channels_[NUM_CHANNELS];
};

// #1513 01d24210..20: the latency all vector channels subtract on evaluation.
struct StockAvatarVectorLatency
{
    double latency;
    double maximum;
    double lastUpdate;
};
StockAvatarVectorLatency & stockAvatarVectorLatency();

// The connection's lastMessageTime and serverTime; false without a connection.
bool stockAvatarVectorLastMessageTime( double & time );
bool stockAvatarVectorServerTime( double & time );

BW_END_NAMESPACE
