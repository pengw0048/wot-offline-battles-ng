#pragma once

#include "pyscript/script.hpp"
#include "math/matrix.hpp"
#include <functional>
#include <memory>

BW_BEGIN_NAMESPACE
extern int StockVehicleWheels_token;

// Original IWheelsDataProvider (vft 014d86b8). Native consumers hold a weak
// view; a destroyed or replaced WheelsAnimator publishes no wheels.
class StockWheelsDataProvider
{
public:
    virtual ~StockWheelsDataProvider() {}
    virtual size_t wheelCount() const = 0;
    virtual float wheelRadius( size_t index ) const = 0;
    virtual bool wheelIsLeft( size_t index ) const = 0;
    // The compound world matrix of the wheel node; false for an unlinked node.
    virtual bool wheelWorld( size_t index, Matrix & world ) const = 0;
};
std::shared_ptr<StockWheelsDataProvider> stockWheelsDataProvider( PyObject * animator );

// Exact LodProcessing::LodSettings converter 0077efe0 shared by animators.
bool readStockLodSettings( PyObject * object, float & maxDistance, uint32 & maxPriority );

// Svarog phase 2 WheelsAnimatorManager (005ae520, vft slot 0 0078c3b0).
void tickStockVehicleWheels( float frameTime );

// A WGSplineTrack publishes the float functor setSplineTrackMovementData
// installs for leading wheels (lambda 8811d2af).
typedef std::function<bool(float &)> StockSplineMovementReader;
bool stockSplineTrackMovementReader( PyObject * track, StockSplineMovementReader & reader );
BW_END_NAMESPACE
