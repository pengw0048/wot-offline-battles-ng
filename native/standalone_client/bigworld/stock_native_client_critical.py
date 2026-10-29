"""Only discrete native track-model, repair-timer and damage-info edges."""
import offline_client_critical_native as _native


def running(battle):
    return (battle.state == 'running' and not battle._worker_mode and
            not battle._replay_mode)


def frame(battle, dt):
    if not running(battle):
        return False
    try:
        _native.frame(battle, dt)
    except Exception as error:
        battle._warn_optional_failure(
            'native local track repair', error, disable=False)
    return True


def reconcile(battle, record, source, canonical):
    if not running(battle) or not record.get('local'):
        return None
    return _native.reconcile(battle, record, source, canonical)


def retire(battle):
    _native.close(battle)


def _publish(battle, entity, edges):
    owner = getattr(battle, '_native_client_critical', None)
    accepted = []
    for edge in edges:
        if (not running(battle) or
                getattr(battle, '_native_client_critical', None) is not owner):
            break
        try:
            kind = edge['kind']
            if kind == 'crash':
                appearance = getattr(entity, 'appearance', None)
                callback = getattr(appearance, 'delCrashedTrack', None)
                if callable(callback):
                    callback(edge['left'])
            elif kind == 'progress':
                battle._avatar.updateVehicleMiscStatus(
                    entity.id, edge['status'], edge['code'],
                    (edge['seconds'],))
            elif kind == 'repaired':
                # This is the existing stock GUI lifetime check, entered only
                # for a real repaired-device presentation transition.
                if battle._damage_info_is_serviceable():
                    battle._show_damage_info(
                        entity.id, edge['index'], edge['extra'], entity.id)
            accepted.append(edge['key'])
        except Exception as error:
            battle._warn_optional_failure(
                'native track repair presentation', error, disable=False)
    return accepted
