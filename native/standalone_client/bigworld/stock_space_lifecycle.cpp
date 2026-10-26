#ifndef WOT_STOCK_SPACE_LIFECYCLE_PORTABLE
#include "pch.hpp"
#include "stock_space_lifecycle.hpp"
#include "stock_space_sounds.hpp"
#include "space/deprecated_space_helpers.hpp"
#include "connection_control.hpp"
#include "connection_model/bw_connection.hpp"
#include "connection_model/bw_null_connection.hpp"
#include "connection_model/bw_replay_connection.hpp"
#include "connection_model/bw_server_connection.hpp"
#include "space/client_space.hpp"
#include "space/space_manager.hpp"
#include "pyscript/script.hpp"
#endif

BW_BEGIN_NAMESPACE
int StockSpaceLifecycle_token = 1;

void ConnectionControl::clearStockMainSpace()
{
    BWConnection* retired = pConnection_;
    if (!retired) return;
    BWNullConnection* offline = this->pNullConnection();
    BWServerConnection* server = this->pServerConnection();
    BWReplayConnection* replay = this->pReplayConnection();
    // Original 63f610 does not force an active network/replay owner offline.
    if (server && (server->isLoggingIn() || server->isOnline()))
    {
        ERROR_MSG("ConnectionControl::clearSpaces: Called while still connected to a server. Ignoring request.\n");
        return;
    }
    if (replay && replay->pReplayController() != NULL)
    {
        ERROR_MSG("ConnectionControl::clearSpaces: Called while still playing a replay. Ignoring request.\n");
        return;
    }
    const bool deferDeletion = isInTick_;
    if (deferDeletion) condemnedConnections_.push_back(retired);
    offlineTime_ = retired->clientTime();
    // Callbacks may create/clear another connection. Retire the old identity
    // before invoking them, and perform no writes to the live slot afterwards.
    ++stockMainSpaceRevision_;
    pConnection_ = NULL;
    mode_ = NONE;
    if (offline)
    {
        offline->destroyPlayerCellEntity();
        offline->destroyPlayerBaseEntity();
    }
    retired->clearAllEntities();
    if (!deferDeletion) delete retired;
}

void clearStockEntitiesAndSpaces(bool keepClientOnlySpaces)
{
    const ClientSpacePtr currentSpace = DeprecatedSpaceHelpers::cameraSpace();
    SpaceManager& manager = SpaceManager::instance();
    // Capture the operation's native identities before entity callbacks can
    // create another connection/space. ac2f90's keep branch selects these maps;
    // callbacks must not cause this retiring operation to clear a replacement.
    BW::vector<ClientSpacePtr> spaces;
    for (const auto& entry : manager.spaces_) spaces.push_back(entry.second);
    if (!keepClientOnlySpaces)
        for (const auto& entry : manager.localSpaces_) spaces.push_back(entry.second);
    ConnectionControl& control = ConnectionControl::instance();
    BWConnection* const mainConnection = control.pConnection_;
    const uint64 mainRevision = control.stockMainSpaceRevision_;
    ConnectionControl::ClientSpaceConnections connections;
    ConnectionControl::ClientSpaces clientSpaces;
    connections.swap(control.clientSpaceConnections_);
    clientSpaces.swap(control.clientSpaces_);
    const bool deferDeletion = control.isInTick_;
    if (deferDeletion)
        for (const auto& entry : connections) control.condemnedConnections_.push_back(entry.second);
    // 64f200 first flushes the captured current-space static sound feature.
    // Both local maps and selected geometry identities were captured before
    // this backend boundary. Other spaces retain their own live sound groups.
    clearStockCurrentSpaceSounds(currentSpace);
    // Main connection, all entry-time client connections, selected geometry.
    if (control.pConnection_ == mainConnection && control.stockMainSpaceRevision_ == mainRevision)
        control.clearSpaces();
    // 63fe70 clears the entry-time local connections even for keep=true.
    // Both maps were detached before the main connection's script callbacks.
    for (const auto& entry : connections)
    {
        entry.second->clearAllEntities();
        const auto space = clientSpaces.find(entry.first);
        if (space != clientSpaces.end() && !space->second->cleared()) space->second->clear();
        if (!deferDeletion) delete entry.second;
    }
    for (const auto& retired : spaces)
        if (manager.space(retired->id()) == retired.get() && !retired->cleared()) retired->clear();
}

static PyObject* py_clearEntitiesAndSpaces(PyObject* args)
{
    PyObject* value = NULL;
    if (!PyArg_ParseTuple(args, "|O:clearEntitiesAndSpaces", &value)) return NULL;
    bool keepClientOnlySpaces = false;
    if (value && Script::setData(value, keepClientOnlySpaces, "() argument 1 optionally")) return NULL;
    clearStockEntitiesAndSpaces(keepClientOnlySpaces);
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(clearEntitiesAndSpaces, BigWorld)
BW_END_NAMESPACE
