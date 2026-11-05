"""Freeze pinned room constants as data; never shipped as server code."""
import argparse
import json
from pathlib import Path
import sys
import unicodedata


def codepoint_ranges(predicate):
    result = []
    for value in range(0x110000):
        if not predicate(chr(value)):
            continue
        if result and result[-1][1] + 1 == value:
            result[-1][1] = value
        else:
            result.append([value, value])
    return result


def bake(source, output):
    source, output = Path(source), Path(output)
    sys.path[:0] = [str(source / 'server'), str(source / 'src/res/scripts/client'),
                   str(Path(__file__).resolve().parents[1] / 'wire')]
    import lan_battle_server as server
    from gui.mods.offline_lan_0922 import bot_tactics, spg_positions
    from gui.mods.offline_lan_0922.ai.maps import TACTICAL_MAPS
    from gui.mods.offline_lan_0922.bot_fitting_data import CLASSES, PLANS
    from gui.mods.offline_lan_0922.vehicle_configuration import RETIRED_BOT_VEHICLES
    from schema import R

    # Only constructor defaults and immutable map/tactics data are exported.
    # No BattleState, simulation, networking, or round result is instantiated.
    player = server.Player(player_id=1, conn=None, address=('127.0.0.1', 0))
    player_defaults = server.BattleState._public_player(player)
    profile = bot_tactics.default_profile()
    active_profile = bot_tactics.empty()
    spg_positions.validate_catalog(spg_positions.CATALOG)
    value = {
        'schema': 1,
        'gameplay_revision': 'abd03c4a91366f52ff6cb81d6b8b56539ea3c5c0',
        'protocol': server.PROTOCOL_VERSION,
        'client_build': server.CLIENT_BUILD_0922,
        'required_capabilities': server.MODERN_CLIENT_REQUIRED_CAPABILITIES,
        'server_capabilities': server.SERVER_CAPABILITIES,
        'public_player_defaults': player_defaults,
        'public_bot_defaults': server.BattleState._sanitize_bot_state(
            {}, {'id': 1, 'team': 1, 'slot': 0, 'name': 'Bot'}, None),
        'unicode_alnum_ranges': codepoint_ranges(str.isalnum),
        'unicode_cased_ranges': codepoint_ranges(lambda c: c.islower() or c.isupper() or c.istitle()),
        'unicode_case_ignorable_ranges': codepoint_ranges(
            lambda c: unicodedata.category(c) in ('Mn', 'Me', 'Cf', 'Lm', 'Sk')),
        'unicode_lower': {str(codepoint): chr(codepoint).lower()
                          for codepoint in range(0x110000)
                          if chr(codepoint).lower() != chr(codepoint)},
        'unicode_whitespace': [codepoint for codepoint in range(0x110000)
                               if chr(codepoint).isspace()],
        'unicode_decimal': {str(codepoint): unicodedata.decimal(chr(codepoint))
                            for codepoint in range(0x110000) if chr(codepoint).isdecimal()},
        'wire_records': {name: [field.name for field in R[name].fields]
                         for name in ('PlayerRow', 'PlayerRowFull', 'BotRow', 'BotRowFull')},
        'lobby': {
            'map_pool': server.MAP_POOL,
            'callsigns': server.BOT_CALLSIGNS_0922,
            'team_classes': bot_tactics.team_class_choices(),
            'default_profile': active_profile,
            'authored_default_profile': profile,
            'vehicle_classes': CLASSES,
            'retired_bot_vehicles': sorted(RETIRED_BOT_VEHICLES),
            'classes': bot_tactics.CLASSES,
            'skill_tiers': server.bot_gunnery.SKILL_TIERS,
            'skill_modes': server.bot_gunnery.SKILL_MODES,
            'tier_modes': server.bot_planner.BOT_TIER_MODES,
        },
        'spg_catalog': spg_positions.CATALOG,
        'siege_vehicle_params': server.SIEGE_VEHICLE_PARAMS,
        'bot_fitting_plans': PLANS,
        'tactics_value_parameters': bot_tactics.PARAMETERS,
        'tactics_value_presets': bot_tactics.PRESETS,
        'tactics_skills': bot_tactics.SKILLS,
        'maps': {name: {'tactics': bot_tactics.for_round(active_profile, name),
                        'default_tactics': bot_tactics.for_round(profile, name),
                        'default_tactics_digest': bot_tactics.digest(bot_tactics.for_round(profile, name)),
                        'tactics_digest': bot_tactics.digest(bot_tactics.for_round(active_profile, name)),
                        'tactical_map': TACTICAL_MAPS.get(name)}
                 for name in server.MAP_POOL},
        'timing': {
            'tick_hz': server.TICK_HZ,
            'prebattle_seconds': server.PREBATTLE_SECONDS,
            'battle_duration_seconds': server.BATTLE_DURATION_SECONDS,
            'bot_planner_interval_ticks': server.BOT_PLANNER_INTERVAL_TICKS,
            'replica_snapshot_ticks': server.REPLICA_SNAPSHOT_TICKS,
            'bot_manifest_refresh_ticks': server.BOT_MANIFEST_REFRESH_TICKS,
            'bot_order_refresh_ticks': server.BOT_ORDER_REFRESH_TICKS,
            'destructible_refresh_ticks': server.DESTRUCTIBLE_REFRESH_TICKS,
            'result_reset_seconds': server.RESULT_RESET_SECONDS,
        },
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(value, ensure_ascii=True, allow_nan=False,
                                sort_keys=True, separators=(',', ':')) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    parser.add_argument('output')
    args = parser.parse_args()
    bake(args.source, args.output)
