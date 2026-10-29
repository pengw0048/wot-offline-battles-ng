"""Lifecycle and LAN boundary for the persistent native worker simulation.

No planning, collision, motion, gunnery or per-actor loop lives in this module.
"""
import offline_bot_runtime_native as native


def enabled(runtime):
    battle = getattr(runtime, '_standalone_battle', None)
    return battle is not None and bool(battle._worker_mode)


def retire(runtime):
    owner = getattr(runtime, '_native_bot_owner', None)
    runtime._native_bot_owner = None
    if owner is not None:
        native.close(owner)


def prepare(runtime):
    if not runtime.is_authority() or runtime.adapter is None or runtime.finished:
        return None
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        owner = native.create(runtime, runtime._standalone_battle)
        runtime._native_bot_owner = owner
    return owner


def frame(runtime, now, force=False):
    owner = prepare(runtime)
    if owner is None:
        return False
    changed = native.frame(
        owner, runtime, runtime._standalone_battle, now, int(force))
    flush_world(runtime)
    return changed


def prewarm(runtime, now):
    owner = prepare(runtime)
    return 0 if owner is None else native.prewarm(
        owner, runtime, runtime._standalone_battle, now)


def prewarm_players(runtime, now):
    owner = prepare(runtime)
    return 0 if owner is None else native.prewarm_players(
        owner, runtime, runtime._standalone_battle, now)


def players(runtime):
    return native.players(runtime, runtime._standalone_battle)


def player_environment(runtime, dt, now):
    battle = runtime._standalone_battle
    if dt <= 0.0 or battle.client is None:
        return False
    owner = prepare(runtime)
    if owner is None:
        return False
    batch = native.player_environment(
        owner, runtime, battle, dt, int(battle.client.is_bot_authority()))
    if batch is None:
        return False
    sequence, observations = batch
    sender = getattr(battle.client, 'send_player_environment', None)
    sent = bool(callable(sender) and sender(observations, sequence))
    native.ack_player_environment(owner, battle, sequence, int(sent))
    return sent


def human_ram(runtime):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return []
    return native.human_ram(owner, runtime, runtime._standalone_battle)


def update(runtime, dt, now, players=None, neighbours=None):
    if not runtime.is_authority() or runtime.adapter is None or runtime.finished:
        return []
    battle = runtime._standalone_battle
    owner = prepare(runtime)
    # The boundary copies accepted ingress and emits plain messages. The C++
    # owner consumes every physical slice before returning to this function.
    messages = native.update(owner, runtime, battle, dt, now, players)
    flush_world(runtime)
    return messages


def flush_world(runtime):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return True
    battle = runtime._standalone_battle
    events = native.world_events(owner, runtime, battle)
    if not battle._battle_live:
        return not events
    for sequence, event in events:
        if not battle.client.send_destructible(event):
            return False
        native.ack_world(owner, sequence)
    return True


def world_ingress(runtime, event, now):
    owner = prepare(runtime)
    if owner is None:
        return False
    return native.world_ingress(owner, runtime, event, now)


def player_contacts(runtime, players, now):
    owner = prepare(runtime)
    if owner is None:
        return 0
    battle = runtime._standalone_battle
    results = native.player_contacts(owner, runtime, battle, players, now)
    if not flush_world(runtime):
        return 0
    sent = 0
    for row in results:
        if battle.client.send_player_destructible_contact_result(
                row['player_id'], row['seq'], row['accepted'], row['token']):
            native.ack_player_contact(owner, row['serial'], now)
            sent += 1
    return sent


def snapshot(runtime, message):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return False
    native.snapshot(owner, runtime, message)
    if message.get('battle_result') is not None:
        runtime.finished = True
    return True


def detached_queue(runtime, record, entity, state, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        owner = prepare(runtime)
    return owner is not None and native.detached_queue(
        owner, runtime, record, entity, state, now)


def detached_ingress(runtime, message, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return None
    return native.detached_ingress(owner, runtime, message, now)


def detached_advance(runtime, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return 0
    battle = runtime._standalone_battle
    tail = native.detached_advance(owner, runtime, battle, now)
    if tail is not None:
        battle.client.send_bot_state([], **tail)
    return 0


def detached_rows(runtime):
    owner = getattr(runtime, '_native_bot_owner', None)
    return [] if owner is None else native.detached_rows(owner)


def projectile_ingress(runtime, message, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return None
    return native.projectile_ingress(owner, runtime, message, now)


def projectile_observe(runtime, message, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return None
    return native.projectile_observe(owner, runtime, message, now)


def projectile_event(runtime, event, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        owner = prepare(runtime)
    return owner is not None and native.projectile_event(
        owner, runtime, event, now)


def projectile_advance(runtime, now, force_progress=False):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return 0
    battle = runtime._standalone_battle
    rows = native.projectile_advance(
        owner, runtime, battle, now, int(force_progress))
    senders = {
        'progress': battle.client.send_projectile_progress,
        'resolve': battle.client.send_projectile_resolve,
        'ricochet': battle.client.send_projectile_ricochet,
    }
    sent_count = 0
    for serial, kind, wire in rows:
        sent = False
        try:
            sent = bool(senders[kind](**wire))
        except Exception as error:
            native.transport_error(owner, runtime, str(error))
        finally:
            native.projectile_ack(owner, serial, int(sent))
        sent_count += int(sent)
    return sent_count


def projectile_launches(runtime, message):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return False
    sender = getattr(runtime._standalone_battle.client, 'send_projectile_launch', None)
    if not callable(sender):
        return False
    complete = True
    wave = native.projectile_launches(owner, runtime, message)
    while wave:
        for bot_id, sequence, args, kwargs in wave:
            accepted = False
            try:
                accepted = sender(*args, **kwargs) == sequence
            except Exception as error:
                native.transport_error(owner, runtime, str(error))
            finally:
                native.projectile_launch_result(owner, bot_id, sequence, int(accepted))
            complete = accepted and complete
        wave = native.projectile_launches(owner, runtime, None)
    return complete


def presentation(runtime, now=None):
    if not runtime.is_authority() or runtime.adapter is None or runtime.finished:
        return ()
    owner = getattr(runtime, '_native_bot_owner', None)
    return native.presentation(owner, runtime)


def apply_presentation(runtime, states, now):
    owner = getattr(runtime, '_native_bot_owner', None)
    if owner is None:
        return False
    return native.apply_presentation(
        owner, runtime, runtime._standalone_battle, states, now)


def acknowledge_launch(runtime, bot_id, sequence):
    owner = getattr(runtime, '_native_bot_owner', None)
    return owner is not None and native.ack_launch(owner, bot_id, sequence)


def acknowledge_ram(runtime, player_id, sequence):
    owner = getattr(runtime, '_native_bot_owner', None)
    return owner is not None and native.ack_ram(owner, player_id, sequence)
