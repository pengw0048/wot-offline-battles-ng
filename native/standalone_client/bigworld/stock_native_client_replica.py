"""Visible-client lifecycle glue for the persistent native replica owner.

The module never interpolates a pose or feeds a remote vehicle. Python keeps
ordered snapshot health/spotting/effect events and asynchronous stock creates;
only not-yet-ready entities return through that existing lifecycle adapter.
"""
from __future__ import print_function

import weakref
import offline_client_replica_native as _native


def bind(sync, battle):
    if battle._worker_mode or not battle._config.get('native_remote_vehicles', True):
        return False
    sync._standalone_replica_battle = weakref.ref(battle)
    sync._standalone_replica_owner = _native.create()
    return True


def ingest(sync):
    owner = getattr(sync, '_standalone_replica_owner', None)
    if owner is not None:
        battle = sync._standalone_replica_battle()
        if battle is not None:
            _report(sync, _native.ingest(owner, sync, battle))


def owns(battle, record):
    owner = getattr(battle._sync, '_standalone_replica_owner', None)
    return (owner is not None and record.get('native_remote')
            and record.get('ready') and not record.get('local')
            and _native.manages(owner, record))


def _report(sync, errors):
    battle = sync._standalone_replica_battle()
    if battle is not None:
        for row in errors:
            battle._warn_optional_failure(
                'native replica ' + row['entity'],
                RuntimeError(row['error']), disable=False)


def advance(sync, now):
    owner = getattr(sync, '_standalone_replica_owner', None)
    if owner is None:
        return None
    now = sync._now() if now is None else float(now)
    pending, errors = _native.advance(owner, sync, now)
    output = []
    for event in pending:
        sync._emit(event, output)
    if errors:
        _report(sync, errors)
    return output


def retire(battle):
    sync = getattr(battle, '_sync', None)
    owner = getattr(sync, '_standalone_replica_owner', None)
    if owner is not None:
        _native.close(owner)
        del sync._standalone_replica_owner
        del sync._standalone_replica_battle
