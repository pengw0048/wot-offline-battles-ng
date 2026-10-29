"""Discrete HUD and stock visual edges for the native visible environment."""
from __future__ import print_function
import offline_client_environment_native as _native


def _report(battle, errors):
    for row in errors:
        battle._warn_optional_failure(
            'native environment ' + row['entity'],
            RuntimeError(row['error']), disable=False)


def bind(battle):
    if battle._worker_mode or not hasattr(battle, '_native_client_state'):
        return False
    if getattr(battle, '_native_client_environment', None) is None:
        battle._native_client_environment = _native.create(battle)
        ingest(battle)
    return True


def ingest(battle):
    owner = getattr(battle, '_native_client_environment', None)
    if owner is not None:
        _report(battle, _native.ingest(owner, battle))


def _apply(battle, owner, result):
    if result is None:
        return False
    _report(battle, result['errors'])
    changed = False
    # Only actual presentation edges cross back into the stock lifecycle.
    for row in result['transitions']:
        record = battle._records.get(row['entity'])
        if (record is None or record.get('engine_id') != row['engine_id'] or
                getattr(battle, '_native_client_environment', None) is not owner):
            continue
        try:
            battle._set_record_spot_visibility(
                record, row['visible'], row['marker'])
            _native.acknowledge(owner, row)
            changed = True
        except Exception as error:
            battle._warn_optional_failure(
                'native visibility ' + row['entity'], error, disable=False)
    if getattr(battle, '_native_client_environment', None) is not owner:
        return changed
    if not battle._vision_feed_failed:
        try:
            if result['vision'] is not None:
                battle._avatar.syncVehicleAttrs(
                    {'circularVisionRadius': result['vision']})
                battle._published_vision_radius = result['vision']
            for device_id, active in result['devices']:
                if getattr(battle, '_native_client_environment', None) is not owner:
                    break
                battle._avatar.updateVehicleOptionalDeviceStatus(
                    battle._avatar.playerVehicleID, device_id, active)
                battle._published_still_devices[device_id] = active
        except Exception as error:
            battle._vision_feed_failed = True
            battle._warn_optional_failure('battle HUD vision feed', error)
    return changed


def frame(battle, now, hud_only=False):
    owner = getattr(battle, '_native_client_environment', None)
    return (None if owner is None else
            _apply(battle, owner, _native.frame(owner, battle, now, int(hud_only))))


def foliage(battle, now, force=False):
    owner = getattr(battle, '_native_client_environment', None)
    if owner is None:
        return None
    from . import native_client_drive
    native_client_drive.frame(battle, now)
    _report(battle, _native.foliage(owner, battle, now, int(force)))
    return False


def activate(battle, chunk_id, item_index):
    owner = getattr(battle, '_native_client_environment', None)
    return (None if owner is None else
            _native.activate(owner, int(chunk_id), int(item_index)))


def radio(battle, message, now):
    owner = getattr(battle, '_native_client_environment', None)
    if owner is None:
        return None
    try:
        accepted = _native.radio(owner, message, now)
        return (_apply(battle, owner, _native.frame(owner, battle, now, 2))
                if accepted else False)
    except Exception as error:
        battle._warn_optional_failure('native team visibility', error,
                                     disable=False)
        return False


def retire(battle):
    _native.retire_survival(battle)
    owner = getattr(battle, '_native_client_environment', None)
    if owner is not None:
        _native.close(owner)
        del battle._native_client_environment


def lifecycle(battle, record):
    owner = getattr(battle, '_native_client_environment', None)
    if owner is not None:
        _native.lifecycle(owner, record)
