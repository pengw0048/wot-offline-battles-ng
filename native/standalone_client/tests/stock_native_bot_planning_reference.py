"""Freeze static parking candidates once before the first native Bot step."""
import math


def freeze_parking(runtime):
    from gui.mods.offline_lan_0922 import bot_tactics as config
    from gui.mods.offline_lan_0922 import bot_tactics_runtime as manual
    from gui.mods.offline_lan_0922 import spg_positions as sourced
    graph = runtime.baked_graph
    name = graph['map']
    states = list(runtime.states.values())
    profile = runtime._bot_tactics
    settings = config.map_settings(profile, name)
    deleted = set(settings.get('deleted_positions', ()))
    result = dict(directions=tuple(graph['directions']), actors={})
    for state in states:
        initial = state.get('_spg_initial')
        if initial is None:
            continue
        is_manual = initial['source'] == 'launcher_manual_v1'
        clearance = math.hypot(*state['collision_shape'][:2]) + (2.0 if is_manual else 2.5)
        catalog = dict(manual=is_manual, candidates=[], areas={})
        if is_manual:
            grid = manual.graph_view(name, graph)
            identity = config.digest(profile)
            for zone in settings.get('positions', ()):
                if zone['id'] in deleted or zone['team'] != state['team']:
                    continue
                catalog['areas'][zone['id']] = dict(circle=True,
                    bounds=(zone['point'][0], zone['point'][1], 0.0, 0.0),
                    radius=zone['radius'])
                for centre, index, point in manual._manual_candidates(grid, zone, clearance):
                    angle = math.radians(zone['heading'])
                    bounds = config.MAPS[name]['bounds']
                    face = (max(bounds[0], min(bounds[2], point[0] + math.sin(angle)*100)),
                            point[1], max(bounds[1], min(bounds[3], point[2] + math.cos(angle)*100)))
                    plan = dict(catalog=identity, map=name, side='team%d' % state['team'],
                        zone=zone['id'], cell='manual', source='launcher_manual_v1',
                        vehicle=state['vehicle'], point=point, face=face, radius=2.0, clearance=clearance)
                    catalog['candidates'].append((plan, index, zone['priority'], centre))
        else:
            data = sourced.CATALOG
            entry = data['maps'][name]
            grid = sourced._Graph(graph, entry['bounds'])
            side = sourced.side_for_team(state['team'], graph['bases'])
            for zone in entry['zones']:
                for cell in zone['cells']:
                    catalog['areas'][cell] = dict(circle=False,
                        bounds=sourced.cell_bounds(cell, entry['bounds']), radius=0.0)
            if side is not None and entry['spawn_sides'].get(str(state['team'])) == side:
                for zone, cell, index, point in grid.candidates(entry, side, clearance):
                    if ('spg_%d_%s' % (state['team'], zone['id']) in deleted or
                            not sourced._has_initial_support(zone, state['team'], states, entry['bounds'])):
                        continue
                    enemy = graph['bases'][2-state['team']]
                    face = (float(enemy[0]), point[1], float(enemy[1]))
                    if zone.get('face_cell'):
                        left, bottom, right, top = sourced.cell_bounds(zone['face_cell'], entry['bounds'])
                        face = ((left+right)*0.5, point[1], (bottom+top)*0.5)
                    plan = dict(catalog=data['revision'], map=name, side=side,
                        zone=zone['id'], cell=cell, source=zone['source'], vehicle=state['vehicle'],
                        point=point, face=face, radius=2.0, clearance=clearance)
                    catalog['candidates'].append((plan, index, zone.get('priority', 0), 0.0))
        result['actors'][state['id']] = catalog
    return result


def freeze_perception(runtime, state, descriptor):
    """Copy undamaged Bot coefficients for one installed vehicle mode.

    Vision and signal critical factors remain live native state. Deriving the
    clean coefficient directly also handles a fully destroyed module, whose
    current projected value cannot be inverted to recover its base.
    """
    from gui.mods.offline_lan_0922 import bot_runtime as laws
    crew = runtime.bot_crew_level(state['id'])
    profile = laws._bot_profile(descriptor, crew)
    base = laws._base_invisibility(descriptor, profile)
    shot = laws._shot_invisibility_factor(descriptor)
    moving, still, delay = laws._vision_range_pair(descriptor, crew)
    moving_aspect, still_aspect = profile['invisibility_moving'], profile['invisibility_still']
    radio = laws.loadout.modifiers(descriptor,
        factors=laws._bot_default_crew_factors(descriptor, crew))['radio_factor']
    radio *= laws._number(laws._value(laws._value(descriptor, 'radio', {}), 'distance', 0.0))
    values = (int(state.get('team', 0)), int(state.get('slot', 0)), 3.5, 1.7,
        float(moving), float(still), -1.0 if delay is None else float(delay),
        float(base[1]), float(base[0]), float(moving_aspect[0]),
        float(still_aspect[0] - moving_aspect[0]), float(moving_aspect[1]),
        float(still_aspect[1]), float(shot),
        float(profile['camouflage_net_delay']) if profile['has_camouflage_net'] else -1.0,
        max(0.0, radio), 0.0, 0.0)
    return values, False, False
