#include "stock_native_bot_step_state.hpp"
#include <cmath>
#include <iomanip>
#include <iostream>
#include <string>
using namespace wot::offline_bot_step;
struct World:VerticalWorld {
    int mode=0;double gx=0.,gz=0.;
    std::optional<double> initial_ground(double,double,double)override{return 0.;}
    std::optional<double> basic_ground(double x,double z,double hint,std::optional<double> ceiling)override {
        const double height=gx*x+gz*z+(mode==1&&x>1.?3.:mode==4&&x>1.?.8:0.);
        if((mode==2&&z>1.)||(mode==3&&x< -1.)||(ceiling&&height>*ceiling))return {};
        // The source's native column starts two metres above its hint.
        return height<=hint+2.?std::optional<double>{height}:std::nullopt;
    }
    std::optional<double> ground(double,double,double,double,std::optional<double>)override{return {};}
    bool turret_clear(const VerticalState&,const VerticalState&)override{return true;}
    std::array<double,2>passive_move(VerticalState&,double,double,double)override{return {};}
};
int main(int argc,char**argv){std::cout<<std::setprecision(17);World world;VerticalState state;SuspensionDescriptor d;d.half_length=3.;d.half_width=1.5;d.springs.emplace_back();
    if(argc>1&&std::string(argv[1])=="lifecycle") {
        state.position={0.,.3,0.};state.grounded_once=true;state.edge_support_active=true;state.speed=2.;state.vertical_speed=-1.;state.pitch_speed=.2;state.roll_speed=.1;
        VerticalMemory memory;memory.springs.emplace_back(GroundMemory{});
        const auto emit=[&]{std::cout<<state.position[1]<<' '<<state.edge_support_active<<' '<<(state.basic_support_datum?*state.basic_support_datum:99999.)<<' '<<state.pitch_speed<<' '<<state.roll_speed<<'\n';};
        std::cout<<release_detailed_support(state,memory,d,world)<<' '<<memory.springs.size()<<' ';emit();
        int budget=1;advance_basic_vertical(state,d,world,state.position,0.,budget);emit();
        world.mode=4;advance_basic_vertical(state,d,world,state.position,.1,budget);emit();
        world.mode=0;advance_basic_vertical(state,d,world,state.position,.1,budget);emit();
        world.mode=2;state.basic_support_datum=.3;std::cout<<select_detailed_support(state,d,world)<<' ';emit();
        state.airborne=true;std::cout<<release_detailed_support(state,memory,d,world)<<' ';emit();return 0;
    }
    while(std::cin>>world.mode>>world.gx>>world.gz>>state.position[0]>>state.position[1]>>state.position[2]>>state.yaw>>state.terrain_pitch>>state.roll) {
        state.grounded_once=true;state.airborne=false;state.edge_support_active=false;state.basic_support_datum.reset();
        const auto rows=basic_support_status(state,d,world);
        for(const auto& row:rows)std::cout<<row.position[0]<<' '<<row.position[1]<<' '<<row.position[2]<<' '<<row.valid<<' '<<row.continuous_rise<<' '<<(row.ground?*row.ground:99999.)<<' ';
        const bool selected=select_detailed_support(state,d,world);VerticalMemory memory;
        const bool released=release_detailed_support(state,memory,d,world);
        std::cout<<selected<<' '<<released<<'\n';
    }
}
