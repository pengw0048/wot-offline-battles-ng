#include "stock_replay_controller.hpp"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using Bytes=std::vector<std::uint8_t>;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void word(Bytes& bytes,std::uint32_t value){for(unsigned shift=0;shift<32;shift+=8)bytes.push_back(static_cast<std::uint8_t>(value>>shift));}
void small(Bytes& bytes,std::uint16_t value){bytes.push_back(static_cast<std::uint8_t>(value));bytes.push_back(static_cast<std::uint8_t>(value>>8));}
void real(Bytes& bytes,float value){std::uint32_t bits=0;std::memcpy(&bits,&value,4);word(bytes,bits);}
void string(Bytes& bytes,const std::string& value){word(bytes,static_cast<std::uint32_t>(value.size()));bytes.insert(bytes.end(),value.begin(),value.end());}
struct Observer : wot::StockReplayConnection {
    unsigned begins=0,ends=0;std::shared_ptr<wot::StockReplayClock> clock;
    std::vector<wot::StockReplayAction> messages;std::function<void()> duringDelivery,duringEnd;
    bool validEntityID(std::uint32_t id) const override{return id>0&&id<0x7f000000;}
    bool validEntityType(std::uint16_t type) const override{return type==7;}
    bool begin(const std::shared_ptr<wot::StockReplayClock>& value,std::string&) override{++begins;clock=value;clock->attached=true;return true;}
    bool deliver(const wot::StockReplayAction& action,std::string&) override{messages.push_back(action);if(duringDelivery)duringDelivery();return true;}
    void end() override{if(clock&&clock->attached){++ends;clock->attached=false;if(duringEnd)duringEnd();}}
};
Bytes base(const std::string& name=""){Bytes bytes;word(bytes,123);small(bytes,7);string(bytes,name);word(bytes,3);bytes.insert(bytes.end(),{1,2,3});return bytes;}
Bytes cell(std::uint16_t type=7){Bytes bytes;word(bytes,123);word(bytes,55);small(bytes,type);word(bytes,0);for(float value:{1.f,2.f,3.f,.1f,.2f,.3f})real(bytes,value);word(bytes,2);bytes.insert(bytes.end(),{4,5});return bytes;}
Bytes anchor(){Bytes bytes;const double value=100;std::uint64_t bits=0;std::memcpy(&bits,&value,8);word(bytes,static_cast<std::uint32_t>(bits));word(bytes,static_cast<std::uint32_t>(bits>>32));return bytes;}
void append(wot::StockReplayFile& file,std::uint32_t type,float time,const Bytes& bytes){std::string error;require(file.stream().append(type,time,bytes,error),error.c_str());}
void save(wot::StockReplayFile& file,const std::string& path){std::string error;require(file.save(path,wot::ReplayContainer::EncryptedZlib,error),error.c_str());}
}
int main(int argc,char** argv){
    try{
        require(argc==2,"Usage: test_stock_replay_controller <private-fixture-directory>");const std::string root=argv[1];std::string error;
        require(wot::stockReplayAutoStartFile({"client.exe","-unattended"}).empty(),"Replay auto-start invented a command-line file");
        require(wot::stockReplayAutoStartFile({"client.exe","a.WOTREPLAY","\"first.wotreplay\"","last.wotreplaylist"})=="last.wotreplaylist","Replay auto-start lost original case-sensitive substring/last-argument semantics");
        require(wot::stockReplayAutoStartFile(std::vector<std::string>(32,"file.wotreplay")).empty(),"Replay auto-start ignored the original 32-argument guard");
        wot::StockReplayFile fixture;fixture.stream().metadata({"{\"fixture\":true}"});Bytes version;string(version,"0.9.22.0 #1513");
        append(fixture,0x18,0,version);append(fixture,0,0,base());append(fixture,1,0,cell());append(fixture,0x15,0,anchor());
        Bytes control;word(control,123);control.push_back(1);append(fixture,2,.2f,control);save(fixture,root+"/controller-current.wotreplay");
        auto* observer=new Observer;wot::StockReplayController controller{std::unique_ptr<wot::StockReplayConnection>(observer)};controller.clientVersion="0.9.22.0 #1513";
        unsigned finished=0;controller.onFinished=[&](){++finished;};
        require(controller.startPlayback(root+"/controller-current.wotreplay",error),error.c_str());
        require(controller.playing()&&!controller.ready()&&!controller.finished()&&controller.arenaInfo()=="{\"fixture\":true}","Replay startup did not install the active file owner");
        require(controller.tick(10,error)&&observer->messages.size()==2&&controller.timeMark(0x80000002)==0,"Loading prefix advanced time or crossed the ready barrier");
        require(observer->messages[0].id==123&&observer->messages[0].entityType==7&&observer->messages[0].data==Bytes({1,2,3}),"Base-player wire fields changed");
        const auto& cellMessage=observer->messages[1];require(cellMessage.space==55&&cellMessage.vehicle==0&&cellMessage.position[2]==3&&cellMessage.direction[1]==.2f&&cellMessage.data==Bytes({4,5}),"Cell-player wire fields changed");
        controller.onClientReady();require(controller.tick(0,error)&&controller.timeMark(0x80000003)==100,"Time anchor was not consumed at the ready transition");
        require(controller.setSpeed(0,error)&&controller.tick(1,error)&&observer->messages.size()==2,"Paused replay consumed a future packet");
        require(controller.setSpeed(2,error)&&controller.tick(.11f,error)&&observer->messages.size()==3&&finished==1,"Due native control message or EOF callback did not arrive");
        require(controller.playing()&&controller.finished()&&controller.tick(10,error)&&finished==1,"Replay completion callback repeated or destroyed its owner");
        require(controller.timeMark(0x80000001)==.2f&&controller.timeMark(17)==-1,"Replay time-mark sentinel contract changed");
        controller.stop();controller.stop();require(!controller.playing()&&observer->ends==1,"Repeated stop duplicated native connection teardown");
        const unsigned starts=observer->begins;
        for(unsigned variant=0;variant<5;++variant){
            wot::StockReplayFile rejected;
            if(variant==0)append(rejected,99,0,{});
            if(variant==1)append(rejected,0,0,base("unrecovered-type-name"));
            if(variant==2){append(rejected,0,0,base());append(rejected,1,0,cell(8));}
            if(variant==3)append(rejected,2,0,control);
            if(variant==4){Bytes malformed=base();malformed.pop_back();append(rejected,0,0,malformed);}
            const auto path=root+"/controller-rejected-"+std::to_string(variant);save(rejected,path);
            require(!controller.startPlayback(path,error)&&!error.empty()&&observer->begins==starts&&!controller.playing(),"Unsupported replay mutated the native connection before preflight completed");
        }
        unsigned differs=0;controller.clientVersion="different";controller.onVersionDiffers=[&](){require(controller.playing()&&controller.awaitingVersion(),"Version callback observed uninstalled state");++differs;};
        require(controller.startPlayback(root+"/controller-current.wotreplay",error)&&differs==1,"Replay version mismatch callback did not arrive");
        const auto delivered=observer->messages.size();require(controller.tick(1,error)&&observer->messages.size()==delivered,"Replay delivered messages before version acceptance");
        require(controller.confirmVersion(false)&&!controller.playing(),"Rejected version did not terminate the native owner");
        controller.clientVersion="0.9.22.0 #1513";
        require(controller.startPlayback(root+"/controller-current.wotreplay",error),error.c_str());
        observer->duringDelivery=[&](){controller.stop();};
        require(controller.tick(1,error)&&!controller.playing()&&observer->messages.size()==delivered+1,"Reentrant teardown continued the retired packet batch");
        observer->duringDelivery={};require(controller.startPlayback(root+"/controller-current.wotreplay",error),error.c_str());observer->clock->attached=false;
        require(!controller.tick(0,error)&&!controller.playing()&&!error.empty(),"Lost native connection remained a successful playback");
        require(controller.startPlayback(root+"/controller-current.wotreplay",error),error.c_str());
        observer->duringEnd=[&](){require(controller.startPlayback(root+"/controller-current.wotreplay",error),error.c_str());};
        controller.stop();observer->duringEnd={};
        require(controller.playing()&&observer->clock->attached&&controller.arenaInfo()=="{\"fixture\":true}",
                "Retiring connection teardown destroyed the replay started by its callback");
        const auto beforeRestart=observer->messages.size();
        require(controller.tick(0,error)&&observer->messages.size()==beforeRestart+2,
                "Reentrant replay restart lost its file or connection clock");
        controller.stop();
        require(!controller.setSpeed(std::numeric_limits<float>::quiet_NaN(),error),"Invalid playback speed was accepted");
        std::cout<<"Replay controller core contract passed: strict preflight, wire fields, ready/version barriers, real file clock and reentrant teardown; entity observer is not native acceptance"<<std::endl;
        return 0;
    }catch(const std::exception& exception){std::cerr<<"Replay controller contract failed: "<<exception.what()<<std::endl;return 1;}
}
