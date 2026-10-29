#pragma once
#include "stock_native_client_drive_contacts.hpp"
struct _object;
namespace stock_native_client_drive {
class PlayerFeedback {
    struct Impl;std::unique_ptr<Impl>impl_;
public:
    explicit PlayerFeedback(_object*battle);~PlayerFeedback();
    void observed(ID first,ID second,const Point&,const Point&,double now);
    void present(_object*local,const offline_contact::Body&,const ContactPeer&,double now);
};
}
