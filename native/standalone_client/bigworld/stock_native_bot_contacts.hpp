#pragma once

#include "offline_contact_roster.h"
#include "offline_math_batch.h"
#include "stock_native_bot_motion.hpp"
#include <set>

namespace stock_native_bot_contacts {
using ID=std::int64_t;
using Pair=std::pair<ID,ID>;
using Point=stock_native_bot_motion::Point;
using Motion=stock_native_bot_motion::State;
using Descriptor=stock_native_bot_motion::Descriptor;
using Body=offline_contact::Body;
struct Profile {
    double spall=1.,bonus=0.;
    std::optional<double> contact_armor;
};
struct Checkpoint {
    ID actor=0,sequence=0;
    std::array<double,5> totals{};
};
struct RamMotion {
    ID player=0,sequence=0,sample_us=0;
    Point velocity{},position{},angles{},player_position{},player_angles{},player_velocity{};
};
struct Actor {
    Motion* motion=nullptr;
    const Descriptor* descriptor=nullptr;
    int team=0;
    Profile profile;
    std::optional<double> ram_vertical_speed;
    std::map<ID,Checkpoint> push_acks;
    std::map<ID,RamMotion> ram_motion;
    ID ram_motion_sequence=0;
};
struct Receipt {
    ID sequence=0,bot=0;
    bool valid=false,has_history=false,player_screened=false,bot_screened=false;
    Body history;
    Profile history_profile;
    Point player_position{},player_angles{},player_velocity{},bot_velocity{},hit{};
    double armor_player=0.,armor_bot=0.,spall_player=0.,bonus_player=-1.;
    std::array<double,2> player_normal{};
    std::vector<std::string> bot_tracks,player_tracks;
};
struct Human {
    ID network_id=0,acknowledged_ram_sequence=0;
    Body body;
    Profile profile;
    bool checkpoint_field_present=false,checkpoints_valid=true;
    std::vector<Checkpoint> checkpoints;
    std::vector<Receipt> receipts;
};
struct Armor {
    std::array<double,2> value{};
    std::array<std::vector<std::string>,2> tracks;
};
struct Report {
    ID bot=0,target=0,sequence=0,player_receipt=0;
    bool target_human=false;
    std::int64_t damage_bot=0,damage_target=0;
    double closing_speed=0.;
    std::array<double,4> positions{};
    std::vector<std::string> bot_tracks,target_tracks;
};
struct PushProof {
    ID sequence=0,attacker=0,target=0;
    bool attacker_human=false,target_human=false;
};
class World {
public:
    virtual ~World()=default;
    virtual stock_native_bot_motion::World& motion_world(ID)=0;
    // Native hit-tester proof at the frozen body poses. No substituted armour.
    virtual std::optional<Armor> armor(const Body&,const Body&,const std::array<double,3>&)=0;
};
std::optional<std::array<double,3>> overlap(const Body&,const Body&);
bool vertical_overlap(const Body&,const Body&);
bool contains(const Body&,const Point&,double margin);
std::array<std::int64_t,2> damage(double closing,double own_mass,double other_mass,
    double own_armor,double other_armor,const Profile&,const Profile&,bool moving_own,bool moving_other);
Body project(const Actor&,double dt);
offline_math::Body geometry(const Body&);
bool wreck_active(const Actor&);
std::vector<ID> python27_integer_order(const std::vector<ID>& insertions);
class Contacts {
    ID human_base_;
    std::set<Pair> contacts_;
    std::map<Pair,double> cooldowns_;
    std::map<ID,ID> acknowledged_;
    std::map<Pair,std::vector<Report>> cached_reports_;
    std::map<ID,double> wait_elapsed_;
    std::map<std::pair<bool,ID>,PushProof> push_proofs_;
    ID ram_sequence_=0,push_sequence_=0;
    std::vector<Report> historical(std::map<ID,Actor>&,const std::vector<Human>&);
    Report terminal(ID bot,ID player,ID sequence);
public:
    explicit Contacts(ID human_id_base):human_base_(human_id_base){}
    void acknowledge(ID player,ID sequence);
    void consume(std::map<ID,Actor>&,const std::vector<Human>&,World&,double now);
    void translations(std::map<ID,Actor>&,const std::vector<Human>&,
        const std::map<ID,Point>& starts,ID sample_us,const std::vector<ID>& owner_order={});
    // dictionary_order is the fixed Python integer-key body traversal order.
    std::vector<Report> settle(std::map<ID,Actor>&,const std::vector<Human>&,
        const std::vector<ID>& dictionary_order,double dt,double now,World&,const std::vector<ID>& owner_order={});
    std::map<ID,double> take_waits();
    const std::map<std::pair<bool,ID>,PushProof>& push_proofs()const{return push_proofs_;}
    const std::set<Pair>& active_contacts()const{return contacts_;}
};
}
