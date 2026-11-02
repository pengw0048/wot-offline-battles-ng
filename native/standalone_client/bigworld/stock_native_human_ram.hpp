#pragma once
#include "stock_native_bot_armor_geometry.hpp"
#include "offline_simulation_types.h"
#include "stock_native_client_state.hpp"
#include <string>
struct _object;
namespace native_battle { struct Battle; }
namespace stock_native_human_ram {
using ID=std::int64_t;
using Body=stock_native_bot_armor::Body;
using Plates=stock_native_bot_armor::Plates;
using XZ=stock_native_bot_armor::XZ;
class World {
public:
    virtual ~World()=default;
    virtual bool ready(ID player)=0;
    virtual std::optional<Plates> armor(const Body&,const Body&,const XZ&)=0;
};
template<class Services,class Vehicles>class NativeWorld final:public World {
    Services& services_;
    Vehicles& vehicles_;
public:
    NativeWorld(Services&services,Vehicles&vehicles):services_(services),vehicles_(vehicles){}
    bool ready(ID id)override{
        const offline_simulation::ActorKey key{offline_simulation::ActorKind::Human,id};
        const auto*record=services_.find(key);const auto*vehicle=vehicles_.find(key);
        return record&&record->record_ready&&record->available&&vehicle&&vehicle->ready;
    }
    std::optional<Plates> armor(const Body&first,const Body&second,const XZ&normal)override{
        // These server probes contain historical chassis poses only. A current
        // hydraulic receipt cannot prove either historical body, even when the
        // chassis coordinates match. Return a terminal unavailable result;
        // readiness still describes resources, so the probe does not hang.
        for(const auto* body:{&first,&second}){
            const offline_simulation::ActorKey key{offline_simulation::ActorKind::Human,body->id};
            const auto* vehicle=vehicles_.find(key);
            if(vehicle&&vehicle->has_siege)return {};
        }
        auto a=first,b=second;a.id+=1000000;b.id+=1000000;
        const auto result=services_.armor(a,b,{{normal[0],normal[1],0.}});
        if(!result)return {};return Plates{result->value,result->tracks};
    }
};
// Reads the latest snapshot's human_ram_probes and vehicle/entity readiness, then invokes
// only typed native geometry. Bad requests are isolated to their own row;
// missing resources omit a reply so the server retains the exact substep.
// A baseline-only historical hydraulic query instead replies available=false.
stock_native_client_state::Value results(native_battle::Battle&,World&,std::vector<std::string>* errors=nullptr);
}
