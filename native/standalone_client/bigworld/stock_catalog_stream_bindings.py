"""Avoid unused contact sorting when every baked candidate is registered."""

_START = 'def _stream_baked_motion_instances_1513(spaceID, vehicle_box):\n'
_END = '\n\ndef _vehicle_pose_axes('
_OLD = '''\tcache = globals().get('g_offh_destr_unresolved_obstacles')
\tdef contact_priority(identity):
'''
_NEW = '''\tcache = globals().get('g_offh_destr_unresolved_obstacles')
\t# Registered instances are checked by the live catalog contact query below.
\t# Their baked intersection keys are never consumed by this admission pass.
\t# Keep the original mixed path: a missing item's repair may invalidate an
\t# existing instance before that instance's turn in the original ordering.
\tif (type(identities) in (tuple, list, set) and type(instances) is dict and
\t\t\t(cache is None or type(cache) is dict) and
\t\t\tall(identity in instances for identity in identities)):
\t\tfor identity in identities:
\t\t\tcombat_count('destructible_stream_candidates')
\t\t\tif cache is not None:
\t\t\t\tcache.pop(identity, None)
\t\treturn ()
\tdef contact_priority(identity):
'''


def sensor(source):
    """Adapt only the prepared copy, rejecting source drift and partial edits."""
    if source.count(_START) != 1 or source.count(_END) != 1:
        raise ValueError('Pinned catalog stream function boundary changed')
    start = source.index(_START)
    end = source.index(_END, start)
    body = source[start:end]
    if body.count(_NEW) == 1 and body.count(_OLD) == 0:
        return source
    if body.count(_OLD) != 1 or body.count(_NEW) != 0:
        raise ValueError('Pinned catalog stream admission changed')
    return source[:start] + body.replace(_OLD, _NEW, 1) + source[end:]
