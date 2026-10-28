"""Attach the recovered WoT replay owner to Python and real native connections."""
import argparse
from pathlib import Path


def after(source, anchor, addition):
    if addition in source:
        return source
    if source.count(anchor) != 1:
        raise RuntimeError('The reviewed replay owner anchor has changed: ' + repr(anchor))
    return source.replace(anchor, anchor + addition)


def before(source, anchor, addition):
    if addition in source:
        return source
    if source.count(anchor) != 1:
        raise RuntimeError('The reviewed replay teardown anchor has changed: ' + repr(anchor))
    return source.replace(anchor, addition + anchor)


def patch(root):
    root = Path(root)
    updates = []
    path = root / 'client/script_bigworld.cpp'
    source = after(path.read_text(), '#include "pch.hpp"', '\n#include "stock_replay_controller.hpp"')
    source = after(source,
        '\tif (!Script::init( paths, "client" ) )\n\t{\n\t\treturn false;\n\t}',
        '\n\n\tif (!initialiseStockReplayController())\n\t{\n\t\treturn false;\n\t}')
    updates.append((path, source))
    path = root / 'client/script_app.cpp'
    source = after(path.read_text(), '#include "pch.hpp"', '\n#include "stock_replay_controller.hpp"')
    source = before(source, '\tBigWorldClientScript::fini();', '\tfiniStockReplayController();\n')
    updates.append((path, source))
    path = root / 'client/connection_control.hpp'
    source = after(path.read_text(), '#include <memory>', '\n\nnamespace wot { struct StockReplayClock; }')
    source = after(source, '\tvoid stopReplay();', '''

	// WoT packet replay retains its own codec and native offline connection.
	BWNullConnection * startStockReplayConnection(
		const std::shared_ptr<wot::StockReplayClock> & clock );
	void stopStockReplayConnection( BWNullConnection * expected );''')
    updates.append((path, source))
    path = root / 'client/connection_control.cpp'
    source = after(path.read_text(), '#include "pch.hpp"', '\n#include "stock_replay_controller.hpp"')
    source = after(source, '    connectionHelper_.update( pConnection_, dt );',
                   '\n\n    tickStockReplayController( dt );')
    anchor = '''/**
 *  This method terminates the replay, in the same way disconnect terminates'''
    addition = '''BWNullConnection * ConnectionControl::startStockReplayConnection(
    const std::shared_ptr<wot::StockReplayClock> & clock )
{
    if (pConnection_ != NULL || !clock)
    {
        return NULL;
    }

    BWNullConnection * connection = createStockReplayNativeConnection(
        entityFactory_, spaceDataStorage_, EntityType::entityDefConstants(),
        offlineTime_, this->nextLocalSpaceID(), clock );
    pConnection_ = connection;
    mode_ = OFFLINE;
    server_.clear();
    pConnection_->addSpaceDataListener( EntityManager::instance() );
    pConnection_->setStreamDataFallbackHandler( EntityManager::instance() );
    pConnection_->addEntitiesListener( &EntityManager::instance() );
    return connection;
}


void ConnectionControl::stopStockReplayConnection( BWNullConnection * expected )
{
    if (pConnection_ != expected || mode_ != OFFLINE)
    {
        return;
    }

    pConnection_->clearAllEntities();
    if (pConnection_ == expected)
    {
        this->destroyMainConnection();
    }
}


'''
    source = before(source, anchor, addition)
    updates.append((path, source))
    path = root / 'client/entity.hpp'
    source = before(path.read_text(), '\tbool loadingPrerequisites() const { return loadingPrerequisites_; }',
                    '\t// #1513 Entity +0x140 suppresses replay effects during entry callbacks.\n'
                    '\tbool stockReplayEnteringWorld() const { return stockReplayEnteringWorld_; }\n\n')
    source = before(source, '\tbool loadingPrerequisites_;', '\tbool stockReplayEnteringWorld_ = false;\n')
    updates.append((path, source))
    path = root / 'client/entity.cpp'
    source = path.read_text()
    start = source.index('void Entity::onEnterWorld()\n{')
    end = source.index('\nvoid Entity::onPositionUpdated()', start)
    section = source[start:end]
    section = before(section, '\tthis->onEnterSpace( /* transient */ false );', '''
	// #1513 679b0d..679d63 brackets the native and script entry operations.
	// Restore on exceptional/partial entry too; nested entry preserves its owner.
	struct StockReplayEntryScope
	{
		bool & entering;
		bool previous;
		explicit StockReplayEntryScope( bool & value ) : entering( value ), previous( value ) { entering = true; }
		~StockReplayEntryScope() { entering = previous; }
	} stockReplayEntryScope( stockReplayEnteringWorld_ );

''')
    updates.append((path, source[:start] + section + source[end:]))
    for path, source in updates:
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
