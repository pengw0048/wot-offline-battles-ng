"""Cold stock entity/effect edges around the native visible turret producer."""
import weakref
import offline_client_turret_presentation_native as _native


def bind(manager):
    owner = getattr(manager, '_native_turret_presentation', None)
    if owner is None:
        owner = _native.create(manager)
        manager._native_turret_presentation = owner
    return owner


def adopt(manager, turret):
    turret['_native_turret_manager'] = weakref.ref(manager)
    _native.adopt(bind(manager), turret)


def push(turret, row, now, elapsed):
    reference = turret.get('_native_turret_manager')
    manager = reference() if reference is not None else None
    if manager is not None:
        _native.push(bind(manager), turret, row, now, elapsed)


def advance(manager, now):
    return _native.advance(bind(manager), now)


def retire(manager):
    return _native.retire(bind(manager))


def clear(manager):
    return _native.clear(bind(manager))


def ingress(battle):
    if battle._worker_mode:
        return
    manager = battle._detached_turrets
    if (manager is not None and
            getattr(battle, '_native_client_state', None) is not None):
        _native.ingress(bind(manager), battle, battle._detached_turret_rows)


def frame(battle, now):
    manager = battle._detached_turrets
    if manager is None:
        return 0
    return _native.battle_frame(bind(manager), battle, now)
