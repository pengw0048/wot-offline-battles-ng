// Real room/network threads, with no engine or external TCP client.
#include "room_host.hpp"
#include "room_json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#if defined(_WIN32)
#include <winsock2.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace wot::room;
namespace {
using Clock=Channel::Clock;
void require(bool value,const char* why) {if(!value)throw std::runtime_error(why);}
unsigned unusedPort() {
#if defined(_WIN32)
    WSADATA data{};require(WSAStartup(MAKEWORD(2,2),&data)==0,"Test Winsock startup failed");
#endif
    const auto socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    require(::bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Cannot allocate test port");
#if defined(_WIN32)
    int length=sizeof(address);
#else
    socklen_t length=sizeof(address);
#endif
    require(getsockname(socket,reinterpret_cast<sockaddr*>(&address),&length)==0,"Cannot inspect test port");
    const auto port=ntohs(address.sin_port);
#if defined(_WIN32)
    closesocket(socket);WSACleanup();
#else
    ::close(socket);
#endif
    return port;
}
double processCpu() {
#if defined(_WIN32)
    FILETIME created{},exited{},kernel{},user{};require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=0,"Cannot read process CPU");
    const auto ticks=[](FILETIME v){return (std::uint64_t(v.dwHighDateTime)<<32)|v.dwLowDateTime;};return double(ticks(kernel)+ticks(user))/1e7;
#else
    rusage usage{};require(getrusage(RUSAGE_SELF,&usage)==0,"Cannot read process CPU");return double(usage.ru_utime.tv_sec+usage.ru_stime.tv_sec)+double(usage.ru_utime.tv_usec+usage.ru_stime.tv_usec)/1e6;
#endif
}
template<class Predicate>void until(Predicate predicate,const char* why) {
    const auto deadline=Clock::now()+std::chrono::seconds(5);
    while(!predicate()){require(Clock::now()<deadline,why);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
Codec noExternalCodec() {
    Codec codec;codec.frameSize=[](const unsigned char*,std::size_t){return std::size_t(8);};
    codec.decode=[](const unsigned char*,std::size_t,Value&,std::string&){return false;};
    codec.encode=[](const Value&,std::string&,std::string&){return false;};return codec;
}
}
int main(int argc,char**argv)try {
    require(argc==3,"Expected resource and temporary folder paths");const std::filesystem::path folder=std::filesystem::u8path(argv[2]);std::filesystem::create_directories(folder);
    std::ifstream input(argv[1]);const auto resources=parseJson(std::string((std::istreambuf_iterator<char>(input)),{}));
    auto config=object();config.object["schema"]=integer(1);config.object["resources_path"]=text(argv[1]);config.object["receipts_path"]=text((folder/"receipts.json").u8string());config.object["bind_address"]=text("127.0.0.1");config.object["port"]=integer(unusedPort());
    const auto path=folder/"host.json";{std::ofstream stream(path);stream<<writeJson(config);}
    Host host(path.u8string(),noExternalCodec(),[](const Value&)->std::optional<Value>{return {};});
    auto hello=object();hello.object["type"]=text("hello");hello.object["role"]=text("simulation_worker");hello.object["protocol"]=resources.get("protocol");hello.object["client_build"]=resources.get("client_build");hello.object["capabilities"]=resources.get("required_capabilities");
    require(host.send(freeze(std::move(hello)))==Channel::Admission::admitted,"Worker hello was refused");
    until([&]{return host.status().ready;},"Worker hello was not consumed");
    std::set<std::int64_t> admitted;std::int64_t sequence=0;std::size_t refused=0;
    const auto fill_deadline=Clock::now()+std::chrono::seconds(5);
    while(true) {
        for(unsigned i=0;i<64;++i) {
            auto ping=object();ping.object["type"]=text("ping");ping.object["seq"]=integer(++sequence);ping.object["client_time"]=number(double(sequence));
            if(host.send(freeze(std::move(ping)))==Channel::Admission::admitted)admitted.insert(sequence);else ++refused;
        }
        const auto queues=host.metrics();if(queues.first.pending==512&&queues.second.pending==512&&refused)break;
        require(Clock::now()<fill_deadline,"Host queues did not reach bounded backpressure");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto before=host.metrics();const auto started=Clock::now();const double cpu=processCpu();
    // This fixed idle window measures spinning; readiness/consumption above
    // and below are proved by state predicates, never by this delay.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const double cpu_used=processCpu()-cpu,wall=std::chrono::duration<double>(Clock::now()-started).count();const auto blocked=host.metrics();
    require(blocked.first.pending==512&&blocked.second.pending==512,"Blocked host admitted beyond channel bounds");
    require(blocked.first.admitted==before.first.admitted&&blocked.second.admitted==before.second.admitted,"Blocked host continued admitting work");
    require(blocked.first.peak_pending<=512&&blocked.second.peak_pending<=512,"Host queue exceeded its capacity");
    require(cpu_used<wall*.5,"Backpressured host busy-spun instead of waiting");
    std::set<std::int64_t> replies;const auto collect=[&](const std::vector<Channel::Entry>& entries){
        for(const auto& entry:entries)if(entry.message->get("type").text=="pong") {
            const auto id=intField(*entry.message,"seq");require(admitted.count(id)!=0,"Reply for a refused ping");require(replies.insert(id).second,"Duplicate ping reply");
        }
    };
    until([&]{collect(host.receive(64));return replies==admitted;},"An admitted ping lost its reply after backpressure recovery");
    host.stop();require(host.status().stopped&&host.status().error.empty(),"Host did not stop cleanly");
    collect(host.receive(512));require(host.metrics().second.pending==0,"Stopped host retained unread deliveries");
    std::cout<<"Real Host bounded backpressure passed: admitted="<<admitted.size()<<" refused="<<refused<<" replies="<<replies.size()<<" idle_cpu_fraction="<<cpu_used/wall<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
