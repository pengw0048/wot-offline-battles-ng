"""Discrete stock lifecycle and presentation seams for native client queries."""
from __future__ import print_function
import weakref
import offline_client_query_native as _native


def _report(battle, errors):
    for row in errors:
        battle._warn_optional_failure(
            'native query ' + row['entity'], RuntimeError(row['error']),
            disable=False)


def bind(battle):
    if battle._worker_mode or not hasattr(battle, '_native_client_state'):
        return False
    if getattr(battle, '_native_client_query', None) is not None:
        # Projectile observation can create the owner while Vehicle startup is
        # pending. The later ready boundary must install its actual resources
        # before camera alignment and the first ammo publication.
        ingest(battle)
        return True
    from .entities.remote_vehicle import _SegmentCollisionResultExt
    owner = _native.create(battle, _SegmentCollisionResultExt)
    battle._native_client_query = owner
    compatibility = battle._runtime.compatibility
    compatibility._native_client_query_owner = owner
    obstacles = battle._detached_turret_obstacles
    if obstacles is not None:
        reference = weakref.ref(battle)
        def changed():
            current = reference()
            if current is not None:
                sync_turrets(current)
        obstacles._native_query_changed = changed
        sync_turrets(battle)
    ingest(battle)
    return True


def ingest(battle):
    owner = getattr(battle, '_native_client_query', None)
    if owner is not None:
        _report(battle, _native.ingest(owner, battle))
        if getattr(battle, '_native_client_environment', None) is not None:
            from . import native_client_environment
            native_client_environment.ingest(battle)
        if getattr(battle, '_native_client_projectile', None) is not None:
            from . import native_client_projectile
            native_client_projectile.resources(battle)


def lifecycle(battle, record):
    owner = getattr(battle, '_native_client_query', None)
    if owner is not None:
        _native.flags(owner, record)


def sync_turrets(battle):
    owner = getattr(battle, '_native_client_query', None)
    obstacles = battle._detached_turret_obstacles
    if owner is not None and obstacles is not None:
        _report(battle, _native.turrets(owner, obstacles._turrets,
                                        battle._detached_turret_rows))


def collidable(compatibility, except_ids, start, end):
    owner = getattr(compatibility, '_native_client_query_owner', None)
    if owner is None:
        return None
    import ProjectileMover
    return _native.candidates(owner, except_ids, start, end,
                              ProjectileMover.ProjectileAwareEntities.entities)


def segment_may_hit(compatibility, entity, start, end):
    owner = getattr(compatibility, '_native_client_query_owner', None)
    return (None if owner is None else
            _native.segment_may_hit(owner, entity, start, end))


def collisions(compatibility, entity, start, end):
    owner = getattr(compatibility, '_native_client_query_owner', None)
    return (None if owner is None else
            _native.collisions(owner, entity, start, end))


def pick(battle, start, direction, maximum, held, fov, now, epsilon):
    owner = getattr(battle, '_native_client_query', None)
    if owner is None:
        return None
    result = _native.outline(owner, start, direction, maximum, fov, held,
                             battle._avatar.spaceID,
                             battle._turret_server_time_ms(now), epsilon)
    if result is not None:
        _report(battle, result['errors'])
    return result


def retire(battle):
    owner = getattr(battle, '_native_client_query', None)
    if owner is None:
        return
    _native.close(owner)
    compatibility = battle._runtime.compatibility
    if getattr(compatibility, '_native_client_query_owner', None) is owner:
        del compatibility._native_client_query_owner
    obstacles = battle._detached_turret_obstacles
    if obstacles is not None and hasattr(obstacles, '_native_query_changed'):
        del obstacles._native_query_changed
    del battle._native_client_query


# Exact scripts.pkg #1513 direct import consumers. Modules loaded after the
# compatibility install import the already-patched canonical module function.
_COLLIDABLE_CONSUMERS = (
    'AvatarInputHandler.control_modes',
    'AvatarInputHandler.gun_marker_ctrl',
    'VehicleGunRotator',
)


def install_aliases(compatibility, original, replacement):
    import sys
    restore_aliases(compatibility)
    owned = []
    compatibility._native_query_aliases = owned
    for name in _COLLIDABLE_CONSUMERS:
        module = sys.modules.get(name)
        if module is None:
            continue
        if getattr(module, 'getCollidableEntities', None) is original:
            owned.append((module, original, replacement))
            module.getCollidableEntities = replacement


def restore_aliases(compatibility):
    for module, original, replacement in getattr(
            compatibility, '_native_query_aliases', ()):
        if getattr(module, 'getCollidableEntities', None) is replacement:
            module.getCollidableEntities = original
    compatibility._native_query_aliases = []
