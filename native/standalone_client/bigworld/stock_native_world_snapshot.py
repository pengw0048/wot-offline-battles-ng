"""Freeze the installed #1513 resources once at native world installation.

No function in this module is called from native queries or Bot steps.
"""
from __future__ import print_function

import math


_KINDS = {'tree': 0, 'falling': 1, 'fragile': 2, 'structure': 3}


def _local_boxes(record, row):
    boxes = record.get('boxes', ())
    if row['kind'] != 'structure':
        index = row.get('box_index')
        boxes = (boxes[index],) if index is not None else boxes
    result = []
    for box in boxes:
        minimum, maximum = box[:3], box[3:6]
        center = tuple((float(a) + float(b)) * .5
                       for a, b in zip(minimum, maximum))
        half = tuple((float(b) - float(a)) * .5
                     for a, b in zip(minimum, maximum))
        result.append((center, ((half[0], 0., 0.), (0., half[1], 0.),
                                (0., 0., half[2])), box[6]))
    return tuple(result)


def _dependencies(value):
    if not isinstance(value, dict):
        raise ValueError('Native structure dependency table is not a dictionary')
    return tuple((int(key), tuple(int(item) for item in values))
                 for key, values in sorted(value.items()))


def build(sensor):
    import AreaDestructibles
    import DestructiblesCache
    from gui.mods.offline_lan_0922 import destructibles_authority

    catalog = getattr(sensor, '_destructible_catalog', None) or {}
    available = bool(catalog)
    cache = AreaDestructibles.g_cache
    if cache is None:
        raise RuntimeError('Native world installation requires the #1513 resource cache')
    quantization = int(catalog.get('quantization', 1000))
    rows = dict(catalog.get('baked_instances', {}))
    rows.update(catalog.get('tree_instances', {}))
    live = getattr(sensor, 'g_offh_destr_instances', {})
    for key, row in live.items():
        if key not in rows:
            rows[key] = row
    descriptors = {}
    output = []
    for key, baked in sorted(rows.items()):
        kind = baked['kind']
        if kind not in _KINDS:
            raise ValueError('Native world instance has an invalid kind')
        filename = baked.get('descriptor_filename', baked['filename'])
        if filename not in descriptors:
            try:
                descriptors[filename] = cache.getDescByFilename(filename)
            except Exception:
                descriptors[filename] = None
        descriptor = descriptors[filename]
        expected = _KINDS[kind]
        valid = isinstance(descriptor, dict) and descriptor.get('type') == expected
        record = catalog.get('resources', {}).get(baked['filename'], {})
        signature = baked.get('signature')
        if not isinstance(signature, (tuple, list)) or len(signature) != 12:
            raise ValueError('Native world instance lacks its exact locator')
        signature = tuple(int(value) for value in signature)
        item_scale = float(baked.get('item_scale', 0.))
        if kind == 'tree':
            item_scale = math.sqrt(sum((float(value) / quantization) ** 2
                                       for value in signature[6:9]))
        scaled, raw = [], []
        dependencies, inverse = (), ()
        correction, tree_health = 0., 0.
        parameters = None
        fracture, touchdown = None, None
        try:
            if valid:
                if kind == 'structure':
                    modules = descriptor.get('modules')
                    valid = isinstance(modules, dict) and bool(modules)
                    if valid:
                        for material, module in sorted(modules.items()):
                            health = float(module['health'])
                            raw.append((int(material), health))
                            scaled.append((int(material), float(
                                DestructiblesCache.scaledDestructibleHealth(item_scale, health))))
                        dependencies = _dependencies(descriptor.get('destroyDepends') or {})
                        inverse = _dependencies(descriptor.get('inversedDestroyDepends') or {})
                elif kind == 'tree':
                    tree_health = float(descriptor['health'])
                    parameters = tuple(float(descriptor[name]) for name in (
                        'height', 'mass', 'springAngle', 'springStiffnes',
                        'springResist', 'airResist', 'buryDepth'))
                    fracture = descriptor.get('fractureEffect')
                    touchdown = descriptor.get('touchdownEffect')
                else:
                    health = float(descriptor['health'])
                    raw = [(-1, health)]
                    scaled = [(-1, float(DestructiblesCache.scaledDestructibleHealth(
                        item_scale, health)))]
                    correction = float(descriptor['kineticDamageCorrection'])
        except (KeyError, TypeError, ValueError, OverflowError, ZeroDivisionError):
            valid = False
            scaled, raw, dependencies, inverse = [], [], (), ()
            correction, tree_health, parameters, fracture, touchdown = 0., 0., None, None, None
        # A contradictory descriptor cannot grant native admission. Keep the
        # baked placement so native unresolved geometry can still be proved.
        numeric = [correction, tree_health] + [value for unused, value in scaled + raw]
        if parameters is not None:
            numeric.extend(parameters)
        if any(math.isnan(value) or math.isinf(value) for value in numeric):
            valid = False
            scaled, raw, dependencies, inverse = [], [], (), ()
            correction, tree_health, parameters, fracture, touchdown = 0., 0., None, None, None
        retained = False
        if record.get('retained_collision_boxes'):
            if kind == 'structure':
                retained = True
            else:
                retained = baked.get('box_index', 0 if len(record.get('boxes', ())) == 1 else None) in record['retained_collision_boxes']
        output.append(dict(key=tuple(int(value) for value in key), kind=expected,
            filename=filename, boxes=tuple(baked.get('boxes', ())),
            local_boxes=_local_boxes(record, baked) if kind != 'tree' else (),
            origin=tuple(float(value) / quantization for value in signature[:3]),
            signature=signature, quantization=float(quantization),
            ambiguous=kind != 'tree' and signature in catalog.get('ambiguous_instances', ()),
            item_scale=item_scale, scaled_health=tuple(scaled), reference_health=tuple(raw),
            broad_phase_margin=float(baked.get('broad_phase_margin', 0.)),
            kinetic_correction=correction, tree_health=tree_health,
            descriptor_valid=bool(valid), retained_collision=bool(retained),
            dependencies=dependencies, inverse_dependencies=inverse,
            tree_parameters=parameters, fracture_variants=fracture,
            touchdown_variants=touchdown))
    authority = getattr(destructibles_authority, '_state', {})
    broken = tuple((int(chunk), int(item), material)
                   for chunk, state in sorted(authority.get('chunks', {}).items())
                   for item, material in sorted(state.get('keys', ()),
                       key=lambda row: (row[0], -1 if row[1] is None else row[1])))
    return dict(catalog_available=available,
        unit_vehicle_mass=float(cache.unitVehicleMass), instances=tuple(output),
        broken=broken,
        speculative=tuple(getattr(sensor, 'g_offh_destr_speculative', {})),
        isolated_chunks=tuple(getattr(sensor, 'g_offh_destr_isolated_chunks', ())),
        isolated_slots=tuple(getattr(sensor, 'g_offh_destr_isolated_slots', ())),
        excluded_instances=tuple(catalog.get('excluded_instances', ())),
        layout_repairs=tuple(catalog.get('layout_repairs', ())),
        layout_generations=tuple(sorted(catalog.get('layout_generations', {}).items())))
