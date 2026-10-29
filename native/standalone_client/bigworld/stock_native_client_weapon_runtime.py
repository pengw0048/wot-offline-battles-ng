"""Consume native gun publication edges at stock HUD/audio boundaries only."""
from __future__ import print_function
import offline_client_weapon_runtime_native as _native


def running(battle):
    # The native Avatar's first targeting parameter initialization precedes
    # scene/status preparation. That single cold lifecycle edge stays stock.
    return (not battle._worker_mode and not battle._replay_mode and
            battle.state == 'running')


def advance(battle, entity, now=None):
    if battle._replay_mode:
        return battle._gun_state
    if not running(battle):
        return None
    return _native.advance(battle, entity,
                           battle._clock() if now is None else float(now))


def publish(battle, kind, now=None, force=False, remaining=0.0, duration=0.0):
    if battle._replay_mode and not battle._replay_publishing:
        return False
    if not running(battle):
        return None
    return _native.publish(
        battle, kind, battle._clock() if now is None else float(now),
        int(force), float(remaining), float(duration))


def reload_factor(battle):
    return _native.reload_factor(battle)


def refresh(battle, now=None):
    if running(battle):
        _native.refresh(battle, battle._clock() if now is None else float(now))


def _publish(battle, entity, edges):
    owner = getattr(battle, '_native_client_weapon_runtime', None)
    accepted = []
    def alive():
        return (getattr(battle, '_native_client_weapon_runtime', None) is owner
                and battle.state == 'running')
    for edge in edges:
        if not alive():
            break
        try:
            kind = edge['kind']
            vehicle_id = battle._server.vehicle_id
            if kind == 'ammo':
                battle._avatar.updateVehicleAmmo(
                    vehicle_id, edge['compact'], edge['quantity'],
                    edge['clip'], 0)
            elif kind == 'shell':
                battle._avatar.updateVehicleSetting(
                    vehicle_id, edge['setting'], edge['compact'])
            elif kind == 'equipment':
                battle._avatar.updateVehicleAmmo(
                    vehicle_id, edge['compact'], edge['quantity'],
                    edge['stage'], edge['remaining'])
            elif kind == 'targeting':
                turret_yaw, gun_pitch = entity.getAimParams()
                if not alive():
                    return accepted
                battle._avatar.updateTargetingInfo(
                    turret_yaw, gun_pitch, *edge['signature'])
                if not alive():
                    return accepted
                battle._targeting_signature = tuple(edge['signature'])
            elif kind == 'reload':
                battle._avatar.updateVehicleGunReloadTime(
                    vehicle_id, edge['remaining'], edge['duration'])
                if not alive():
                    return accepted
                battle._reload_event = (
                    edge['remaining'], edge['duration'])
                # The native owner already selected and froze this edge. The
                # original resource owner supplies only the mechanical effect.
                audio = None
                if not battle._replay_mode:
                    audio = battle._run_optional_feature(
                        'live reload sound', battle._ensure_live_reload_sound)
                if not alive():
                    return accepted
                if audio:
                    if not edge['live']:
                        audio.stop()
                    else:
                        battle._run_optional_feature(
                            'live reload sound', audio.on_edge,
                            ((edge['remaining'], edge['duration'],
                              edge['reason']), edge['gun'],
                             edge['now'], edge['now']))
            accepted.append(edge['id'])
        except Exception as error:
            if not alive():
                return accepted
            battle._warn_optional_failure(
                'native weapon ' + edge['kind'], error, disable=False)
    return accepted


def retire(battle):
    if getattr(battle, '_native_client_weapon_runtime', None) is not None:
        _native.close(battle)
        del battle._native_client_weapon_runtime


def equipment_ingress(battle, states):
    if running(battle):
        _native.equipment_ingress(battle, states)


def restore_equipment_snapshot(battle, snapshot, present=False):
    return (None if not running(battle) else
            _native.equipment_snapshot(battle, snapshot, int(present)))


def audio_frame(battle, now):
    if not running(battle):
        return None
    try:
        return _native.audio_frame(battle, float(now))
    except Exception as error:
        battle._warn_optional_failure('native reload sound cue', error,
                                     disable=False)
        return False
