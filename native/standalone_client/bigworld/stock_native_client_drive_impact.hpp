#pragma once
#include "stock_native_client_drive_publication.hpp"
#include "stock_native_bot_internal_geometry.hpp"
#if !defined(WOT_CLIENT_IMPACT_HOST_BUILD)
#include "stock_native_bot_vehicle_geometry.hpp"
#endif
namespace stock_native_client_drive {
struct ImpactConfig {int maximum_health=0;double safe_speed=0.,maximum_speed=0.;std::size_t maximum_pending=0;};
class PlayerImpact final:public step::ImpactWorld {
    step::InternalLayout interior_;
    const stock_native_bot_geometry::Vehicle*vehicle_=nullptr;
    stock_native_bot_geometry::Pose pose_;
    PublicationLedger&publication_;ImpactConfig config_;
    double last_world_impact_=-1.e30;
    int observe(double,const std::vector<Point>&,const Point&);
public:
    PlayerImpact(PublicationLedger&,ImpactConfig,step::InternalLayout);
    void geometry(const stock_native_bot_geometry::Vehicle&,stock_native_bot_geometry::Pose);
    double safeSpeed()const{return config_.safe_speed;}
    void metadata(ImpactConfig config,step::InternalLayout interior){config_=std::move(config);interior_=std::move(interior);}
    int landing(PlayerState&,double,bool normal_impact,const std::vector<Point>&,
        const Point&normal,std::optional<Point>landing_position={});
    double world(const PlayerState&,const ContactTrace&,double speed,double yaw,double now);
    std::vector<Point>ground_witnesses()override{return {};}
    std::vector<step::ImpactHit>material_segment(const Point&,const Point&)override;
    std::vector<step::ImpactHit>interior_segment(const Point&,const Point&)override;
};
}
