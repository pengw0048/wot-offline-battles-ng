"""Install the original client-space and entity/space clear owners with real retirement."""
import argparse
from pathlib import Path


def replace(source, before, after, marker=None):
    count = source.count(marker or after)
    if count == 1:
        return source
    if count > 1:
        raise ValueError('Duplicate space-lifecycle marker: ' + repr(marker or after))
    if source.count(before) != 1:
        raise ValueError('Pinned space-lifecycle seam changed: ' + repr(before))
    return source.replace(before, after, 1)


ORIGINAL_CLEAR_SPACES = r'''void ConnectionControl::clearSpaces()
{
    BWServerConnection * pServerConn;
    BWNullConnection * pNullConn;
    BWReplayConnection * pReplayConn;

    if (pConnection_ == NULL)
    {
        return;
    }

    if ((pNullConn = this->pNullConnection()) != NULL)
    {
        pNullConn->destroyPlayerCellEntity();
        pNullConn->destroyPlayerBaseEntity();
    }
    else if ((pServerConn = this->pServerConnection()) != NULL &&
        (pServerConn->isLoggingIn() || pServerConn->isOnline()))
    {
        ERROR_MSG( "ConnectionControl::clearSpaces: "
            "Called while still connected to a server. Ignoring request.\n" );
        return;
    }
    else if ((pReplayConn = this->pReplayConnection()) != NULL &&
        pReplayConn->pReplayController() != NULL)
    {
        ERROR_MSG( "ConnectionControl::clearSpaces: "
            "Called while still playing a replay. Ignoring request.\n" );
        return;
    }

    pConnection_->clearAllEntities();

    this->destroyMainConnection();
}'''


ORIGINAL_FINI = r'''void ConnectionControl::fini()
{
    // Clear entities in online, offline, replay spaces
    this->clearSpaces();

    // Clear entites in local spaces 
    ClientSpaceConnections::iterator iter = clientSpaceConnections_.begin();
    while (iter != clientSpaceConnections_.end())
    {
        iter->second->clearAllEntities();

        delete iter->second;
        ++iter;
    }
    clientSpaceConnections_.clear();

    // SpaceManager owns the elements
    clientSpaces_.clear();
}'''


def patch(root):
    root = Path(root)
    files = {}
    path = root / 'client/script_bigworld.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, '#include "pch.hpp"', '#include "pch.hpp"\n#include "stock_space_lifecycle.hpp"', '#include "stock_space_lifecycle.hpp"')
    source = replace(source, '\tMath_token |', '\tStockSpaceLifecycle_token |\n\tMath_token |', '\tStockSpaceLifecycle_token |')
    files[path] = source
    path = root / 'client/connection_control.hpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, '\tvoid destroyMainConnection();', '\tvoid destroyMainConnection();\n\tvoid clearStockMainSpace( bool keepLocalEntities = false );', '\tvoid clearStockMainSpace( bool keepLocalEntities = false );')
    source = replace(source, 'private:\n', 'private:\n\tfriend void clearStockEntitiesAndSpaces( bool keepClientOnlySpaces );\n', '\tfriend void clearStockEntitiesAndSpaces( bool keepClientOnlySpaces );')
    source = replace(source, 'private:\n', 'private:\n\tfriend class StockSpaceLifecycle;\n', '\tfriend class StockSpaceLifecycle;')
    source = replace(source, '\tvoid destroyMainConnection();', '\tuint64 stockMainSpaceRevision_ = 0;\n\tvoid destroyMainConnection();', '\tuint64 stockMainSpaceRevision_ = 0;')
    source = replace(source, '\tvoid destroyMainConnection();', '\tunsigned stockClientSpacesClosedAt_ = ~0u;\n\tvoid destroyMainConnection();', '\tunsigned stockClientSpacesClosedAt_ = ~0u;')
    files[path] = source
    path = root / 'lib/space/space_manager.hpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, 'private:\n', 'private:\n\tfriend class StockSpaceLifecycle;\n', '\tfriend class StockSpaceLifecycle;')
    source = replace(source, '\tSpaceMap localSpaces_;\n', '\tSpaceMap localSpaces_;\n\tunsigned stockInitGeneration_ = 0;\n', '\tunsigned stockInitGeneration_ = 0;')
    files[path] = source
    path = root / 'lib/space/space_manager.cpp'
    source = path.read_text(encoding='utf-8-sig')
    # A new initialisation phase reopens client-space creation after fini.
    source = replace(source, 'void SpaceManager::init()\n{\n\n}', 'void SpaceManager::init()\n{\n\t++stockInitGeneration_;\n}')
    files[path] = source
    path = root / 'lib/space/client_space.hpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, '\tbool cleared() const;\n',
        '\tbool cleared() const;\n\n'
        '\t// A retiring owner stops async geometry completion from publishing\n'
        '\t// entities or UDOs into this space; terminal like clear().\n'
        '\tvoid stockRevokeLoadPublication() { stockLoadPublicationRevoked_ = true; }\n'
        '\tbool stockLoadPublicationRevoked() const { return stockLoadPublicationRevoked_; }\n',
        '\tvoid stockRevokeLoadPublication()')
    source = replace(source, '\tbool isCleared_;\n', '\tbool isCleared_;\n\tbool stockLoadPublicationRevoked_ = false;\n',
        '\tbool stockLoadPublicationRevoked_ = false;')
    files[path] = source
    path = root / 'lib/compiled_space/compiled_space_mapping.hpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, '\tLoaderTask* pTask_;\n\tuint32 numFailures_;\n',
        '\tLoaderTask* pTask_;\n\tuint32 numFailures_;\n\tbool stockPublicationRevoked_ = false;\n',
        '\tbool stockPublicationRevoked_ = false;')
    files[path] = source
    path = root / 'lib/compiled_space/compiled_space_mapping.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, '\tif (state_ == STATE_UNLOADED)\n\t{\n\t\treturn;\n\t}\n\n\tif (pTask_)\n\t{\n\t\tpTask_->wait();\n\t}\n',
        '\tif (state_ == STATE_UNLOADED)\n\t{\n\t\treturn;\n\t}\n\n'
        '\t// wait() pumps this mapping\'s own completion through TaskManager::tick.\n'
        '\t// A mapping being unloaded must not publish entities or UDOs from it.\n'
        '\tstockPublicationRevoked_ = true;\n'
        '\tif (pTask_)\n\t{\n\t\tpTask_->wait();\n\t}\n',
        '\tstockPublicationRevoked_ = true;')
    source = replace(source, '\t\tspace_.mappingLoaded( this );\n\n\t\tthis->spawnEntities();\n\t\tthis->spawnUserDataObjects();\n',
        '\t\t// Loaders stay bound so unloadSync unloads them symmetrically. A\n'
        '\t\t// retired mapping or space publishes no loaded area/bounds, entities\n'
        '\t\t// or UDOs: its owner is detached and mappings_ may already be empty.\n'
        '\t\tif (stockPublicationRevoked_ || space_.cleared() ||\n'
        '\t\t\tspace_.stockLoadPublicationRevoked())\n\t\t{\n'
        '\t\t\tTRACE_MSG( "Compiled space \'%s\' completed while retiring; "\n'
        '\t\t\t\t"not published\\n", path_.c_str() );\n\t\t}\n'
        '\t\telse\n\t\t{\n\t\t\tspace_.mappingLoaded( this );\n\n'
        '\t\t\tthis->spawnEntities();\n\t\t\tthis->spawnUserDataObjects();\n\t\t}\n',
        'space_.stockLoadPublicationRevoked())')
    files[path] = source
    path = root / 'lib/compiled_space/compiled_space.cpp'
    source = path.read_text(encoding='utf-8-sig')
    # Unloading waits pump other completions; detach before deleting so a
    # re-entrant clear/unmap never sees a half-deleted mapping.
    source = replace(source, '\tfor (size_t i = 0; i < mappings_.size(); ++i)\n\t{\n\t\tbw_safe_delete( mappings_[i].pLoader_ );\n\t}\n\n\tmappings_.clear();\n',
        '\tMappings retired;\n\tretired.swap( mappings_ );\n'
        '\tfor (size_t i = 0; i < retired.size(); ++i)\n\t{\n\t\tbw_safe_delete( retired[i].pLoader_ );\n\t}\n',
        '\tretired.swap( mappings_ );')
    source = replace(source, '\t\t\tbw_safe_delete( mappings_[i].pLoader_ );\n\t\t\tmappings_.erase( mappings_.begin() + i );\n\t\t\tbreak;\n',
        '\t\t\tCompiledSpaceMapping * pRetired = mappings_[i].pLoader_;\n'
        '\t\t\tmappings_.erase( mappings_.begin() + i );\n\t\t\tbw_safe_delete( pRetired );\n\t\t\tbreak;\n',
        '\t\t\tCompiledSpaceMapping * pRetired = mappings_[i].pLoader_;')
    files[path] = source
    path = root / 'client/connection_control.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, '#include "connection_control.hpp"\n', '#include "connection_control.hpp"\n#include "stock_space_lifecycle.hpp"\n', '#include "stock_space_lifecycle.hpp"')
    source = replace(source, ORIGINAL_CLEAR_SPACES, "void ConnectionControl::clearSpaces()\n{\n    this->clearStockMainSpace();\n}")
    source = replace(source, ORIGINAL_FINI, "void ConnectionControl::fini()\n{\n    finiStockClientSpaces();\n}")
    source = replace(source, 'SpaceID ConnectionControl::createLocalSpace()\n{\n    BW_GUARD;\n\n',
        'SpaceID ConnectionControl::createLocalSpace()\n{\n    BW_GUARD;\n\n    if (!stockClientSpacesOpen())\n    {\n'
        '        ERROR_MSG( "ConnectionControl::createLocalSpace: client spaces are finalised\\n" );\n'
        '        return NULL_SPACE_ID;\n    }\n\n', '    if (!stockClientSpacesOpen())')
    source = replace(source, '    BWConnection * pConnection = pConnection_;\n    pConnection_ = NULL;',
        '    BWConnection * pConnection = pConnection_;\n    ++stockMainSpaceRevision_;\n    pConnection_ = NULL;')
    before = '''        ClientSpaceConnections::iterator iter = clientSpaceConnections_.begin();
        while (iter != clientSpaceConnections_.end())
        {
            // This should never have any non-local entities, so this will have
            // no effect except advancing BWNullConnection::clientTime
            iter->second->update( dt );
            iter->second->updateServer();
            // TODO: I'd like this to be true... But I think server
            // time-correction is causing it to drift.
            /*
            MF_ASSERT( isEqual( pClientSpaceConnection_->clientTime(),
                this->gameTime() ) );
            */

            ++iter;
        }'''
    after = '''        const ClientSpaceConnections snapshot = clientSpaceConnections_;
        for (const auto& entry : snapshot)
        {
            auto live = clientSpaceConnections_.find( entry.first );
            if (live == clientSpaceConnections_.end() || live->second != entry.second) continue;
            entry.second->update( dt );
            live = clientSpaceConnections_.find( entry.first );
            if (live != clientSpaceConnections_.end() && live->second == entry.second)
                entry.second->updateServer();
        }'''
    source = replace(source, before, after)
    before = '''        Connections::iterator iter = condemnedConnections_.begin();
        while (iter != condemnedConnections_.end())
        {
            delete *iter;
            ++iter;
        }

        condemnedConnections_.clear();'''
    after = '''        while (!condemnedConnections_.empty())
        {
            Connections retired;
            retired.swap( condemnedConnections_ );
            for (BWConnection* connection : retired) delete connection;
        }'''
    source = replace(source, before, after)
    files[path] = source
    header = Path(__file__).with_name('stock_space_lifecycle.hpp').read_bytes()
    # Validate every owner seam before publishing any source mutation.
    for path, source in files.items():
        path.write_text(source, encoding='utf-8')
    (root / 'lib/stock_space_lifecycle.hpp').write_bytes(header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
