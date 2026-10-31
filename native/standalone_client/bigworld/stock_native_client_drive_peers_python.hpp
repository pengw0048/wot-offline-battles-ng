#pragma once
#include "stock_native_client_state.hpp"
#include <memory>
struct _object;
namespace native_battle {struct Battle;}
namespace stock_native_client_drive {
class NativePeers;
// The round owns tactics and default-crew producers. Accepted actor/resource
// revisions decide cold rebuilds; motion-only snapshots perform no Python read.
class PeerInstaller {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    PeerInstaller();
    ~PeerInstaller();
    PeerInstaller(const PeerInstaller&)=delete;
    PeerInstaller&operator=(const PeerInstaller&)=delete;
    void synchronize(const native_battle::Battle&,stock_native_client_state::State&,NativePeers&);
};
}
