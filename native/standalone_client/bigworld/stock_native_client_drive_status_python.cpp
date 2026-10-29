#include "stock_native_client_drive_status.hpp"
#include "stock_native_client_query.hpp"
#include "stock_native_bot_descriptor.hpp"
#include "stock_native_bot_python.hpp"
#include <cmath>
#include <stdexcept>
namespace stock_native_client_drive {
namespace {
namespace shared=stock_native_client_state;
namespace p=stock_native_bot_python;
StatusDescriptor readDescriptor(PyObject*descriptor){
    StatusDescriptor out;
    for(const char*name:{"engineHealth","ammoBayHealth","fuelTankHealth","radioHealth","leftTrackHealth","rightTrackHealth","gunHealth","turretRotatorHealth","surveyingDeviceHealth"}){
        const auto pool=stock_native_bot_descriptor::devicePool(descriptor,name);if(pool.maximum)out.maxima[name]=*pool.maximum;if(pool.regeneration)out.regeneration[name]=*pool.regeneration;
    }
    auto type=p::value(descriptor,"type"),roles=p::value(type.get(),"crewRoles");
    const auto roster=stock_native_bot_descriptor::crewRoster(descriptor);
    if(roles.get()!=Py_None&&p::size(roles.get())!=Py_ssize_t(roster.size()))throw p::Error("Native player crew metadata has inconsistent widths");
    for(std::size_t i=0;i<roster.size();++i){auto row=p::item(roles.get(),i);auto&entry=out.crew_roles[roster[i]];
        for(Py_ssize_t j=0;j<p::size(row.get());++j){auto role=p::item(row.get(),j);entry.insert(p::string(role.get()));}}
    return out;
}
}
struct StatusOwner::Impl {
    struct Entry {std::shared_ptr<shared::Actor> actor;std::uint64_t descriptor_revision=0,accepted_revision=0;PyObject*descriptor=nullptr;shared::Value effective;std::shared_ptr<Status> status;};
    std::map<std::string,Entry> entries;
    std::uint64_t epoch=0;
};
StatusOwner::StatusOwner():impl_(new Impl){}
StatusOwner::~StatusOwner()=default;
void StatusOwner::synchronize(shared::State&state,double now,std::optional<double>server){
    if(state.retired)throw std::runtime_error("Native player status battle retired");
    if(!std::isfinite(now))throw std::invalid_argument("Native player status clock is invalid");
    if(impl_->epoch!=state.epoch){impl_->entries.clear();impl_->epoch=state.epoch;}
    std::set<std::string> live;
    for(const auto&row:state.actors){auto actor=row.second;if(!actor||!actor->ready||actor->tombstone)continue;
        auto*descriptor=stock_native_client_query::descriptor(*actor);if(!descriptor||descriptor==Py_None)continue;
        live.insert(row.first);auto&entry=impl_->entries[row.first];
        // Native query owns the descriptor's lifetime. Identity is only a
        // resource fence; accepted mutable state is fenced by its revision.
        const auto&effective=actor->state.get("effective_params");const bool accepted_changed=!entry.status||entry.accepted_revision!=state.revision;
        if(entry.actor!=actor||entry.descriptor_revision!=actor->descriptor_revision||entry.descriptor!=descriptor||(accepted_changed&&entry.effective!=effective)){
            auto metadata=readDescriptor(descriptor);const auto&critical=effective.get("critical").resolved();
            // The accepted loadout snapshot is the exact _CriticalDescriptorView
            // producer. Its pools supersede resource descriptor pools, including
            // equipment/crew modifiers and the absence of an individual pool.
            if(critical.kind!=shared::Value::Kind::Null){metadata.maxima.clear();metadata.regeneration.clear();const auto&devices=critical.get("devices").resolved();
                if(devices.kind!=shared::Value::Kind::Array)throw std::invalid_argument("Native accepted critical device pools are malformed");
                for(const auto&device:devices.array){const auto&name=device.get("name").resolved().text;if(name.empty())throw std::invalid_argument("Native accepted critical pool has no name");
                    metadata.maxima[name]=device.get("max_hp").numeric();metadata.regeneration[name]=device.get("regen_hp").numeric();}}
            if(entry.actor==actor&&entry.status)entry.status->descriptor(std::move(metadata));else entry.status=installStatus(actor,std::move(metadata));
            entry.actor=actor;entry.descriptor_revision=actor->descriptor_revision;entry.descriptor=descriptor;entry.effective=effective;
        }
        entry.status->update(state.revision,server);entry.accepted_revision=state.revision;
    }
    for(auto at=impl_->entries.begin();at!=impl_->entries.end();)if(!live.count(at->first))at=impl_->entries.erase(at);else ++at;
}
}
