#include "pch.hpp"
#include "stock_turret_filter.hpp"

BW_BEGIN_NAMESPACE
int StockTurretFilter_token = 1;

StockTurretFilter::StockTurretFilter( PyWGEntityFilter * owner,
    const std::shared_ptr<StockEntityFilterMotion> & motion ) :
    StockEntityFilter( owner, motion )
{
}

void StockTurretFilter::updateVelocityReceipts(
    const StockAvatarFilterHelper::Waypoint & current,
    const StockAvatarFilterHelper::Waypoint & next )
{
    // 006e6330 always replaces acceleration after the base receipt update,
    // including when the base rejected non-input waypoints.
    StockEntityFilter::updateVelocityReceipts( current, next );
    motion_->predictedAcceleration.set( 0.f, -9.82f, 0.f );
    motion_->predictedAngularAcceleration.setZero();
}

bool StockTurretFilter::tryCopyState( const MovementFilter & source )
{
    // 006e6440 performs the base copy before checking the turret subtype.
    if (!StockEntityFilter::tryCopyState( source )) return false;
    const StockTurretFilter * other = dynamic_cast<const StockTurretFilter *>( &source );
    if (!other) return false;
    originalReserved_ = other->originalReserved_;
    return true;
}

PyWGTurretFilter::PyWGTurretFilter( PyTypeObject * type ) : PyWGEntityFilter( type ) {}

Filter * PyWGTurretFilter::getNewFilter()
{
    StockTurretFilter * filter = new StockTurretFilter( this,
        std::make_shared<StockEntityFilterMotion>() );
    this->applySettings( *filter );
    return filter;
}

PY_TYPEOBJECT( PyWGTurretFilter )
PY_BEGIN_METHODS( PyWGTurretFilter )
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES( PyWGTurretFilter )
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED( PyWGTurretFilter, "WGTurretFilter", BigWorld )
BW_END_NAMESPACE
