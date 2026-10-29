#include "stock_native_client_drive_dispatch.hpp"
namespace stock_native_client_drive {
bool publishPlayer(PlayerDriver&driver,PublicationLedger&ledger,const Publication&publication,PlayerPublicationSink&sink){
    const bool barriers=!ledger.barriers().empty();
    // An unadmitted physical barrier fences every later pose/one-shot. The
    // sink retains that barrier and its FIFO suffix when transport is full.
    if(!sink.barriers())return false;
    if(barriers)driver.input_sent();
    bool siege_sent=false;
    if(publication.siege_request&&sink.current(publication.siege_request)){
        driver.siege_admitted(*publication.siege_request,sink.inputSequence());
        siege_sent=true;
    }
    if(publication.landing)return sink.landing();
    if(publication.input&&!barriers&&!siege_sent)return sink.current();
    return true;
}
}
