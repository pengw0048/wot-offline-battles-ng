"""Visible-player lifecycle and rare impact effects for native suspension.

The native owner samples columns, filters the real scene, retains support and
integrates every suspension slice. Horizontal input/contact ownership remains
in the fixed local-player adapter. No ray or numeric-policy callback is passed.
"""
import offline_client_drive_native as native


def enabled(battle):
    return not battle._worker_mode


def prepare(battle):
    if not enabled(battle):
        return None
    owner = getattr(battle, '_native_client_drive_owner', None)
    if owner is None:
        owner = native.create(battle)
        battle._native_client_drive_owner = owner
    return owner


def retire(battle):
    owner = getattr(battle, '_native_client_drive_owner', None)
    battle._native_client_drive_owner = None
    if owner is not None:
        native.close(owner)


def frame(battle, now):
    owner = getattr(battle, '_native_client_drive_owner', None)
    if owner is not None:
        native.frame(owner, battle, now)


def ingress(battle, event):
    return native.ingress(prepare(battle), battle, event, battle._clock())


def restore(battle):
    owner = getattr(battle, '_native_client_drive_owner', None)
    if owner is not None:
        native.restore(owner, battle)


def advance(battle, entity, position, yaw, dt, height_delta, speed_delta):
    result, impact, witnesses, normal = native.advance(
        prepare(battle), battle, battle._local_suspension_params, position,
        yaw, dt, height_delta, speed_delta, battle._entity_turret_yaw(entity))
    if impact is not None:
        battle._landing_contact_pose = tuple(result)
        battle._landing_contact_witnesses = witnesses
        battle._landing_contact_normal = normal
        battle._apply_landing_impact(entity, impact, normal_impact=True)
    return result
