#include "stock_native_bot_publication.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace stock_native_bot_publication {
namespace {
constexpr double pi=3.14159265358979323846;
const std::array<std::string,9> devices{{"ammoBayHealth","engineHealth","fuelTankHealth","gunHealth",
    "leftTrackHealth","radioHealth","rightTrackHealth","surveyingDeviceHealth","turretRotatorHealth"}};
const std::array<std::string,8> crew{{"commander","driver","gunner1","gunner2","loader1","loader2","radioman1","radioman2"}};
constexpr std::array<double,37> scales{{0.,0.,10000.,10000.,10000.,100000.,100000.,100000.,100000.,100000.,
    10000.,0.,0.,0.,0.,0.,1000000.,1000000.,0.,0.,0.,0.,1000000.,1000000.,0.,0.,0.,0.,0.,0.,0.,1000000.,1000000.,0.,0.,100000.,100000.}};
double wrapped(double value)
{const double shifted=value+pi;double result=std::fmod(shifted,2.*pi);if(result<0.)result+=2.*pi;return result-pi;}
ID integral(double value)
{
    if(!std::isfinite(value)||value<double(std::numeric_limits<ID>::min())||value>=double(std::numeric_limits<ID>::max()))
        throw std::invalid_argument("Unrepresentable native bot publication integer");
    return ID(value);
}
double clamp(double value,double low,double high)
{return std::max(low,std::min(high,value));}
int crew_mask(const std::set<std::string>& names)
{
    int mask=0;
    for(const auto& name:names) {
        const auto at=std::find(crew.begin(),crew.end(),name);
        if(at==crew.end())throw std::invalid_argument("Unknown native critical crew member");
        mask|=1<<int(at-crew.begin());
    }
    return mask;
}
std::array<double,18> motion_values(const stock_native_bot_contacts::RamMotion& m)
{
    std::array<double,18> values{};std::size_t n=0;
    for(const auto& p:{m.velocity,m.position,m.angles,m.player_position,m.player_angles,m.player_velocity})
        for(double v:p)values[n++]=v;
    return values;
}
std::vector<std::optional<double>> edge(const Input& in,double now)
{
    std::vector<std::optional<double>> out;
    // Ordered scalar subset from _PUBLICATION_EDGE_SCALAR_FIELDS. All absent
    // optional groups retain a distinct empty value, even when their zeroed
    // columns would have the same integer wire representation.
    const auto scalar=[&](int index,std::uint32_t group=0u) {
        out.push_back(group&&!(in.flags&group)?std::nullopt:std::optional<double>(in.scalars[index]));
    };
    const auto bit=[&](std::uint32_t flag){out.push_back(bool(in.flags&flag));};
    scalar(0);scalar(11);scalar(12);scalar(13,Ammo);bit(AmmoReloadPending);scalar(17);scalar(14,Clip);scalar(15,Clip);
    bit(BurstActive);for(int i:{18,19,20,22,21})scalar(i,Burst);
    scalar(24,Siege);scalar(26,Siege);scalar(27);bit(Alive);scalar(29);scalar(30);scalar(33);scalar(28);bit(WorldPose);bit(RouteWreckBlocked);
    out.push_back(in.flags&RamMotion?std::optional<double>(double(in.motions.size())):std::nullopt);
    if(in.flags&RamMotion)for(const auto& m:in.motions) {
        out.push_back(double(m.player));out.push_back(double(m.sequence));out.push_back(double(m.sample_us));
        for(double v:motion_values(m))out.push_back(v);
    }
    scalar(34);
    out.push_back(double(in.ammunition.size()));for(int v:in.ammunition)out.push_back(v);
    out.push_back(double(in.equipment.size()));
    for(const auto& e:in.equipment) {
        out.push_back(double(e.contract_revision));out.push_back(e.uses);out.push_back(e.active);out.push_back(e.ready_at);
        out.push_back(now>=e.ready_at);out.push_back(e.auto_pending_since);out.push_back(e.ai_pending_since);
    }
    scalar(35,ShotAngles);scalar(36,ShotAngles);
    return out;
}
}
ID fixed(double value,double scale)
{
    if(!std::isfinite(value))throw std::invalid_argument("Nonfinite native bot publication value");
    const double scaled=value*scale;
    return integral(scaled>=0.?std::floor(scaled+.5):-std::floor(-scaled+.5));
}
std::vector<ID> encode(const Input& in,double now)
{
    std::vector<ID> row;row.reserve(120);
    for(std::size_t index=0;index<scales.size();++index) {
        if(index==1){row.push_back(in.flags);continue;}
        double value=in.scalars[index];
        if(!std::isfinite(value))throw std::invalid_argument("Nonfinite native bot scalar");
        if((index==35||index==36)&&!(in.flags&ShotAngles)){row.push_back(0);continue;}
        if(index==6||index==7||index==35)value=wrapped(value);
        if(index==2||index==4)value=clamp(value,-2000.,2000.);
        else if(index==3)value=clamp(value,-1000.,1000.);
        else if(index==9||index==36)value=clamp(value,-1.2,1.2);
        else if(index==10)value=clamp(value,-80.,80.);
        row.push_back(scales[index]?fixed(value,scales[index]):integral(value));
    }
    if(in.flags&Ammo) {
        if(in.ammunition.size()>5)throw std::invalid_argument("Too many native shell types");
        row.push_back(ID(in.ammunition.size()));row.insert(row.end(),in.ammunition.begin(),in.ammunition.end());
    }
    if(in.flags&Critical) {
        std::vector<std::array<ID,4>> records;
        for(const auto& item:in.critical.devices) {
            const auto at=std::find(devices.begin(),devices.end(),item.first);
            if(at==devices.end())throw std::invalid_argument("Unknown native critical device");
            const auto& d=item.second;
            records.push_back({{ID(at-devices.begin()),fixed(d.hp.value_or(0.),1000.),fixed(d.maximum.value_or(1.),1000.),d.destroyed?2:d.critical?1:0}});
        }
        std::sort(records.begin(),records.end());row.push_back(ID(records.size()));
        for(const auto& record:records)row.insert(row.end(),record.begin(),record.end());
        row.push_back(crew_mask(in.critical.crew_ko));row.push_back(in.crew_roster_present?crew_mask(in.critical.crew_roster):-1);
    }
    if(in.flags&Equipment) {
        if(in.equipment.size()>3)throw std::invalid_argument("Too many native consumables");
        row.push_back(ID(in.equipment.size()));
        for(const auto& e:in.equipment) {
            row.push_back(e.uses);row.push_back(fixed(std::max(0.,e.ready_at-now),1000000.));row.push_back(e.active?1:0);
            for(const auto& start:{e.auto_pending_since,e.ai_pending_since})
                row.push_back(start?fixed(std::max(0.,now-*start),1000000.):-1);
        }
    }
    if(in.flags&ContactMomentum) {
        row.push_back(fixed(in.push[0],10000.));row.push_back(fixed(in.push[1],10000.));row.push_back(fixed(in.push[2],100000.));
        if(in.acknowledgements.size()>30)throw std::invalid_argument("Too many native contact acknowledgements");
        row.push_back(ID(in.acknowledgements.size()));
        for(const auto& item:in.acknowledgements) {
            const auto& c=item.second;
            if(item.first<1||item.first>2147483647||c.sequence<1||c.sequence>2147483647)
                throw std::invalid_argument("Invalid native contact acknowledgement identity");
            row.push_back(item.first);row.push_back(c.sequence);
            for(double v:c.totals) {
                if(!std::isfinite(v)||std::abs(v)>1.e12)throw std::invalid_argument("Invalid native contact acknowledgement total");
                row.push_back(fixed(v,10000.));
            }
        }
    }
    if(in.flags&RamMotion) {
        if(in.motions.size()>30)throw std::invalid_argument("Too many native ram motion rows");
        row.push_back(ID(in.motions.size()));std::set<ID> peers;
        for(const auto& m:in.motions) {
            if(m.player<1||m.player>2147483647||m.sequence<1||m.sequence>2147483647||
                m.sample_us<0||m.sample_us>9007199254740991LL||!peers.insert(m.player).second)
                throw std::invalid_argument("Invalid native ram motion identity");
            row.insert(row.end(),{m.player,m.sequence,m.sample_us});
            const auto values=motion_values(m);
            for(std::size_t i=0;i<values.size();++i) {
                const bool angle=(i>=6&&i<=8)||(i>=12&&i<=14);
                const double limit=i<3||i>=15?200.:angle?(i==6||i==12?2.*pi:pi):i==4||i==10?1000.:2000.;
                if(!std::isfinite(values[i])||std::abs(values[i])>limit)throw std::invalid_argument("Invalid native ram motion value");
                row.push_back(fixed(values[i],angle?100000.:10000.));
            }
        }
    }
    return row;
}
bool Signature::operator==(const Signature& other)const
{return actors==other.actors&&launches==other.launches&&rams==other.rams;}
Batch Publisher::publish(const std::vector<Input>& inputs,double now,ID sample,
    const std::deque<offline_simulation::weapons::LaunchRecord>& launches,
    std::vector<stock_native_bot_contacts::Report>& pending)
{
    Batch out;out.sample_us=sample;Signature signature;std::set<ID> failed;
    for(const auto& input:inputs) {
        const ID id=integral(input.scalars[0]);
        try{out.rows.push_back(encode(input,now));signature.actors.push_back(edge(input,now));}
        catch(const std::exception&){out.rows.push_back({id});out.failed.push_back(id);failed.insert(id);}
    }
    for(const auto& launch:launches)if(!failed.count(launch.actor.id)) {
        out.launches.push_back(launch);signature.launches.push_back({{launch.actor.id,launch.edge.shot_seq}});
    }
    std::vector<stock_native_bot_contacts::Report> deferred;
    for(const auto& report:pending) {
        if(failed.count(report.bot)||(!report.target_human&&failed.count(report.target))){deferred.push_back(report);continue;}
        out.reports.push_back(report);signature.rams.push_back({{report.bot,report.target_human?1:0,report.target,report.sequence}});
    }
    pending=std::move(deferred);
    if(!previous_||!(*previous_==signature)){++edge_revision_;edge_sample_us_=sample;previous_=std::move(signature);}
    out.edge_sample_us=edge_sample_us_;out.edge_revision=edge_revision_;return out;
}
}
