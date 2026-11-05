#include "room_strategy_data.hpp"
#include <charconv>

namespace wot::room::strategy {
Value point(const Value& raw) {
    auto p = object();
    if (list(raw)) {
        const auto& a = rows(raw);
        p.object["x"] = number(a.empty() ? 0. : a[0].numeric());
        p.object["y"] = number(0.);
        p.object["z"] = number(a.size() > 1 ? a[1].numeric() : 0.);
    } else {
        p.object["x"] = number(rounded(std::clamp(n(raw,"x"), -2000., 2000.)));
        p.object["y"] = number(rounded(std::clamp(n(raw,"y"), -1000., 1000.)));
        p.object["z"] = number(rounded(std::clamp(n(raw,"z"), -2000., 2000.)));
    }
    return p;
}
double distance(const Value& a, const Value& c) { return std::hypot(n(a,"x") - n(c,"x"), n(a,"z") - n(c,"z")); }
static Value capturePosition(const Value& raw) {
    if(list(raw)) {
        const auto& values=rows(raw);auto out=object();int i=0;for(const char* axis:{"x","y","z"}){if(std::size_t(i)>=values.size())break;out.object[axis]=number(values[i++].numeric());}return out;
    }
    return record({{"x",number(n(raw,"x"))},{"y",number(n(raw,"y"))},{"z",number(n(raw,"z"))}});
}
bool inside(const Value& pose, const Value& area) { return distance(capturePosition(pose), area.get("point")) <= n(area,"radius"); }
Value constrainCapture(const Value& raw, const Value& pose) {
    auto order = raw.resolved(); const auto& area = order.get("capture_area"); if (!area.truth()) return order;
    const bool at = inside(pose,area);
    const Value& move = order.get("move_position").truth() ? order.get("move_position") : pose;
    const bool move_inside = inside(move,area);
    if (!at) {
        order.object["combat_mode"] = text("base_capture");
        order.object["move_position"] = area.get("point");
        order.object["throttle_override"] = Value();
    } else if ((oneOf(s(order,"combat_mode"),{"route","advance","base_capture"}) || !move_inside) &&
               !(has(order,"throttle_override") && !null(order.get("throttle_override")) && n(order,"throttle_override") == 0. && move_inside)) {
        order.object["combat_mode"] = text(null(order.get("target_id")) ? "base_capture" : "engage");
        order.object["move_position"] = capturePosition(pose);
        order.object["throttle_override"] = number(0.);
    } else return order;
    order.object["route_join"] = boolean(false); order.object["parking_phase"] = Value(); order.object["parking_slot"] = Value();
    return order;
}
bool emptyConfirmed(const Value& state, const Value& profile) {
    const auto& ammo = state.get("ammo_remaining"); const auto& shells = rows(profile.get("shells"));
    if (!list(ammo) || rows(ammo).empty() || (!shells.empty() && shells.size() != rows(ammo).size())) return false;
    for (const auto& raw : rows(ammo)) {
        const auto& v = raw.resolved();
        if ((v.kind != Value::Kind::Integer && v.kind != Value::Kind::Number) || v.numeric(-1.) != 0.) return false;
    }
    return true;
}
bool emptyActive(const Value& bot) { return b(bot,"empty_ammo") || emptyConfirmed(bot.get("state"),bot.get("profile")); }
bool weaponCanRecover(const Value& bot) {
    for (const auto& v : rows(bot.get("state").get("ammo_remaining"))) if (v.numeric() >= 1.) return true;
    return false;
}
bool weaponReady(const Value& bot) {
    if (!weaponCanRecover(bot)) return false;
    const auto& state = bot.get("state"); const auto& ammo = rows(state.get("ammo_remaining"));
    if (contains(state.get("critical").get("destroyed"),text("gunHealth"))) return false;
    const auto loaded = intField(state,"shell_index",-1);
    if (loaded < 0 || static_cast<std::uint64_t>(loaded) >= ammo.size() || ammo[static_cast<std::size_t>(loaded)].numeric() < 1.) return false;
    if (b(state,"burst_active")) return true;
    return !b(state,"ammo_reload_pending") && (!has(state,"clip") || n(state,"clip") > 0.) && n(state,"reload_time") <= 0.;
}
bool magazineFollowup(const Value& bot) {
    const auto& state = bot.get("state");
    return b(state,"burst_active") || (intField(state,"clip_size",1) > 1 && intField(state,"clip") > 0 && weaponCanRecover(bot));
}
static Value coverPoint(const Value& raw) {
    const bool sequence = list(raw); if ((!sequence && !dict(raw)) || (sequence && rows(raw).size()<3)) return {};
    auto out = object(); int i = 0;
    for (const char* axis : {"x","y","z"}) {
        if (!sequence && !has(raw,axis)) return {};
        const auto& value = sequence ? rows(raw)[i++] : raw.get(axis);
        const double x = value.numeric(std::numeric_limits<double>::quiet_NaN()); if (!std::isfinite(x)) return {};
        out.object[axis] = number(rounded(x));
    }
    return out;
}
Value normalizeCover(const Value& raw) {
    auto out = object();const auto& raw_id=raw.get("id").resolved();std::string supplied=s(raw,"id");
    if(raw_id.truth()&&raw_id.kind==Value::Kind::Integer)supplied=std::to_string(raw_id.integer);
    else if(raw_id.truth()&&raw_id.kind==Value::Kind::Boolean)supplied="True";
    else if(raw_id.truth()&&raw_id.kind==Value::Kind::Number){char buffer[64];const auto converted=std::to_chars(buffer,buffer+sizeof buffer,raw_id.number);supplied.assign(buffer,converted.ptr);if(supplied.find_first_of(".eE")==std::string::npos)supplied+=".0";}
    std::string key; for (unsigned char c : supplied) if (c<128 && key.size()<80) key.push_back(char(c));
    out.object["id"] = text(key); out.object["position"] = coverPoint(raw.get("position"));
    auto peek = coverPoint(raw.get("peek_position")); if (!null(peek)) out.object["peek_position"] = peek;
    for (const char* field : {"travel_distance","slope"}) out.object[field] = number(std::max(0.,rounded(n(raw,field))));
    for (const char* field : {"route_alignment","enemy_occlusion","exposure","water","ally_congestion"})
        out.object[field] = number(rounded(std::clamp(n(raw,field,std::string(field)=="exposure"?1.:0.),0.,1.),4));
    for (const char* field : {"peek_feasible","escape_feasible"}) {
        const auto& v = raw.get(field).resolved(); const bool yes = v.kind != Value::Kind::Text && v.numeric(-1.)==1.;
        out.object[field] = boolean(yes && (std::string(field)!="peek_feasible" || !null(peek)));
    }
    return out;
}
Value scoreCover(const Value& raw, const Value& weights) {
    static const std::pair<const char*,double> defaults[] = {
        {"travel_distance",-.055},{"route_alignment",22.},{"enemy_occlusion",32.},{"exposure",-28.},{"slope",-1.4},
        {"water",-55.},{"ally_congestion",-18.},{"peek_feasible",9.},{"escape_feasible",12.}};
    auto out = normalizeCover(raw), breakdown = object(); double score = 0.;
    for (const auto& entry : defaults) {
        const double part = rounded(n(out,entry.first)*n(weights,entry.first,entry.second));
        breakdown.object[entry.first] = number(part); score += part;
    }
    out.object["breakdown"] = std::move(breakdown); out.object["score"] = number(rounded(score));
    auto reasons = array();
    if (n(out,"water") >= .5) reasons.array.push_back(text("water_risk"));
    if (n(out,"slope") > 18.) reasons.array.push_back(text("steep_slope"));
    if (n(out,"enemy_occlusion") < .25) reasons.array.push_back(text("low_occlusion"));
    if (n(out,"exposure") > .75) reasons.array.push_back(text("high_exposure"));
    if (!b(out,"escape_feasible")) reasons.array.push_back(text("no_escape"));
    out.object["reasons"] = std::move(reasons); return out;
}
Value scoreCovers(const Value& candidates, const Value& weights) {
    auto out = array(); for (const auto& c : rows(candidates)) out.array.push_back(scoreCover(c,weights));
    std::stable_sort(out.array.begin(),out.array.end(),[](const Value& a,const Value& b) {
        return std::make_tuple(-n(a,"score"),n(a,"travel_distance"),s(a,"id")) < std::make_tuple(-n(b,"score"),n(b,"travel_distance"),s(b,"id"));
    });
    for (std::size_t i=0;i<out.array.size();++i) out.array[i].object["rank"] = integer(i+1);
    return out;
}
Value personality(ID actor) {
    const auto value = (std::uint64_t(actor)*1103515245ull+12345ull)&0x7fffffff;
    return record({{"aggression",number(rounded(.35+(value%41)/100.))},{"caution",number(rounded(.25+((value>>8)%41)/100.))},
        {"teamwork",number(rounded(.30+((value>>12)%51)/100.))},{"patience",number(rounded(.25+((value>>16)%56)/100.))},
        {"initiative",number(rounded(.25+((value>>20)%61)/100.))},{"adaptability",number(rounded(.30+((value>>4)%51)/100.))},
        {"jiggle",number(rounded(.18+((value>>6)%65)/100.))}});
}
Value crossfireRisk(const Value& bot,const std::vector<Value>& contacts) {
    std::vector<double> bearings;const auto& pose=bot.get("state");
    for(const auto& c:contacts)if(b(c,"visible")&&contains(c.get("threatened_bot_ids"),integer(id(bot)))) {
        const double d=distance(c.get("position"),pose);if(d<=1.||d>360.)continue;
        bearings.push_back(std::atan2(n(c.get("position"),"x")-n(pose,"x"),n(c.get("position"),"z")-n(pose,"z")));
    }
    if(bearings.size()<2)return {};double best=0.;const double minimum=55.*3.14159265358979323846/180.;
    for(std::size_t i=0;i<bearings.size();++i)for(std::size_t j=i+1;j<bearings.size();++j) {
        const double delta=bearings[i]-bearings[j],angle=std::abs(std::atan2(std::sin(delta),std::cos(delta)));if(angle<minimum)continue;
        best=std::max(best,std::clamp((angle-minimum)/(3.14159265358979323846/2.),0.,1.));
    }
    return number(rounded(best));
}
bool diskReached(const Value& position,const Value& target,const Value& following,const Value& previous) {
    if (distance(position,target)<=6.) return true;
    if (!null(previous)) {
        const double dx=n(position,"x")-n(previous,"x"),dz=n(position,"z")-n(previous,"z"),len=dx*dx+dz*dz;
        if (len>1.e-6) {
            const double fraction=std::clamp(((n(target,"x")-n(previous,"x"))*dx+(n(target,"z")-n(previous,"z"))*dz)/len,0.,1.);
            if (std::hypot(n(target,"x")-n(previous,"x")-fraction*dx,n(target,"z")-n(previous,"z")-fraction*dz)<=6.) return true;
        }
    }
    if (null(following)) return false;
    const double dx=n(following,"x")-n(target,"x"),dz=n(following,"z")-n(target,"z"),len=dx*dx+dz*dz;
    if (len<=1.e-6) return false;
    const double fraction=((n(position,"x")-n(target,"x"))*dx+(n(position,"z")-n(target,"z"))*dz)/len;
    return fraction>0. && std::hypot(n(position,"x")-n(target,"x")-std::min(1.,fraction)*dx,n(position,"z")-n(target,"z")-std::min(1.,fraction)*dz)<=6.;
}
}
