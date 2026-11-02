"""Battle wire schema: one source for the server codec, the native client decoder and replay."""
from codec import (Any, Bool, Enum, Field, List, Map, OneOf, Record, Ref, Scalar, Schema, Str, Tuple,
                   Variant)

U8, U16, U32 = Scalar('u8'), Scalar('u16'), Scalar('u32')
I8, I32, I64, U64 = Scalar('i8'), Scalar('i32'), Scalar('i64'), Scalar('u64')
F64 = Scalar('f64')
BOOL = Bool()
STR = Str()
VEC3 = List(F64, 3)


def F(name, kind):
    return Field(name, kind)


def O(name, kind):
    return Field(name, kind, optional=True)


def N(name, kind):
    return Field(name, kind, nullable=True)


def ON(name, kind):
    return Field(name, kind, optional=True, nullable=True)


R = {}


def record(name, *fields):
    R[name] = Record(name, list(fields))
    return Ref(name)


ATTACKER_KIND = Enum('', 'player', 'bot')
DEVICE_STATE = Enum('normal', 'critical', 'destroyed')

# ---- shared ------------------------------------------------------------------
AUTHORITY = (F('worker_status', Enum('connected', 'failed', 'missing')),
             O('worker_failure_reason', STR))

TIMING = record('Timing',
    F('phase', Enum('loading', 'prebattle', 'battle', 'finished')),
    F('start_in_ms', I64), F('remaining_ms', I64), F('duration_ms', I64))

CRITICAL_DEVICE = record('CriticalDevice',
    F('name', STR), F('hp', F64), F('max_hp', F64), F('state', DEVICE_STATE))

R['CriticalEventDevice'] = Record('CriticalEventDevice', [
    F('name', STR), F('state', DEVICE_STATE), O('old_state', DEVICE_STATE), F('cause', STR)])
R['CriticalEventCrew'] = Record('CriticalEventCrew', [
    F('name', STR), F('state', Enum('normal', 'destroyed')), F('cause', STR)])
R['CriticalEventFire'] = Record('CriticalEventFire', [F('state', BOOL), F('cause', STR)])
R['CriticalEventAmmoRack'] = Record('CriticalEventAmmoRack', [
    F('state', Enum('destroyed')), F('cause', STR)])
CRITICAL_EVENT = Variant('kind', {'device': 'CriticalEventDevice', 'crew': 'CriticalEventCrew',
                                  'fire': 'CriticalEventFire', 'ammo_rack': 'CriticalEventAmmoRack'})

# Durable and event critical payloads; an empty dict means "no critical state".
CRITICAL = record('Critical',
    O('devices', List(CRITICAL_DEVICE, 16)), O('destroyed', List(STR, 16)),
    O('crew_ko', List(STR, 8)), O('crew_roster', List(STR, 8)),
    O('fire', BOOL), O('ammo_rack_death', BOOL), O('events', List(CRITICAL_EVENT, 24)))

STUN_FACTORS = Map(F64, 11)

SHELL_STUN = record('ShellStun',
    F('stunRadius', F64), F('stunDuration', F64), F('stunFactor', F64),
    F('guaranteedStunDuration', F64), F('damageDurationCoeff', F64),
    F('guaranteedStunEffect', F64), F('damageEffectCoeff', F64))

SHELL = record('Shell',
    F('kind', Enum('HOLLOW_CHARGE', 'HIGH_EXPLOSIVE', 'ARMOR_PIERCING', 'ARMOR_PIERCING_HE',
                   'ARMOR_PIERCING_CR')),
    F('caliber', F64), F('damage', List(F64, 2)), F('explosionRadius', F64),
    O('explosionDamageFactor', F64), O('explosionDamageAbsorptionFactor', F64),
    O('explosionEdgeDamageFactor', F64), O('stun', SHELL_STUN))

SOURCE_SHOT = record('SourceShot',
    F('speed', F64), F('gravity', F64), F('maxDistance', F64),
    F('piercingPower', List(F64, 2)), F('deadeye', BOOL), F('shell', SHELL))

PROJECTILE_FIELDS = (
    F('projectile_id', STR), F('shooter_kind', Enum('player', 'bot')), F('shooter_id', I64),
    F('source_vehicle', STR), F('source_shot', SOURCE_SHOT), F('shot_seq', I64),
    F('burst_group_seq', I64), F('burst_index', I32), F('burst_count', I32),
    F('shell_index', I32), F('team', I32), F('origin', VEC3), F('velocity', VEC3),
    F('range_origin', VEC3), F('segment_origin', VEC3), F('segment_velocity', VEC3),
    F('segment_start_time_ms', I64), F('ricochet_count', I32),
    F('base_penetration_multiplier', F64), F('gravity', F64), F('max_distance', F64),
    F('max_time_ms', I64), F('is_he', BOOL), F('splash_radius', F64),
    F('penetration_factor', F64), F('launch_server_time_ms', I64),
    F('checked_through_ms', I64), F('checked_distance', F64), F('piercing_loss', F64),
    F('authority_epoch', I64), O('fire_intent_seq', I64), O('fire_input_seq', I64),
    O('bot_presentation_time_us', I64))
PROJECTILE = record('Projectile', *PROJECTILE_FIELDS)

EQUIPMENT_CONTRACT = record('EquipmentContract',
    F('name', STR), F('kind', STR), F('id', I64), F('compactDescr', I64), F('tags', List(STR)),
    F('reuseCount', I64), F('cooldownSeconds', F64), F('autoactivate', BOOL),
    F('fireStartingChanceFactor', F64), F('repairAll', BOOL), F('bonusValue', F64),
    F('crewLevelIncrease', F64), F('enginePowerFactor', F64),
    F('turretRotationSpeedFactor', F64), F('engineHpLossPerSecond', F64),
    F('autoReactionSeconds', F64), O('stunResistanceDuration', F64),
    O('stunResistanceEffect', F64))

# Equipment state rows carry either a contract index (tick snapshot) or the full contract.
EQUIPMENT_STATE_FIELDS = (
    F('usesLeft', I64), F('cooldownTimeLeft', F64), F('active', BOOL),
    N('autoPendingElapsed', F64), N('aiPendingElapsed', F64))
EQUIPMENT_STATE_INDEXED = record('EquipmentStateIndexed', F('equipment', I32), *EQUIPMENT_STATE_FIELDS)
EQUIPMENT_STATE = record('EquipmentState', F('equipment', EQUIPMENT_CONTRACT), *EQUIPMENT_STATE_FIELDS)

TANK_PUSH = Tuple(I64, I64, F64, F64, F64, F64, F64)
TURRET_PUSH = Tuple(STR, I64, F64, F64, F64, F64, F64, F64)
RAM_MOTION_ROW = Tuple(*((I64, I64, I64) + (F64,) * 18))

BOT_ROW_FIELDS = (
    F('id', I32), F('team', I32), F('slot', I32), F('name', STR), F('vehicle', STR),
    F('world_pose', BOOL), F('x', F64), F('y', F64), F('z', F64), F('yaw', F64), F('pitch', F64),
    F('roll', F64), F('aim_yaw', F64), F('gun_pitch', F64), F('speed', F64),
    F('push_x', F64), F('push_z', F64), F('push_yaw', F64), F('ram_motion', List(RAM_MOTION_ROW, 30)),
    F('airborne', BOOL), F('service_brake', BOOL), F('route_wreck_blocked', BOOL),
    F('contact_push_acks', List(TANK_PUSH, 30)), F('movement_dir', I8), F('rotation_dir', I8),
    F('siege_state', I8), F('siege_time_left_ms', I64), F('siege_transition_total_ms', I64),
    F('fire_seq', I64), F('shell_index', I32), F('next_shell_index', I32),
    F('ammo_remaining', List(I64, 5)), F('ammo_reload_pending', BOOL), F('clip', I32),
    F('clip_size', I32), F('health', I64), F('max_health', I64), F('alive', BOOL),
    O('frags', I64), O('team_killer', BOOL), O('death_attacker_kind', ATTACKER_KIND),
    O('death_attacker_id', I64), O('combat_revision', I64), O('combat_base_revision', I64),
    O('combat_ack_seq', I64), O('combat_fire_elapsed', F64), O('combat_fire_timer', F64),
    O('fire_attacker_kind', ATTACKER_KIND), O('fire_attacker_id', I64),
    O('stun_end_server_time_ms', I64), O('stun_factors', STUN_FACTORS),
    O('stun_attacker_kind', ATTACKER_KIND), O('stun_attacker_id', I64),
    O('burst_active', BOOL), O('burst_group_seq', I64), O('burst_count', I32),
    O('burst_next_index', I32), O('burst_interval', F64), O('burst_time_left', F64),
    O('burst_shell_index', I32), O('reload_time', F64), O('reload_duration', F64),
    F('critical', CRITICAL), O('shot_yaw', F64), O('shot_pitch', F64), F('death_reason', I32),
    F('display_health', I64))
BOT_ROW = record('BotRow', *(BOT_ROW_FIELDS + (F('equipment_states', List(EQUIPMENT_STATE_INDEXED, 3)),)))
BOT_ROW_FULL = record('BotRowFull', *(BOT_ROW_FIELDS + (F('equipment_states', List(EQUIPMENT_STATE, 3)),)))
BOT_ROSTER_ROW = record('BotRosterRow', F('id', I32), F('team', I32), F('slot', I32), F('name', STR))

# ---- players -----------------------------------------------------------------------
EQUIPMENT_INTENT_RESULT = record('EquipmentIntentResult',
    F('intent_seq', I64), F('accepted', BOOL), F('reason', STR))
GUN_CHECKPOINT = record('GunCheckpoint',
    F('reload_time', F64), F('reload_duration', F64), F('clip', I32), F('clip_size', I32),
    F('dispersion', F64))
RAM_CONTACT = record('RamContact',
    F('seq', I64), F('bot_id', I32), F('bot_state_revision', I64), F('presentation_time_us', I64),
    F('native_contact_time_us', I64), F('contact_x', F64), F('contact_y', F64), F('contact_z', F64),
    F('contact_normal_x', F64), F('contact_normal_z', F64), F('contact_armor_player', F64),
    F('contact_armor_bot', F64), F('contact_screened_player', BOOL), F('contact_screened_bot', BOOL),
    F('contact_spall_player', F64), F('contact_bonus_player', F64), F('x', F64), F('y', F64),
    F('z', F64), F('yaw', F64), F('pitch', F64), F('roll', F64), F('vx', F64), F('vy', F64),
    F('vz', F64), F('bot_vx', F64), F('bot_vy', F64), F('bot_vz', F64),
    O('bot_history_bracket', Tuple(I64, I64, I64, I64)), O('bot_ram_motion_seq', I64),
    O('track_contacts_player', List(STR, 2)), O('track_contacts_bot', List(STR, 2)),
    O('ram_bot_state', Any()), F('input_seq', I64))
DESTRUCTIBLE_TOKEN = Tuple(I64, I64, OneOf(I64, Enum(None)))
DESTRUCTIBLE_CONTACT = record('DestructibleContact',
    F('seq', I64), F('x', F64), F('y', F64), F('z', F64), F('yaw', F64), F('pitch', F64),
    F('roll', F64), F('forward', F64), F('speed', F64), F('dt', F64), F('end_x', F64),
    F('end_y', F64), F('end_z', F64), F('end_yaw', F64), F('token', List(DESTRUCTIBLE_TOKEN, 64)),
    F('input_seq', I64), F('pose_time_us', I64))

PLAYER_ROW_FIELDS = (
    F('id', I64), F('name', STR), F('vehicle', STR), F('vehicle_compact_descr', STR),
    F('team', I32), F('slot', I32), F('requested_team', I32), F('marks_on_gun', I32),
    F('participating', BOOL), F('world_pose', BOOL), F('spawn_x', F64), F('spawn_z', F64),
    F('x', F64), F('y', F64), F('z', F64), F('yaw', F64), F('pitch', F64), F('roll', F64),
    F('up_cosine', F64), F('aim_yaw', F64), F('gun_pitch', F64), F('forward', F64),
    F('turn', F64), F('speed', F64), F('input_seq', I64), F('landing_observation_seq', I64),
    F('input_processed_seq', I64), F('siege_state', I8), F('siege_time_left_ms', I64),
    F('fire_seq', I64), F('shell_index', I32), F('next_shell_index', I32),
    F('shell_change_pending', BOOL), F('health', I64), F('max_health', I64), F('alive', BOOL),
    F('death_reason', I32), F('display_health', I64), F('frags', I64), F('team_killer', BOOL),
    F('death_attacker_kind', ATTACKER_KIND), F('death_attacker_id', I64),
    F('stun_end_server_time_ms', I64), F('stun_factors', STUN_FACTORS),
    F('stun_attacker_kind', ATTACKER_KIND), F('stun_attacker_id', I64),
    F('critical_revision', I64), F('critical_base_revision', I64), F('critical_ack_seq', I64),
    F('equipment_states', List(EQUIPMENT_STATE, 3)), F('equipment_revision', I64),
    F('equipment_intent_seq', I64), F('equipment_intent_result', EQUIPMENT_INTENT_RESULT),
    F('tank_pushes', List(TANK_PUSH, 64)), F('turret_pushes', List(TURRET_PUSH, 64)),
    N('crew_receipt_id', STR), F('ram_contact_admitted_seq', I64),
    F('ram_contact_resolved_seq', I64), F('destructible_contact_admitted_seq', I64),
    F('destructible_contact_resolved_seq', I64), O('gun_checkpoint_seq', I64),
    O('gun_checkpoint', GUN_CHECKPOINT), O('ram_contact', RAM_CONTACT),
    O('ram_contacts', List(RAM_CONTACT, 32)), O('destructible_contacts', List(DESTRUCTIBLE_CONTACT, 16)),
    O('destructible_contact_resolved_seqs', List(I64, 64)),
    O('destructible_contact_rejected_seqs', List(I64, 16)), O('critical', CRITICAL))
PLAYER_ROW = record('PlayerRow', *PLAYER_ROW_FIELDS)
PLAYER_ROW_FULL = record('PlayerRowFull', *(PLAYER_ROW_FIELDS + (
    O('outfits', Map(STR, 3)), O('effective_params', Any()))))

# ---- snapshot ----------------------------------------------------------------------
BASE = record('CaptureBase', F('points', I32), F('time_left', OneOf(F64, I64)), F('invaders', I32),
              F('stopped', BOOL))
RULES = record('Rules', F('bases', Map(BASE, 2)))
BATTLE_RESULT = record('BattleResult', F('winner', I32), F('reason', STR), F('base_team', I32))
DESTRUCTIBLE_FIELDS = (
    F('destructible_kind', Enum('tree', 'column', 'fragile', 'module')), F('chunk_id', I64),
    F('item_index', I64), F('x', F64), F('y', F64), F('z', F64), F('fall_yaw', F64),
    F('speed', F64), F('is_shot', BOOL), O('mat_kind', I64), F('revision', I64),
    F('reported_by', I64))
DESTRUCTIBLE = record('Destructible', F('kind', Enum('destructible')), *DESTRUCTIBLE_FIELDS)
PROBE_BODY = record('RamProbeBody',
    F('id', I64), F('vehicle', STR), F('x', F64), F('y', F64), F('z', F64), F('yaw', F64),
    F('pitch', F64), F('roll', F64), F('shape', Tuple(F64, F64, F64, F64)))
RAM_PROBE = record('RamProbe', F('seq', I64), F('contact_normal', Tuple(F64, F64)),
                   F('first', PROBE_BODY), F('second', PROBE_BODY))
MANIFEST_ROW = record('ManifestRow',
    F('id', I32), F('team', I32), F('slot', I32), F('name', STR), F('vehicle', STR),
    F('max_health', I64), F('health', I64), F('profile', Any()), F('route', Any()),
    O('spg_initial', Any()), F('equipment_contracts', List(EQUIPMENT_CONTRACT, 3)),
    O('skill', STR), O('skill_rating', F64))

SNAPSHOT = record('Snapshot',
    F('protocol', I32), F('server_tick', I64), F('round_id', I64), F('state_revision', I64),
    F('map', STR), N('bot_authority_id', I64),
    F('players', List(OneOf(PLAYER_ROW, PLAYER_ROW_FULL), 30)),
    F('bots', List(OneOf(BOT_ROW, BOT_ROW_FULL, BOT_ROSTER_ROW), 30)),
    F('bot_state_revision', I64), O('bot_manifest', List(OneOf(MANIFEST_ROW, Any()), 30)),
    F('bot_order_revision', I64), O('bot_orders', List(Any(), 30)), F('rules', RULES),
    ON('battle_result', BATTLE_RESULT), F('destructible_revision', I64),
    O('destructibles', List(DESTRUCTIBLE)), O('destructible_base_revision', I64),
    F('timing', TIMING), F('authority_epoch', I64), F('server_time_ms', I64),
    F('motion_time_us', I64), F('bot_state_time_us', I64), F('projectile_revision', I64),
    O('projectiles', List(PROJECTILE, 128)), O('human_ram_probes', List(RAM_PROBE, 64)),
    O('detached_turrets', List(Any(), 30)), *AUTHORITY,
    O('bot_equipment_contracts', List(EQUIPMENT_CONTRACT, 90)), O('phase', STR))

# ---- replica snapshot: each datum at the rate it changes ---------------------------------
# Motion is sent when its values change; a remote actor's state record only when its
# non-countdown fields change; the recipient's own row in full every snapshot.
PLAYER_MOTION_NAMES = ('x', 'y', 'z', 'yaw', 'pitch', 'roll', 'up_cosine', 'aim_yaw',
                       'gun_pitch', 'forward', 'turn', 'speed')
BOT_MOTION_NAMES = ('x', 'y', 'z', 'yaw', 'pitch', 'roll', 'aim_yaw', 'gun_pitch', 'speed',
                    'push_x', 'push_z', 'push_yaw', 'movement_dir', 'rotation_dir', 'airborne',
                    'service_brake')
# Values sampled with a state record; they never make a state record due on their own.
COUNTDOWN_NAMES = ('siege_time_left_ms', 'reload_time', 'burst_time_left', 'combat_fire_elapsed',
                   'combat_fire_timer')
EQUIPMENT_COUNTDOWN_NAMES = ('cooldownTimeLeft', 'autoPendingElapsed', 'aiPendingElapsed')
PLAYER_OWNER_NAMES = (
    'input_seq', 'landing_observation_seq', 'input_processed_seq', 'equipment_intent_seq',
    'equipment_intent_result', 'tank_pushes', 'turret_pushes', 'crew_receipt_id',
    'ram_contact_admitted_seq', 'ram_contact_resolved_seq', 'destructible_contact_admitted_seq',
    'destructible_contact_resolved_seq', 'gun_checkpoint_seq', 'gun_checkpoint', 'ram_contact',
    'ram_contacts', 'destructible_contacts', 'destructible_contact_resolved_seqs',
    'destructible_contact_rejected_seqs')
PLAYER_MOTION = record('PlayerMotion', F('id', I64), *(F(n, F64) for n in PLAYER_MOTION_NAMES))
BOT_MOTION = record('BotMotion', F('id', I32),
                    *(f for f in BOT_ROW_FIELDS if f.name in BOT_MOTION_NAMES))
PLAYER_STATE = record('PlayerState', *(f for f in PLAYER_ROW_FIELDS
                                       if f.name not in PLAYER_MOTION_NAMES + PLAYER_OWNER_NAMES))
BOT_STATE_ROW = record('BotStateRow', *(f for f in R['BotRow'].fields
                                        if f.name not in BOT_MOTION_NAMES))
REPLICA_SNAPSHOT = record('ReplicaSnapshot', *(
    [f for f in R['Snapshot'].fields
     if f.name not in ('players', 'bots', 'bot_equipment_contracts')] +
    [F('full', BOOL), O('own', OneOf(PLAYER_ROW, PLAYER_ROW_FULL)),
     F('player_motion', List(PLAYER_MOTION, 30)), F('bot_motion', List(BOT_MOTION, 30)),
     F('player_states', List(PLAYER_STATE, 30)), F('bot_states', List(BOT_STATE_ROW, 30)),
     F('removed_players', List(I64, 30)), F('removed_bots', List(I32, 30)),
     O('bot_equipment_contracts', List(EQUIPMENT_CONTRACT, 90))]))

LOADING_SNAPSHOT = record('LoadingSnapshot',
    F('protocol', I32), F('client_build', STR), F('server_tick', I64), F('round_id', I64),
    F('map', STR), N('bot_authority_id', I64), F('authority_epoch', I64), F('server_time_ms', I64),
    F('motion_time_us', I64), F('bot_state_time_us', I64),
    F('players', List(OneOf(PLAYER_ROW_FULL, PLAYER_ROW), 30)),
    F('bots', List(OneOf(BOT_ROW_FULL, BOT_ROW, BOT_ROSTER_ROW), 30)), F('bot_state_revision', I64),
    F('projectile_revision', I64), F('projectiles', List(PROJECTILE, 128)),
    F('bot_manifest', List(OneOf(MANIFEST_ROW, Any()), 30)), F('bot_order_revision', I64),
    F('rules', RULES), N('battle_result', BATTLE_RESULT), F('destructible_revision', I64),
    F('destructibles', List(DESTRUCTIBLE)), O('destructible_base_revision', I64),
    O('bot_orders', List(Any(), 30)), F('detached_turrets', List(Any(), 30)), *AUTHORITY)

# ---- events ------------------------------------------------------------------------
EVENT_ID = F('event_id', STR)
HIT_FIELDS = (
    EVENT_ID, O('attacker', I64), O('attacker_bot', I64), O('target', I64), O('target_bot', I64),
    F('damage', I64), F('health', I64), F('dead', BOOL), ON('attack_reason', I32),
    F('death_reason', I32), F('source', STR), O('projectile_id', STR), O('shot_seq', I64),
    O('shell_index', I32), O('shot_result', I32), O('blocked_damage', I64), O('splash', BOOL),
    O('world_pose', BOOL), O('x', F64), O('y', F64), O('z', F64), O('damage_sticker', U64),
    O('critical_accepted', BOOL), O('critical', CRITICAL), O('critical_reject_reason', STR),
    O('critical_revision', I64), O('critical_base_revision', I64), O('critical_ack_seq', I64),
    O('combat_revision', I64), O('combat_base_revision', I64), O('combat_ack_seq', I64),
    O('cosmetic_only', BOOL), O('ram_operation_id', STR), O('ram_frontier_time_us', I64),
    O('push_attributed', BOOL), O('critical_feedback_separate', BOOL), O('display_health', I64))
for _kind in ('health', 'hit', 'bot_hit', 'bot_human_hit', 'bot_bot_hit'):
    R['Event_' + _kind] = Record('Event_' + _kind, list(HIT_FIELDS))
SHOT_FIELDS = (EVENT_ID,) + PROJECTILE_FIELDS + (
    F('maxDistance', F64), F('shot_yaw', F64), F('shot_pitch', F64), O('attacker', I64),
    O('attacker_bot', I64), O('bot_launch_time_us', I64))
R['Event_shot'] = Record('Event_shot', list(SHOT_FIELDS))
R['Event_bot_shot'] = Record('Event_bot_shot', list(SHOT_FIELDS))
R['Event_projectile_ricochet'] = Record('Event_projectile_ricochet', list((EVENT_ID,) + PROJECTILE_FIELDS + (
    F('resolved_time_ms', I64), F('impact', VEC3), F('direct', Any()))))
R['Event_projectile_impact'] = Record('Event_projectile_impact', [
    EVENT_ID, F('projectile_id', STR), F('outcome', Enum('impact', 'miss', 'expired')),
    F('resolved_time_ms', I64), F('checked_distance', F64), F('piercing_loss', F64),
    F('penetration_factor', F64), F('hit_vehicle', BOOL), F('shooter_kind', Enum('player', 'bot')),
    F('shooter_id', I64), F('shot_seq', I64), O('impact', VEC3),
    O('wreck_hit', Any())])
R['Event_authority'] = Record('Event_authority', [
    EVENT_ID, N('player_id', I64), F('round_id', I64), F('authority_epoch', I64)])
R['Event_bot_manifest'] = Record('Event_bot_manifest', [EVENT_ID, F('bots', List(MANIFEST_ROW, 30))])
R['Event_destructible'] = Record('Event_destructible', [EVENT_ID] + list(DESTRUCTIBLE_FIELDS))
R['Event_stun'] = Record('Event_stun', [
    EVENT_ID, F('active', BOOL), F('target_kind', Enum('player', 'bot')), F('target_id', I64),
    F('stun_end_server_time_ms', I64), O('attacker_kind', Enum('player', 'bot')),
    O('attacker_id', I64), O('stun_factors', STUN_FACTORS)])
R['Event_environment_critical'] = Record('Event_environment_critical', [
    EVENT_ID, F('source', STR), F('push_attributed', BOOL), F('cause', STR),
    F('critical_mask', I64), F('critical_count', I32), O('attacker', I64), O('attacker_bot', I64),
    O('target', I64), O('target_bot', I64)])
R['Event_vehicle_statistics'] = Record('Event_vehicle_statistics', [
    EVENT_ID, F('actor_kind', Enum('player', 'bot')), F('actor_id', I64), F('frags', I64),
    F('team_killer', BOOL)])
R['Event_detection'] = Record('Event_detection', [
    EVENT_ID, F('observer_kind', Enum('player', 'bot')), F('observer_id', I64),
    F('target_kind', Enum('player', 'bot')), F('target_id', I64)])
R['Event_assist'] = Record('Event_assist', [
    EVENT_ID, F('category', Enum('track', 'stun', 'radio')),
    F('assister_kind', Enum('player', 'bot')), F('assister_id', I64),
    F('attacker_kind', Enum('player', 'bot')), F('attacker_id', I64),
    F('target_kind', Enum('player', 'bot')), F('target_id', I64), F('damage', I64)])
R['Event_battle_result'] = Record('Event_battle_result', [
    EVENT_ID, F('winner', I32), F('reason', STR), F('base_team', I32)])
EVENT_KINDS = ('authority', 'bot_manifest', 'destructible', 'health', 'hit', 'bot_hit',
               'bot_human_hit', 'bot_bot_hit', 'shot', 'bot_shot', 'projectile_ricochet',
               'projectile_impact', 'stun', 'environment_critical', 'vehicle_statistics',
               'detection', 'assist', 'battle_result')
EVENT = Variant('kind', dict((k, 'Event_' + k) for k in EVENT_KINDS))
EVENT.order = list(EVENT_KINDS)

EVENTS = record('Events',
    O('protocol', I32), F('round_id', I64), F('server_tick', I64), F('events', List(EVENT, 256)),
    F('authority_epoch', I64), F('server_time_ms', I64), *AUTHORITY, ON('bot_authority_id', I64))

# ---- client and worker -> server ---------------------------------------------------
INPUT_RAM_CONTACT = record('InputRamContact', *[f for f in R['RamContact'].fields
                                                 if f.name not in ('input_seq', 'ram_bot_state')])
INPUT_DESTRUCTIBLE_CONTACT = record('InputDestructibleContact',
    F('seq', I64), F('x', F64), F('y', F64), F('z', F64), F('yaw', F64), O('pitch', F64),
    O('roll', F64), O('forward', F64), F('speed', F64), F('dt', F64), F('end_x', F64),
    F('end_y', F64), F('end_z', F64), F('end_yaw', F64), F('token', List(DESTRUCTIBLE_TOKEN, 64)))
INPUT = record('Input',
    F('round_id', I64), O('input_seq', I64), O('forward', F64), O('turn', F64), O('speed', F64),
    O('aim_yaw', F64), O('gun_pitch', F64), O('fire_seq', I64), O('x', F64), O('y', F64),
    O('z', F64), O('yaw', F64), O('pitch', F64), O('roll', F64), O('up_cosine', F64),
    O('pose_time_us', I64), O('shell_index', I32), O('next_shell_index', I32),
    O('shell_change_pending', BOOL), O('gun_checkpoint', GUN_CHECKPOINT), O('siege_enabled', BOOL),
    O('tank_pushes', List(TANK_PUSH, 30)), O('turret_pushes', List(TURRET_PUSH, 30)),
    O('ram_contacts', List(INPUT_RAM_CONTACT, 32)),
    O('destructible_contacts', List(INPUT_DESTRUCTIBLE_CONTACT, 16)))
TRACK = record('TrackRepairRow', F('name', STR), F('hp', F64), F('max_hp', F64), F('state', STR))
TRACK_REPAIR = record('TrackRepair', F('round_id', I64), F('critical_base_revision', I64),
                      F('repair_seq', I64), F('tracks', List(TRACK, 2)))
EQUIPMENT_INTENT = record('EquipmentIntent', F('round_id', I64), F('intent_seq', I64),
    F('equipment_id', I64), F('activation_code', I64), N('selected', STR),
    N('requested_active', BOOL))
LANDING = record('LandingObservation', F('round_id', I64), F('authority_epoch', I64),
    F('observation_seq', I64), F('input_seq', I64), F('impact_speed', F64),
    O('module_contacts', List(Any(), 16)))
RECEIPT = record('DestructibleReceipt', *(f for f in DESTRUCTIBLE_FIELDS
                                         if f.name not in ('revision', 'reported_by')))
LAUNCH_PROOF = Any()
PROJECTILE_LAUNCH = record('ProjectileLaunch',
    F('round_id', I64), F('authority_epoch', I64), F('shooter_kind', Enum('player', 'bot')),
    F('shooter_id', I64), F('shot_seq', I64), O('burst_group_seq', I64), O('burst_index', I32),
    O('burst_count', I32), F('shell_index', I32), F('origin', VEC3), F('velocity', VEC3),
    F('gravity', F64), F('max_distance', F64), F('max_time_ms', I64), F('is_he', BOOL),
    F('splash_radius', F64), F('penetration_factor', F64), F('source_shot', SOURCE_SHOT),
    O('fire_intent_seq', I64), O('fire_input_seq', I64), O('range_origin', VEC3),
    O('launch_server_time_ms', I64), O('launch_time_us', I64),
    O('launch_pose', Tuple(F64, F64, F64, F64, F64, F64)), O('shells_before_shot', I64),
    O('client_build', STR))
CURSOR = record('ProjectileCursor',
    F('projectile_id', STR), F('base_checked_ms', I64), F('checked_through_ms', I64),
    F('checked_distance', F64), F('piercing_loss', F64), F('penetration_factor', F64),
    F('destructibles', List(RECEIPT, 64)), ON('launch_proof', LAUNCH_PROOF))
PROJECTILE_PROGRESS = record('ProjectileProgress', F('round_id', I64), F('authority_epoch', I64),
    F('cursors', List(CURSOR, 30)), O('client_build', STR))
EFFECT = Any()
PROJECTILE_RICOCHET = record('ProjectileRicochet', F('round_id', I64), F('authority_epoch', I64),
    F('projectile_id', STR), F('base_checked_ms', I64), F('resolved_time_ms', I64),
    F('checked_distance', F64), F('piercing_loss', F64), F('penetration_factor', F64),
    F('impact', VEC3), F('segment_origin', VEC3), F('segment_velocity', VEC3),
    F('base_penetration_multiplier', F64), F('direct', EFFECT),
    F('destructibles', List(RECEIPT, 64)), ON('launch_proof', LAUNCH_PROOF), O('client_build', STR))
PROJECTILE_RESOLVE = record('ProjectileResolve', F('round_id', I64), F('authority_epoch', I64),
    F('projectile_id', STR), F('base_checked_ms', I64), F('outcome', Enum('impact', 'miss', 'expired')),
    F('resolved_time_ms', I64), F('checked_distance', F64), F('piercing_loss', F64),
    F('penetration_factor', F64), N('impact', VEC3), N('direct', EFFECT),
    F('splash', List(EFFECT, 30)), F('destructibles', List(RECEIPT, 64)), O('hit_vehicle', BOOL),
    O('wreck_hit', Any()), ON('launch_proof', LAUNCH_PROOF), O('client_build', STR))
# Post-result detached-turret tails have no motion sample or source horizon.
BOT_STATE = record('BotState', F('round_id', I64), O('authority_epoch', I64),
    O('sample_time_us', I64), O('source_batch_horizon_us', I64), F('rows', List(List(I64, 1024), 30)),
    O('human_ram_armors', List(Any(), 64)), O('push_contacts', Any()),
    O('detached_turrets', List(Any(), 30)), O('bot_state_revision', I64))

# ---- message table: None = tagged binary body (low-frequency control) -----------------
MESSAGES = {
    'snapshot': ('Snapshot', 'LoadingSnapshot'),
    'replica_snapshot': 'ReplicaSnapshot',
    'events': 'Events',
    'input': 'Input',
    'track_repair': 'TrackRepair',
    'equipment_intent': 'EquipmentIntent',
    'landing_observation': 'LandingObservation',
    'projectile_launch': 'ProjectileLaunch',
    'projectile_progress': 'ProjectileProgress',
    'projectile_ricochet': 'ProjectileRicochet',
    'projectile_resolve': 'ProjectileResolve',
    'bot_state': 'BotState',
}
for _name in ('battle_live', 'battle_start', 'roster', 'welcome', 'pong', 'worker_ping',
              'worker_pong', 'fire_intent_result', 'landing_observation_result',
              'player_destructible_contact', 'player_destructible_contact_result',
              'bot_observation', 'team_command', 'team_command_ack', 'team_command_terminal',
              'team_chat', 'team_chat_ack', 'battle_receipt', 'error', 'start_denied',
              'team_denied', 'team_size_denied', 'bot_tier_mode_denied', 'bot_skill_mode_denied',
              'hello', 'battle_ready', 'ping', 'snapshot_resync', 'leave_battle', 'leave',
              'battle_receipt_ack', 'bot_manifest', 'bot_ram_report', 'simulation_progress',
              'player_environment', 'destructible', 'rules_state', 'battle_result',
              'descriptor_catalog', 'start_battle',
              'select_vehicle', 'select_team', 'set_team_size',
              'set_bot_tier_mode', 'set_bot_skill_mode'):
    MESSAGES[_name] = None

SCHEMA = Schema(R, MESSAGES)
