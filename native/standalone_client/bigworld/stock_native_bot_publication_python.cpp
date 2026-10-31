#include "stock_native_bot_publication_python.hpp"
#include "stock_native_bot_value.hpp"

namespace stock_native_bot {
namespace {
namespace v=stock_native_bot_value;
using stock_native_client_state::Value;
const char* kind(bool human){return human?"human":"bot";}
const char* kind(Key k){return kind(k.kind==offline_simulation::ActorKind::Human);}
Value identity(Key k){auto out=v::object();out.object["kind"]=v::text(kind(k));out.object["id"]=v::integer(k.id);return out;}
Value proof(const std::vector<double>&values){
    if(values.size()!=14)throw v::Error("Native artillery launch proof width mismatch");
    const char* kinds[]={"","bot","human"};const int target=int(values[1]);if(target<0||target>2)throw v::Error("Native artillery target kind invalid");
    auto out=v::array();auto&a=out.array;
    a.push_back(v::text("launch"));a.push_back(v::integer(std::int64_t(values[0])));a.push_back(v::text(kinds[target]));
    for(int i=2;i<5;++i)a.push_back(v::integer(std::int64_t(values[std::size_t(i)])));
    a.push_back(v::numbers(std::array<double,3>{{values[5],values[6],values[7]}}));
    for(std::size_t i=8;i<14;++i)a.push_back(v::number(values[i]));return out;
}
Value launch(const offline_simulation::weapons::LaunchRecord&l){
    auto out=v::object();auto&d=out.object;const auto&e=l.edge;
    d["id"]=v::integer(l.actor.id);d["fire_seq"]=v::integer(e.shot_seq);d["shell_index"]=v::integer(e.shell);
    d["shot_yaw"]=v::number(l.yaw);d["shot_pitch"]=v::number(l.pitch);d["class_tag"]=v::text(l.class_tag);
    d["burst_group_seq"]=v::integer(e.group_seq);d["burst_index"]=v::integer(e.index);d["burst_count"]=v::integer(e.count);
    d["launch_time_us"]=v::integer(l.launch_time_us);d["launch_pose"]=v::numbers(std::array<double,6>{{l.pose.position[0],l.pose.position[1],l.pose.position[2],l.pose.yaw,l.pose.pitch,l.pose.roll}});
    if(l.has_origin)d["shot_origin"]=v::numbers(l.origin);
    if(l.artillery){d["shot_velocity"]=v::numbers(l.velocity);d["shot_gravity"]=v::number(l.gravity);d["shot_max_distance"]=v::number(l.maximum_distance);d["shot_max_time_ms"]=v::integer(l.maximum_time_ms);d["shot_proof_key"]=proof(l.proof);}
    if(l.has_shells_before)d["shells_before_shot"]=v::integer(l.shells_before);return out;
}
Value ram(const contacts::Report&r){
    auto out=v::object();auto&d=out.object;d["type"]=v::text("bot_ram");d["bot_id"]=v::integer(r.bot);
    d["target_kind"]=v::text(kind(r.target_human));d["target_id"]=v::integer(r.target);d["ram_seq"]=v::integer(r.sequence);
    d["damage_to_bot"]=v::integer(r.damage_bot);d["damage_to_target"]=v::integer(r.damage_target);
    if(!r.bot_tracks.empty()||!r.target_tracks.empty()){d["track_contacts_bot"]=v::strings(r.bot_tracks);d["track_contacts_target"]=v::strings(r.target_tracks);d["track_impact_speed"]=v::number(std::max(0.,r.closing_speed));}
    if(r.target_human){d["ram_contact_player_id"]=v::integer(r.target);d["ram_contact_seq"]=v::integer(r.player_receipt);}
    else d["contact_positions"]=v::numbers(r.positions);return out;
}
Value packObservation(const observation::Observation&o){
    auto out=v::object();auto&d=out.object;d["type"]=v::text("bot_observation");auto contacts=v::array();
    for(const auto&c:o.contacts){auto row=v::object();auto&r=row.object;
        r["observing_team"]=v::integer(c.observing_team);r["target_kind"]=v::text(kind(c.target));r["target_id"]=v::integer(c.target.id);r["target_team"]=v::integer(c.target_team);
        r["visible"]=v::boolean(c.visible);r["fresh"]=v::boolean(c.fresh);r["time_left"]=v::number(c.time_left);auto recipients=v::array();
        for(const auto&a:c.radio_recipients){auto x=identity(a.actor);x.object["time_left"]=v::number(a.time_left);recipients.array.push_back(std::move(x));}r["radio_recipients"]=std::move(recipients);
        r["visible_by_player_ids"]=v::integers(c.visible_by_player_ids);r["visible_by_bot_ids"]=v::integers(c.visible_by_bot_ids);
        r["shootable_by_bot_ids"]=v::integers(c.shootable_by_bot_ids);r["threatened_bot_ids"]=v::integers(c.threatened_bot_ids);
        r["x"]=v::number(c.position[0]);r["y"]=v::number(c.position[1]);r["z"]=v::number(c.position[2]);
        r["health"]=v::integer(c.health);r["max_health"]=v::integer(c.max_health);r["class_tag"]=v::text(c.class_tag);r["armor"]=v::number(c.armor);contacts.array.push_back(std::move(row));
    }d["contacts"]=std::move(contacts);auto links=v::array();
    for(const auto&l:o.radio_links){auto row=identity(l.actor),allies=v::array();for(const auto&k:l.allies)allies.array.push_back(identity(k));row.object["allies"]=std::move(allies);links.array.push_back(std::move(row));}d["radio_links"]=std::move(links);
    auto ranges=v::array();for(const auto&r:o.player_vision_ranges){auto row=v::object();row.object["id"]=v::integer(r.first);row.object["radius"]=v::number(r.second);ranges.array.push_back(std::move(row));}d["player_vision_ranges"]=std::move(ranges);
    auto affordances=v::array();for(const auto&a:o.affordances){auto row=v::object();row.object["bot_id"]=v::integer(a.bot_id);row.object["target_kind"]=v::text(a.target_kind);row.object["target_id"]=v::integer(a.target_id);auto candidates=v::array();
        for(const auto&c:a.candidates){auto x=v::object();x.object["id"]=v::text(c.id);x.object["position"]=v::numbers(c.position);x.object["peek_position"]=v::numbers(c.peek_position);
            const char*names[]={"travel_distance","route_alignment","enemy_occlusion","exposure","slope","water","ally_congestion"};const double values[]={c.travel_distance,c.route_alignment,c.enemy_occlusion,c.exposure,c.slope,c.water,c.ally_congestion};
            for(unsigned i=0;i<7;++i)x.object[names[i]]=v::number(values[i]);x.object["peek_feasible"]=v::boolean(c.peek_feasible);x.object["escape_feasible"]=v::boolean(c.escape_feasible);candidates.array.push_back(std::move(x));
        }row.object["candidates"]=std::move(candidates);affordances.array.push_back(std::move(row));
    }d["affordances"]=std::move(affordances);return out;
}
}
Value packNativeOutputs(const std::vector<Output>&values,std::int64_t horizon){
    auto out=v::array();for(const auto&o:values){const auto&s=o.state;auto row=v::object();auto&d=row.object;d["type"]=v::text("bot_state");auto rows=v::array();
        for(const auto&r:s.rows)rows.array.push_back(v::integers(r));d["rows"]=std::move(rows);d["sample_time_us"]=v::integer(s.sample_us);d["edge_sample_time_us"]=v::integer(s.edge_sample_us);d["edge_revision"]=v::integer(s.edge_revision);d["source_batch_horizon_us"]=v::integer(horizon);
        auto push=v::object(),contacts=v::array();for(const auto&c:o.pushes){auto x=v::object();x.object["seq"]=v::integer(c.sequence);x.object["attacker_kind"]=v::text(kind(c.attacker_human));x.object["attacker_id"]=v::integer(c.attacker);x.object["target_kind"]=v::text(kind(c.target_human));x.object["target_id"]=v::integer(c.target);contacts.array.push_back(std::move(x));}
        push.object["contacts"]=std::move(contacts);push.object["hazards"]=v::integers(o.hazards);push.object["environment_fires"]=v::integers(o.environment_fires);
        auto falls=v::array();for(const auto&f:o.falls)falls.array.push_back(v::integers(f));push.object["falls"]=std::move(falls);auto critical=v::array();
        for(const auto&c:o.critical){auto x=v::array();x.array.push_back(v::integer(c.actor));x.array.push_back(v::text(c.cause));x.array.push_back(v::integer(c.mask));x.array.push_back(v::integer(c.count));critical.array.push_back(std::move(x));}push.object["critical_totals"]=std::move(critical);d["push_contacts"]=std::move(push);
        if(!s.launches.empty()){auto launches=v::array();for(const auto&l:s.launches)launches.array.push_back(launch(l));d["launches"]=std::move(launches);}out.array.push_back(std::move(row));
        for(const auto&r:s.reports)out.array.push_back(ram(r));if(o.observation)out.array.push_back(packObservation(*o.observation));
    }return out;
}
}
