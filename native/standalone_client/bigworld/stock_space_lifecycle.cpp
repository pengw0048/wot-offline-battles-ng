#ifndef WOT_STOCK_SPACE_LIFECYCLE_PORTABLE
#include "pch.hpp"
#include "stock_space_lifecycle.hpp"
#include "stock_space_sounds.hpp"
#include "app.hpp"
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

class StockSpaceLifecycle
{
public:
    // 641050 searches only the client-space connections, never the main one.
    static BWNullConnection* clientSpaceConnection(SpaceID spaceID)
    {
        const ConnectionControl& control = ConnectionControl::instance();
        const auto found = control.clientSpaceConnections_.find(spaceID);
        return found == control.clientSpaceConnections_.end() ? NULL : found->second;
    }

    // 656540: the shared clearSpace/releaseSpace lookup and its ValueErrors.
    static ClientSpacePtr clientSpace(SpaceID spaceID, const char* methodName)
    {
        ClientSpacePtr space = SpaceManager::instance().space(spaceID);
        if (!space)
        {
            PyErr_Format(PyExc_ValueError, "%s: No space ID %d", methodName, spaceID);
            return NULL;
        }
        if (!clientSpaceConnection(spaceID))
        {
            PyErr_Format(PyExc_ValueError,
                "%s: Space ID %d is not a client space (or is no longer held)", methodName, spaceID);
            return NULL;
        }
        return space;
    }

    // ac2f90 selects every space, or only non-client spaces when keeping them.
    static BW::vector<ClientSpacePtr> selectSpaces(bool keepClientOnlySpaces)
    {
        const SpaceManager& manager = SpaceManager::instance();
        BW::vector<ClientSpacePtr> spaces;
        for (const auto& entry : manager.spaces_) spaces.push_back(entry.second);
        if (!keepClientOnlySpaces)
            for (const auto& entry : manager.localSpaces_) spaces.push_back(entry.second);
        return spaces;
    }

    // 63ff90 clears entities, deletes the local connection and drops the held
    // space. Retire both identities first so leave callbacks cannot release
    // this connection again; the caller still pins the ClientSpace.
    static void releaseClientSpace(SpaceID spaceID)
    {
        ConnectionControl& control = ConnectionControl::instance();
        const auto connection = control.clientSpaceConnections_.find(spaceID);
        if (connection == control.clientSpaceConnections_.end()) return;
        BWNullConnection* const retired = connection->second;
        control.clientSpaceConnections_.erase(connection);
        ClientSpacePtr held;
        const auto space = control.clientSpaces_.find(spaceID);
        if (space == control.clientSpaces_.end())
            ERROR_MSG("ConnectionControl::releaseSpace: client space %d is not held\n", spaceID);
        else
        {
            held = space->second;
            control.clientSpaces_.erase(space);
        }
        const bool deferDeletion = control.isInTick_;
        if (deferDeletion) control.condemnedConnections_.push_back(retired);
        retired->clearAllEntities();
        if (!deferDeletion) delete retired;
    }
};

void ConnectionControl::clearStockMainSpace(bool keepLocalEntities)
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
    retired->clearAllEntities(keepLocalEntities);
    if (!deferDeletion) delete retired;
}

void clearStockEntitiesAndSpaces(bool keepClientOnlySpaces)
{
    const ClientSpacePtr currentSpace = DeprecatedSpaceHelpers::cameraSpace();
    SpaceManager& manager = SpaceManager::instance();
    // Capture the operation's native identities before entity callbacks can
    // create another connection/space. ac2f90's keep branch selects these maps;
    // callbacks must not cause this retiring operation to clear a replacement.
    const BW::vector<ClientSpacePtr> spaces = StockSpaceLifecycle::selectSpaces(keepClientOnlySpaces);
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

// 63fc50 is the public createLocalSpace sequence: next local ID, SpaceManager
// space, held ClientSpacePtr, then a BWNullConnection at gameTime().
static SpaceID createSpace()
{
    return ConnectionControl::instance().createLocalSpace();
}
PY_AUTO_MODULE_FUNCTION(RETDATA, createSpace, END, BigWorld)

// ac2bb0 is ClientSpace::clear only; entities and the connection stay held.
static bool clearSpace(SpaceID spaceID)
{
    const ClientSpacePtr space = StockSpaceLifecycle::clientSpace(spaceID, "BigWorld.clearSpace()");
    if (!space) return false;
    space->clear();
    return true;
}
PY_AUTO_MODULE_FUNCTION(RETOK, clearSpace, ARG(SpaceID, END), BigWorld)

// The pinned space outlives 63ff90, as in the original wrapper.
static bool releaseSpace(SpaceID spaceID)
{
    const ClientSpacePtr space = StockSpaceLifecycle::clientSpace(spaceID, "BigWorld.releaseSpace()");
    if (!space) return false;
    StockSpaceLifecycle::releaseClientSpace(spaceID);
    return true;
}
PY_AUTO_MODULE_FUNCTION(RETOK, releaseSpace, ARG(SpaceID, END), BigWorld)

static bool isClientSpace(SpaceID spaceID)
{
    return SpaceManager::instance().space(spaceID) != NULL &&
        StockSpaceLifecycle::clientSpaceConnection(spaceID) != NULL;
}
PY_AUTO_MODULE_FUNCTION(RETDATA, isClientSpace, ARG(SpaceID, END), BigWorld)

// 64f160: geometry clear only (ac2f90), then a collection unless quitting.
static void clearAllSpaces(bool keepClientOnlySpaces)
{
    {
        const BW::vector<ClientSpacePtr> spaces = StockSpaceLifecycle::selectSpaces(keepClientOnlySpaces);
        SpaceManager& manager = SpaceManager::instance();
        for (const auto& space : spaces)
            if (manager.space(space->id()) == space.get()) space->clear();
    }
    if (!App::instance().isQuiting()) PyGC_Collect();
}
PY_AUTO_MODULE_FUNCTION(RETVOID, clearAllSpaces, OPTARG(bool, false, END), BigWorld)

// 651030 parses both flags before rejecting keepPlayerAround, then runs 63f610.
static PyObject* resetEntityManager(bool keepPlayerAround, bool keepLocalEntities)
{
    if (keepPlayerAround)
    {
        PyErr_SetString(PyExc_ValueError, "Setting keepPlayerAround to True is now invalid");
        return NULL;
    }
    ConnectionControl::instance().clearStockMainSpace(keepLocalEntities);
    Py_RETURN_NONE;
}
PY_AUTO_MODULE_FUNCTION(RETOWN, resetEntityManager, OPTARG(bool, false, OPTARG(bool, false, END)), BigWorld)
BW_END_NAMESPACE
