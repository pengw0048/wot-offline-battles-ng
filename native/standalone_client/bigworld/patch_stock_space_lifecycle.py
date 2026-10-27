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
    files[path] = source
    path = root / 'lib/space/space_manager.hpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, 'private:\n', 'private:\n\tfriend class StockSpaceLifecycle;\n', '\tfriend class StockSpaceLifecycle;')
    files[path] = source
    path = root / 'client/connection_control.cpp'
    source = path.read_text(encoding='utf-8-sig')
    source = replace(source, ORIGINAL_CLEAR_SPACES, "void ConnectionControl::clearSpaces()\n{\n    this->clearStockMainSpace();\n}")
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
