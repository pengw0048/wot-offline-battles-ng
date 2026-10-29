"""Remove unused hull preparation from the fixed registration-only consumer."""

_START = 'def _fell_trees_near(\n'
_END = '\n\ndef _solid_destructible_candidate_1513('
_OLD = '''\t\tbbox = ((-1.6, -1.0, -3.6), (1.6, 1.0, 3.6), None)
\t\tbbox = _vehicle_hull_bbox(td)
\t\tif bbox is None:
\t\t\tbbox = ((-1.6, -1.0, -3.6), (1.6, 1.0, 3.6), None)
\t\ttry:
\t\t\thw = max(abs(bbox[0][0]), abs(bbox[1][0]))
\t\t\thl_b = abs(bbox[0][2])
\t\t\thl_f = abs(bbox[1][2])
\t\texcept (AttributeError, KeyError, TypeError, IndexError):
\t\t\traise RuntimeError('#1513 hull hit tester bbox is invalid')
\t\tvehicle_box = _vehicle_swept_box(pos, yaw, vel, bbox)
\t\t_current_cid = AreaDestructibles.chunkIDFromPosition(
\t\t\tMath.Vector3(pos.x, pos.y, pos.z))
\t\t_receipt_key = None
\t\tif _current_cid is not None and _destructible_catalog is not None:
\t\t\t_receipt_key = _proximity_receipt_key_1513(
\t\t\t\tspaceID, _current_cid, pos, vehicle_box)
\t\t\tif (not registration_only and
\t\t\t\t\t_empty_proximity_receipt_valid_1513(
\t\t\t\t\t_receipt_key, mgr, _st['chunks'])):
\t\t\t\treturn
'''
_NEW = '''\t\tif not registration_only:
''' + ''.join('\t' + line for line in _OLD.splitlines(True)[:11]) + '''\t\t_current_cid = AreaDestructibles.chunkIDFromPosition(
\t\t\tMath.Vector3(pos.x, pos.y, pos.z))
\t\t_receipt_key = None
\t\tif (not registration_only and _current_cid is not None and
\t\t\t\t_destructible_catalog is not None):
\t\t\t_receipt_key = _proximity_receipt_key_1513(
\t\t\t\tspaceID, _current_cid, pos, vehicle_box)
\t\t\tif _empty_proximity_receipt_valid_1513(
\t\t\t\t\t_receipt_key, mgr, _st['chunks']):
\t\t\t\treturn
'''


def sensor(source):
    """Patch the pinned consumer text, rejecting drift and partial patches."""
    if source.count(_START) != 1 or source.count(_END) != 1:
        raise ValueError('Pinned tree registration function boundary changed')
    start = source.index(_START)
    end = source.index(_END, start)
    body = source[start:end]
    if body.count(_NEW) == 1 and body.count(_OLD) == 0:
        return source
    if body.count(_OLD) != 1 or body.count(_NEW) != 0:
        raise ValueError('Pinned tree registration preparation changed')
    return source[:start] + body.replace(_OLD, _NEW, 1) + source[end:]
