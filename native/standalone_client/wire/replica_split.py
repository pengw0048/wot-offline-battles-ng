"""Split a complete tick snapshot into the replica form against the last written one."""
from schema import (BOT_MOTION_NAMES, COUNTDOWN_NAMES, EQUIPMENT_COUNTDOWN_NAMES,
                    PLAYER_MOTION_NAMES, PLAYER_OWNER_NAMES)

_ACTOR_KEYS = ('players', 'bots', 'bot_equipment_contracts')


def _signature(state):
    """State without sampled countdowns; a change here makes the record due."""
    out = dict((k, v) for k, v in state.items() if k not in COUNTDOWN_NAMES)
    items = out.get('equipment_states')
    if isinstance(items, list):
        out['equipment_states'] = [
            dict((k, v) for k, v in item.items() if k not in EQUIPMENT_COUNTDOWN_NAMES)
            if isinstance(item, dict) else item for item in items]
    return out


def _actors(rows, motion_names, excluded, previous, full, restate=False):
    motion, states, signatures, seen = [], [], {}, {}
    old_signatures, old_motion = previous
    for row in rows:
        key = row['id']
        moved = tuple(row[name] for name in motion_names)
        seen[key] = moved
        if full or old_motion.get(key) != moved:
            entry = {'id': key}
            entry.update(zip(motion_names, moved))
            motion.append(entry)
        state = dict((k, v) for k, v in row.items() if k not in excluded)
        signature = _signature(state)
        signatures[key] = signature
        if full or restate or old_signatures.get(key) != signature:
            states.append(state)
    removed = [] if full else [key for key in old_signatures if key not in signatures]
    return motion, states, removed, (signatures, seen)


def encode(message, baseline, own_id, full):
    """Return (replica message, baseline) or (message, None) for non-tick snapshots."""
    if 'client_build' in message:
        return message, None
    round_id = message.get('round_id')
    full = bool(full or baseline is None or baseline['round'] != round_id)
    empty = ({}, {})
    players = [row for row in message['players'] if row.get('id') != own_id]
    own = [row for row in message['players'] if row.get('id') == own_id]
    player_motion, player_states, removed_players, player_base = _actors(
        players, PLAYER_MOTION_NAMES, PLAYER_MOTION_NAMES + PLAYER_OWNER_NAMES,
        empty if full else baseline['players'], full)
    contracts = message.get('bot_equipment_contracts')
    # Bot equipment indexes refer to this table; a new table restates every bot.
    contracts_changed = contracts is not None and (full or contracts != baseline['contracts'])
    bot_motion, bot_states, removed_bots, bot_base = _actors(
        message['bots'], BOT_MOTION_NAMES, BOT_MOTION_NAMES,
        empty if full else baseline['bots'], full, contracts_changed)
    out = dict((k, v) for k, v in message.items() if k not in _ACTOR_KEYS)
    out.update(type='replica_snapshot', full=full, player_motion=player_motion,
               bot_motion=bot_motion, player_states=player_states, bot_states=bot_states,
               removed_players=removed_players, removed_bots=removed_bots)
    if own:
        out['own'] = own[0]
    if contracts_changed:
        out['bot_equipment_contracts'] = contracts
    return out, {'round': round_id, 'players': player_base, 'bots': bot_base,
                 'contracts': contracts if contracts is not None else
                 (None if full else baseline['contracts'])}
