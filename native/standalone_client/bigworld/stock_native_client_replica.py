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
    battle._native_client_state = _native.state(sync._standalone_replica_owner)
    return True


def receive(sync, message, manifest=False):
    owner = getattr(sync, '_standalone_replica_owner', None)
    if owner is None:
        return None
    battle = sync._standalone_replica_battle()
    if battle is None:
        return []
    receiver = _native.manifest if manifest else _native.snapshot
    pending = receiver(owner, sync, message, sync._now())
    output = []
    for event in pending:
        # The existing battle event consumer drains the accepted event journal
        # before reconciling snapshot health. Preserve that callback order.
        sync._emit(event, output)
        if getattr(sync, '_standalone_replica_owner', None) is not owner:
            return output
    _report(sync, _native.refresh(owner, battle))
    _query_ingest(battle)
    return output


def ingest(sync):
    # The native receive path owns typed histories from the accepted message.
    # It must never reconstruct them from SnapshotSync._entities.
    return None


def _query_ingest(battle):
    if getattr(battle, '_native_client_query', None) is not None:
        from . import native_client_query
        native_client_query.ingest(battle)


def owns(battle, record):
    owner = getattr(battle._sync, '_standalone_replica_owner', None)
    return (owner is not None and record.get('native_remote')
            and record.get('ready') and not record.get('local')
            and _native.manages(owner, record))


def _report(sync, errors):
    reference = getattr(sync, '_standalone_replica_battle', None)
    battle = reference() if reference is not None else None
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
    battle = sync._standalone_replica_battle()
    if battle is not None:
        errors, changed = _native.refresh_pending(owner, battle)
        _report(sync, errors)
        if changed:
            _query_ingest(battle)
        if getattr(sync, '_standalone_replica_owner', None) is not owner:
            return []
    pending, errors = _native.advance(owner, sync, now)
    output = []
    for event in pending:
        sync._emit(event, output)
    if errors:
        _report(sync, errors)
    return output


def probes(sync, now):
    owner = getattr(sync, '_standalone_replica_owner', None)
    return None if owner is None else _native.probes(owner, now)


def flush_pending(battle, now):
    owner = getattr(battle._sync, '_standalone_replica_owner', None)
    return None if owner is None else _native.flush_pending(owner, battle, now)


def retire(battle):
    sync = getattr(battle, '_sync', None)
    owner = getattr(sync, '_standalone_replica_owner', None)
    if owner is not None:
        _native.close(owner)
        del sync._standalone_replica_owner
        del sync._standalone_replica_battle
    battle._native_client_state = None
