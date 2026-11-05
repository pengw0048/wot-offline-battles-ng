#pragma once
#include "room_strategy_data.hpp"
#include <deque>
#include <cctype>

namespace wot::room::strategy {
class ShotPolicy {
    struct Result {bool hit=false,penetrated=false,known=true;double damage=0.;};
    struct Shot {
        ID sequence=0,group=0,epoch=0,phase_id=0,magazine=0;
        int shell=0,count=0;
        std::optional<Result> result;
    };
    Value profile_;
    std::vector<Value> shells_;
    Value inventory_,anchor_,target_;
    ID epoch_=0,magazine_=0,phase_id_=0;
    int phase_=0,desired_=0,used_=0,phase_fired_=0;
    std::map<std::pair<ID,ID>,int> failures_;
    std::deque<std::shared_ptr<Shot>> pending_;
    std::map<ID,std::shared_ptr<Shot>> by_sequence_;
    std::map<ID,std::vector<std::shared_ptr<Shot>>> groups_;
    std::map<ID,int> group_sizes_,group_counts_;
    std::optional<ID> last_group_;
    int clipSize() const {return std::max(1,int(intField(profile_,"clip_size",1)));}
    void closeGroup(ID key,int count) {
        if(count<=0)return;group_sizes_[key]=count;const auto group=groups_.find(key);
        if(group!=groups_.end()&&!group->second.empty()&&int(group->second.size())==count)result(group->second.back(),true);
    }
    void result(std::shared_ptr<Shot> shot,bool closed=false) {
        auto outcome=*shot->result;
        if(shot->count>1) {
            auto& group=groups_[shot->group];if(!closed)group.push_back(shot);const auto size=group_sizes_.find(shot->group);if(int(group.size())<(size==group_sizes_.end()?shot->count:size->second))return;
            bool epoch_matches=true;outcome={false,false,true,0.};
            for(const auto& r:group){outcome.hit=outcome.hit||r->result->hit;outcome.damage+=r->result->damage;outcome.penetrated=outcome.penetrated||r->result->penetrated;outcome.known=outcome.known&&r->result->known;epoch_matches=epoch_matches&&r->epoch==shot->epoch;}
            groups_.erase(shot->group);if(!epoch_matches)return;
        }
        if(!outcome.known){failures_.clear();return;}
        if(shot->epoch!=epoch_||shot->shell!=phase_||shot->phase_id!=phase_id_||desired_!=phase_||shells_.empty())return;
        auto selected=std::find_if(shells_.begin(),shells_.end(),[&](const Value& s){return intField(s,"index")==shot->shell;});
        const bool artillery=s(profile_,"class_tag")=="SPG";const auto active=orderedShells(profile_,inventory_,true);auto he=[](const Value& shell){auto kind=s(shell,"kind");for(char& c:kind)c=char(std::toupper(static_cast<unsigned char>(c)));return kind=="HIGH_EXPLOSIVE";};
        const bool first_he=!active.empty()&&he(active.front())&&!artillery;
        if(first_he&&selected!=shells_.end()&&!he(*selected))return;
        bool failure=false,success=false;
        if(artillery){failure=!outcome.hit&&outcome.damage<=0.;success=outcome.hit||outcome.damage>0.;}
        else if(first_he&&selected!=shells_.end()&&he(*selected)){failure=outcome.hit&&!outcome.penetrated;success=outcome.hit&&outcome.penetrated;}
        else{failure=outcome.hit&&outcome.damage<=0.;success=outcome.damage>0.;}
        const auto key=std::make_pair(shot->epoch,clipSize()>1?shot->magazine:0);if(success)failures_[key]=0;else if(failure)++failures_[key];
        const auto failures=failures_.find(key);if(failures!=failures_.end()&&failures->second>=(shot->count>1?1:2)) {
            auto existing=std::find_if(active.begin(),active.end(),[&](const Value& s){return intField(s,"index")==shot->shell;});if(existing==active.end()||active.size()<2)return;
            const auto offset=std::size_t(existing-active.begin());desired_=int(intField(active[(offset+1)%active.size()],"index"));failures_.clear();
        }
    }
public:
    explicit ShotPolicy(Value profile):profile_(std::move(profile)),shells_(orderedShells(profile_,Value(),true)) {
        phase_=shells_.empty()?0:int(intField(shells_.front(),"index"));desired_=phase_;
    }
    void context(const Value& position,const Value& target) {
        const double dx=n(position,"x")-n(anchor_,"x"),dz=n(position,"z")-n(anchor_,"z");const bool moved=!null(anchor_)&&dx*dx+dz*dz>9.;
        if(null(anchor_)||moved)anchor_=record({{"x",number(n(position,"x"))},{"z",number(n(position,"z"))}});
        if(moved||(!null(target)&&target!=target_)){++epoch_;failures_.clear();}if(!null(target))target_=target;
    }
    int choose(const Value& state) {
        if(has(state,"burst_active")&&state.get("burst_active").resolved().kind==Value::Kind::Boolean&&!b(state,"burst_active")&&b(state,"burst_group_seq"))closeGroup(intField(state,"burst_group_seq"),int(intField(state,"burst_next_index")));
        inventory_=state.get("ammo_remaining");const auto available=orderedShells(profile_,inventory_,true);if(available.empty())return phase_;
        auto hasIndex=[&](int index){return std::any_of(available.begin(),available.end(),[&](const Value& s){return intField(s,"index")==index;});};
        if(!hasIndex(desired_))desired_=int(intField(available.front(),"index"));const int main=int(intField(available.front(),"index"));if(desired_!=phase_)return desired_;if(!hasIndex(phase_))return desired_;
        const auto actual=std::find_if(available.begin(),available.end(),[&](const Value& s){return intField(s,"index")==phase_;});
        auto he=[](const Value& shell){auto kind=s(shell,"kind");for(char& c:kind)c=char(std::toupper(static_cast<unsigned char>(c)));return kind=="HIGH_EXPLOSIVE";};
        const bool first_he=he(available.front()),artillery=s(profile_,"class_tag")=="SPG";
        if(phase_!=main&&(artillery||first_he||he(*actual)||clipSize()>1)) {
            const bool returning=clipSize()>1?used_>=clipSize():phase_fired_>=(first_he&&!artillery?1:2);if(returning)return main;
        }
        return desired_;
    }
    void launch(ID sequence,int shell,ID group,int count,const Value& position,const Value& target) {
        if(by_sequence_.count(sequence))return;context(position,target);if(last_group_&&group!=*last_group_)closeGroup(*last_group_,group_counts_[*last_group_]);last_group_=group;++group_counts_[group];
        if(shell!=phase_) {const int old=phase_;phase_=shell;++phase_id_;failures_.clear();if(desired_==old||desired_==shell)desired_=shell;phase_fired_=0;used_=0;}
        if(used_==0||used_>=clipSize()){++magazine_;used_=0;}++used_;++phase_fired_;
        auto shot=std::make_shared<Shot>();shot->sequence=sequence;shot->shell=shell;shot->group=group;shot->count=count;shot->epoch=epoch_;shot->phase_id=phase_id_;shot->magazine=magazine_;pending_.push_back(shot);by_sequence_[sequence]=std::move(shot);
    }
    void resolve(ID sequence,bool hit,double damage,bool penetrated,bool known=true) {
        const auto found=by_sequence_.find(sequence);if(found==by_sequence_.end()||found->second->result)return;found->second->result=Result{hit,penetrated,known,damage};
        while(!pending_.empty()&&pending_.front()->result){const auto current=pending_.front();pending_.pop_front();by_sequence_.erase(current->sequence);result(current);}
    }
    Value snapshot() const {
        auto out=record({{"phase",integer(phase_)},{"desired",integer(desired_)},{"epoch",integer(epoch_)},{"magazine",integer(magazine_)},{"used",integer(used_)},{"phase_fired",integer(phase_fired_)},{"phase_id",integer(phase_id_)},{"pending",integer(static_cast<ID>(pending_.size()))}});return out;
    }
};
}
