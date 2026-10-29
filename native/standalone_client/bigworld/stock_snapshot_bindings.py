"""Adapt the exact pinned decoder and canonical-input consumer together."""


def _replace(source, before, after):
    if source.count(before) != 1:
        raise ValueError('The pinned snapshot ownership seam changed: ' + before)
    return source.replace(before, after, 1)


def delta(source):
    source = _replace(source, 'import math\n', '''import math

from gui.mods.offline_lan_0922 import stock_snapshot_static as _static
''')
    # Keep the original scalar/container fast paths. Only owned subclasses
    # reach this fallback; ordinary dynamic scalar fields pay no extra check.
    source = _replace(source,
        "    raise SnapshotDeltaError('unsupported snapshot JSON value')\n",
        "    if kind is _static._StaticDict or kind is _static._StaticList:\n"
        "        return _static.detach(value)\n"
        "    raise SnapshotDeltaError('unsupported snapshot JSON value')\n")
    source = _replace(source,
        '        return dict((name, _clone(values)) for name, values in\n',
        '        return dict((name, _static.detach_rows(values, _clone)) for name, values in\n')
    source = _replace(source,
        '        actors[name] = _clone(message[name])\n',
        '        actors[name] = _static.own_rows(_clone(message[name]))\n')
    source = _replace(source, '    return rows\n\n\ndef decode(',
        '    return _static.own_rows(rows)\n\n\ndef decode(')
    return source


def client(source):
    source = _replace(source, 'def _same_canonical_source(left, right):', '''from gui.mods.offline_lan_0922 import stock_snapshot_static as _snapshot_static


def _same_canonical_source(left, right):''')
    source = _replace(source, '''                if (not _same_canonical_source(cached_source, source) or
                        outfits is None):
                    outfits = _canonical_wire_outfits(source)
''', '''                outfits = _snapshot_static.canonical(
                    source, cached_source, outfits, _canonical_wire_outfits,
                    _same_canonical_source)
''')
    source = _replace(source, '''                if (not _same_canonical_source(cached_source, source) or
                        params is None):
                    params = _canonical_effective_params(source)
''', '''                params = _snapshot_static.canonical(
                    source, cached_source, params, _canonical_effective_params,
                    _same_canonical_source)
''')
    return source
