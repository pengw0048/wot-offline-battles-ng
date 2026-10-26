#pragma once
#include "stock_replay_file.hpp"
#include <functional>
#include <memory>

namespace wot {
std::string stockReplayAutoStartFile(const std::vector<std::string>& arguments);
struct StockReplayClock { double time=0,serverOffset=0;bool attached=false; };
struct StockReplayAction {
    std::uint32_t type=0,id=0,space=0,vehicle=0;
    std::uint16_t entityType=0;
    float position[3]={},direction[3]={};
    bool control=false;
    std::vector<std::uint8_t> data;
};
// The production implementation owns a real ConnectionControl connection.
// Tests provide a contract observer, never a replacement game connection.
class StockReplayConnection {
public:
    virtual ~StockReplayConnection()=default;
    virtual bool validEntityID(std::uint32_t id) const=0;
    virtual bool validEntityType(std::uint16_t type) const=0;
    virtual bool begin(const std::shared_ptr<StockReplayClock>& clock,std::string& error)=0;
    virtual bool deliver(const StockReplayAction& action,std::string& error)=0;
    virtual void end()=0;
};
class StockReplayController {
public:
    explicit StockReplayController(std::unique_ptr<StockReplayConnection> connection);
    ~StockReplayController();
    bool startPlayback(const std::string& path,std::string& error);
    void stop();
    bool tick(float delta,std::string& error);
    void onClientReady() {ready_=true;}
    bool confirmVersion(bool accepted);
    bool setSpeed(float speed,std::string& error);
    float timeMark(std::uint32_t id) const;
    bool playing() const {return playing_;}
    bool ready() const {return ready_;}
    bool finished() const {return finished_;}
    bool awaitingVersion() const {return versionPending_;}
    float speed() const {return speed_;}
    float duration() const {return file_.stream().duration();}
    bool compressed() const {return file_.stream().format()!=ReplayContainer::PacketStream;}
    std::string arenaInfo() const;
    const std::string& error() const {return error_;}
    std::string clientVersion;
    std::string controlMode;
    int playerVehicleID=0;
    bool controllingCamera=true;
    std::function<void()> onFinished,onVersionDiffers;
private:
    bool preflight(const StockReplayStream& stream,std::string& error) const;
    std::unique_ptr<StockReplayConnection> connection_;
    std::shared_ptr<StockReplayClock> clock_;
    StockReplayFile file_;
    std::string error_;
    std::size_t next_=0;
    std::uint64_t identity_=0;
    float speed_=1;
    bool playing_=false,ready_=false,finished_=false,versionPending_=false;
};
} // namespace wot

#if !defined(WOT_REPLAY_CONTROLLER_HOST_BUILD)
#include "cstdmf/bw_namespace.hpp"
BW_BEGIN_NAMESPACE
class BWEntityFactory;
class BWSpaceDataStorage;
class EntityDefConstants;
class BWNullConnection;
BWNullConnection* createStockReplayNativeConnection(BWEntityFactory& factory,
    BWSpaceDataStorage& storage,const EntityDefConstants& definitions,double initialTime,
    unsigned int space,const std::shared_ptr<wot::StockReplayClock>& clock);
bool initialiseStockReplayController();
void tickStockReplayController(float delta);
void finiStockReplayController();
BW_END_NAMESPACE
#endif
