#include "stock_native_bot_contacts.hpp"
#include <cassert>
#include <iomanip>
#include <iostream>
#include <string>
namespace m=stock_native_bot_motion;namespace c=stock_native_bot_contacts;
int main(int argc,char**argv){std::cout<<std::setprecision(17);
    if(argc>1&&std::string(argv[1])=="producer") {
        m::Descriptor descriptor;descriptor.drive.mass=10000.;descriptor.collision_shape={1.,2.,0.,2.};
        m::State own;own.id=1;own.position={0.,0.,5.};own.speed=5.;
        m::State peer;peer.id=2;peer.position={0.,0.,6.};
        c::Actor a;a.motion=&own;a.descriptor=&descriptor;c::Actor b;b.motion=&peer;b.descriptor=&descriptor;
        std::map<c::ID,c::Actor>actors{{1,a}};c::Human human;human.network_id=3;human.body.id=3;human.body.x=0.;human.body.z=6.;human.body.shape=descriptor.collision_shape;human.body.flags=c::Body{}.flags|offline_contact::HasY;
        c::Contacts contacts(1000000);contacts.translations(actors,{human},{{1,{0.,0.,0.}}},1000000,{1},1.);
        assert(own.translation_receipt&&own.translation_receipt->remote_player_contact);m::settle_drive_sweep(own);
        assert(own.translation_receipt->outcome==m::TranslationOutcome::RemotePlayer&&own.speed==5.);
        assert(own.translation_receipt->fraction<1.&&own.translation_receipt->elapsed==1.);
        own.position={0.,0.,5.};actors[2]=b;contacts.translations(actors,{},{{1,{0.,0.,0.}}},2000000,{1,2},1.);
        assert(own.translation_receipt&&!own.translation_receipt->remote_player_contact);m::settle_drive_sweep(own);
        assert(own.translation_receipt->outcome==m::TranslationOutcome::MotorSettled&&own.speed<5.);
        own.alive=false;contacts.translations(actors,{},{{1,{0.,0.,0.}}},3000000,{1,2},1.);assert(!own.translation_receipt);
        std::cout<<"ok\n";return 0;
    }
    m::State state;double sx,sz,rx,rz,fraction,dt,before;bool remote;
    while(std::cin>>state.position[0]>>state.position[2]>>state.yaw>>state.speed>>state.contact_forward>>sx>>sz>>rx>>rz>>fraction>>dt>>before>>remote){
        m::TranslationReceipt receipt;receipt.start={sx,0.,sz};receipt.requested={rx,rz};receipt.fraction=fraction;receipt.elapsed=dt;receipt.speed_before=before;receipt.remote_player_contact=remote;state.translation_receipt=receipt;
        m::settle_drive_sweep(state);std::cout<<state.speed<<' '<<int(state.translation_receipt->outcome)<<'\n';
    }
}
