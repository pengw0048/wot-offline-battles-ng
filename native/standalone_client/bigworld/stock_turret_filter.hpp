#pragma once

#include "stock_entity_filter.hpp"

BW_BEGIN_NAMESPACE

class StockTurretFilter : public StockEntityFilter
{
public:
    StockTurretFilter( PyWGEntityFilter * owner,
        const std::shared_ptr<StockEntityFilterMotion> & motion );
protected:
    virtual bool tryCopyState( const MovementFilter & source ) override;
    virtual void updateVelocityReceipts( const StockAvatarFilterHelper::Waypoint & current,
        const StockAvatarFilterHelper::Waypoint & next ) override;
private:
    // #1513 initializes and copies this byte; no behavioral consumer was observed.
    bool originalReserved_ = false;
};

class PyWGTurretFilter : public PyWGEntityFilter
{
    Py_Header( PyWGTurretFilter, PyWGEntityFilter )
public:
    explicit PyWGTurretFilter( PyTypeObject * type = &s_type_ );
    static PyWGTurretFilter * New() { return new PyWGTurretFilter(); }
    // 006e6270 reuses this original diagnostic label; This::New still creates a turret.
    PY_AUTO_FACTORY_DECLARE( PyWGVehicleFilter, END )
protected:
    virtual Filter * getNewFilter() override;
};

extern int StockTurretFilter_token;
BW_END_NAMESPACE
