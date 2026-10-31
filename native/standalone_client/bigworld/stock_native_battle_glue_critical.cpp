// Integration glue: the local crew-KO set other subsystems read from the critical owner.
#if !defined(WOT_NATIVE_BATTLE_HOST_BUILD)
#include "pch.hpp"
#endif
#include "stock_native_battle_subsystems.hpp"
#include "stock_native_battle_startup_needs.hpp"

namespace native_battle {
namespace critical {
// BattleRuntime._local_crew_critical: the accepted crew-KO set of the local entity, else the record's critical state.
p::Ref localCrewCritical(Battle& b) {
    auto crewKo = [](const std::set<std::string>& names) {
        p::Ref list(PyList_New(0));
        for (const auto& name : names) p::append(list.get(), p::text(name));   // std::set iterates sorted
        auto out = p::dict();
        p::set(out.get(), "crew_ko", std::move(list));
        return out;
    };
    const Actor* local = b.localVehicle();
    if (b.engine.server && local && local->engine_id)
        if (const auto* damage = critical::damage(b, local->engine_id)) return crewKo(damage->crew_ko);
    if (local) {
        // record.get('critical_state') or record['state'].get('critical')
        const Value& presented = local->presented.critical.resolved();
        const Value& critical = presented.truth() ? presented : local->presented.state.get("critical").resolved();
        if (critical.kind == Value::Kind::Object) return p::Ref(cs::write(critical));
    }
    return crewKo({});
}
}
}
