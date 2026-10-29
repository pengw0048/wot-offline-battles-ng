#pragma once
#include "stock_native_client_drive_publication.hpp"
namespace stock_native_client_drive {
// Typed mailbox admission; implementations perform no physical computation.
class PlayerPublicationSink {
public:
    virtual ~PlayerPublicationSink()=default;
    virtual bool barriers()=0;
    virtual bool current(std::optional<bool>siege={})=0;
    virtual bool landing()=0;
    virtual std::optional<ID>inputSequence()const=0;
};
bool publishPlayer(PlayerDriver&,PublicationLedger&,const Publication&,PlayerPublicationSink&);
}
