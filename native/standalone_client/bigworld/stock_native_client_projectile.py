"""Discrete stock presentation edges for native visible-player projectiles.

The persistent C++ owner freezes fire transactions, advances every collision
chord, resolves damage and publishes reliable FIFO terminal/progress messages.
Stock muzzle, tracer motors, HUD and accepted journal feedback stay at their
existing Python lifecycle edges; no per-shell frame business callback runs.
"""
from __future__ import print_function
import offline_client_projectile_native as _native
import offline_client_weapon_runtime_native as _weapon
import offline_client_transport_native as _transport


def enabled(battle):
    return (not battle._worker_mode and not battle._replay_mode
            and getattr(battle, '_native_client_state', None) is not None)


def bind(battle, now=None):
    owner = getattr(battle, '_native_client_projectile', None)
    if owner is None:
        # Opening battle_start is observed before the final running edge.
        # Install the required scene and query owners at this cold boundary.
        from . import native_client_drive, native_client_query
        native_client_drive.prepare(battle)
        if not native_client_query.bind(battle):
            raise RuntimeError('Native player projectile query is unavailable')
        now = _native.now(battle) if now is None else now
        owner = _native.create(battle, now)
        battle._native_client_projectile = owner
    return owner


def _install(battle, event):
    # Store the runtime's canonical spelling so later shot echoes compare equal.
    normalized = battle._projectile_wire_meta(event)
    if normalized is None:
        battle._warn_optional_failure(
            'native projectile presentation',
            RuntimeError('invalid native launch %s' % event.get('projectile_id')),
            disable=False)
        return None
    meta = dict(event)
    meta.update(normalized)
    meta['manager_key'] = meta['projectile_id']
    meta['presentation_offsets'] = {}
    meta['destructibles_pending'] = []
    battle._projectile_meta[meta['projectile_id']] = meta
    return meta


def _publish(battle, now):
    # Both calls decide native deltas and emit only actual stock HUD edges.
    # The source postcommit checkpoint is retained after the transaction.
    _weapon.publish(battle, 'ammo', now, 1)
    gun = battle._gun_state
    _weapon.publish(battle, 'reload', now, 1,
                    gun.reload_time, gun.reload_duration)
    _transport.send_current(battle)


def _accepted(battle, owner, event, now):
    if getattr(battle, '_native_client_projectile', None) is not owner:
        return False
    if not isinstance(event, dict):
        return battle._reject_local_fire(event)
    # This is the presentation record consumed by stock muzzle/tracer edges.
    # Physics, CAS, damage and ammunition never consult this compatibility map.
    _install(battle, event)
    battle._local_fire_intent = None
    try:
        _publish(battle, now)
    except Exception as error:
        battle._warn_optional_failure('local shot publication', error,
                                     disable=False)
    if getattr(battle, '_native_client_projectile', None) is owner:
        battle._defer_local_projectile_presentation(event)
    return True


def shoot(battle, continuing=False):
    now = _native.now(battle)
    owner = bind(battle, now)
    return _accepted(battle, owner,
                     _native.trigger(owner, battle, now, int(continuing)), now)


def advance_burst(battle):
    owner = getattr(battle, '_native_client_projectile', None)
    if owner is None:
        return False
    now = _native.now(battle)
    if not _native.due(owner, now):
        return False
    result = _native.trigger(owner, battle, now, 1)
    if isinstance(result, dict):
        return _accepted(battle, owner, result, now)
    cancel(battle, True)
    return False


def cancel(battle, apply_settings=False):
    owner = getattr(battle, '_native_client_projectile', None)
    if owner is None:
        return None
    burst = battle._local_player_burst
    now = _native.now(battle)
    changed = _native.cancel(owner, battle, now, int(apply_settings))
    if changed and burst is not None and burst['committed'] > 0:
        try:
            entity = battle._server_entity(battle._server.vehicle_id)
            extra = entity.typeDescriptor.extrasDict.get('shoot')
            if extra is not None:
                battle._run_optional_feature('shot muzzle retirement',
                                             extra.stopFor, args=(entity,))
        except Exception as error:
            battle._warn_optional_failure('shot muzzle retirement', error,
                                         disable=False)
    if changed and apply_settings:
        _publish(battle, now)
    return burst


def _epoch_changed(battle, previous):
    if battle._projectile_epoch == previous:
        return
    # An authority change retires native visual owners without impact feedback.
    # The player's burst/gun transaction continues independently of the worker.
    battle._reset_projectile_visuals()
    battle._projectile_visual_terminals = type(battle._projectile_visual_terminals)()
    battle._projectile_resolution_acks = type(battle._projectile_resolution_acks)()
    battle._local_projectile_presentations.clear()
    battle._local_projectile_presented = type(battle._local_projectile_presented)()
    battle._projectile_meta = {}
    battle._projectile_terminal_data = {}
    battle._projectile_scene_stop_reasons = {}


def observe(battle, message):
    now = _native.now(battle)
    previous = battle._projectile_epoch
    result = _native.observe(bind(battle, now), battle, message, now)
    _epoch_changed(battle, previous)
    return result


def ingress(battle, message):
    now = _native.now(battle)
    previous = battle._projectile_epoch
    edges = _native.ingress(bind(battle, now), battle, message, now)
    _epoch_changed(battle, previous)
    for event in edges:
        projectile_id = event['projectile_id']
        current = battle._projectile_meta.get(projectile_id)
        # These discrete records belong only to stock native tracer lifecycle.
        if current is None:
            current = _install(battle, event)
            if current is None:
                continue
        if event['ricochet_count'] > current['ricochet_count']:
            # Ordered ricochet feedback owns the visual segment transition.
            continue
        battle._ensure_projectile_visual(current, now)
    return True


def visual(battle, normalized, now, visual_start=None):
    result = battle._ensure_projectile_visual_native_presentation(
        normalized, now, visual_start=visual_start)
    owner = getattr(battle, '_native_client_projectile', None)
    if owner is not None and isinstance(normalized, dict):
        marker = battle._projectile_visual_meta.get(normalized['projectile_id'])
        settled = bool(result or (marker is not None and
                                 not marker.get('admitted', True)))
        _native.visual_result(owner, normalized['projectile_id'],
                              normalized['ricochet_count'], int(settled))
    return result


def resources(battle):
    owner = getattr(battle, '_native_client_projectile', None)
    if owner is not None:
        return _native.resources(owner, battle, _native.now(battle))
    return False


def discard(battle, projectile_id):
    owner = getattr(battle, '_native_client_projectile', None)
    return (False if owner is None else
            _native.discard(owner, battle, projectile_id))


def event(battle, message):
    now = _native.now(battle)
    previous = battle._projectile_epoch
    result = _native.event(bind(battle, now), battle, message, now)
    _epoch_changed(battle, previous)
    return result


def frame_sample(battle, now):
    return _native.frame_sample(bind(battle, now), battle, now)


def frame(battle, now, force=False):
    owner = bind(battle, now)
    result = _native.advance(owner, battle, now, int(force))
    for edge in result['edges']:
        meta = battle._projectile_meta.get(edge['projectile_id'])
        if meta is None:
            continue
        if edge['kind'] == 'pending':
            meta['pending_resolution'] = {}
            meta['hit_vehicle'] = edge['hit_vehicle']
            if 'terminal_velocity' in edge:
                meta['terminal_velocity'] = edge['terminal_velocity']
        else:
            battle._stop_projectile_visual(edge['projectile_id'],
                {'outcome': 'impact', 'impact': edge['impact']})
            for name in ('segment_origin', 'segment_velocity',
                         'segment_start_time_ms', 'ricochet_count',
                         'base_penetration_multiplier', 'checked_distance',
                         'piercing_loss'):
                meta[name] = edge[name]
            meta['manager_key'] = (edge['projectile_id'], 1)
            meta['pending_resolution'] = None
            meta['pending_ricochet'] = None
            meta['awaiting_resolution'] = False
            meta['destructibles_pending'] = []
    return result['active']


def retire(battle):
    owner = getattr(battle, '_native_client_projectile', None)
    if owner is not None:
        _native.close(owner)
        battle._native_client_projectile = None
