"""Read-only preflight and child-environment isolation for offline replays.

Executed by the launcher session thread, never a Tk or game render callback.
This reads the native tagged/gzip stream without issuing game commands.
"""
from __future__ import annotations
import gzip
import importlib
import math
import os
import struct
import time

FILE_ENV = 'WOT_OFFLINE_REPLAY_FILE'
AUTO_ENV = 'WOT_OFFLINE_REPLAY_AUTOSTART'
MAGIC = b'WOTLANR2'
RECORD_HEADER = struct.Struct('<BdI')
MAX_RECORD = 16 * 1024 * 1024
MAX_TOTAL = 768 * 1024 * 1024
MAX_DURATION = 14400.0


def _wire_modules():
    # The packaged server already carries the authoritative stdlib-only codec.
    try:
        from . import core
    except ImportError:
        import core
    schema = core._battle_wire_schema()
    return schema, importlib.import_module(type(schema).__module__)


def checked_path(path):
    path = os.path.abspath(os.path.expanduser(str(path or '').strip().strip('"')))
    if not path.lower().endswith('.wotlanreplay'):
        raise ValueError('Select a .wotlanreplay file, not a native .wotreplay.')
    if not os.path.isfile(path):
        raise ValueError('The selected replay file does not exist.')
    return path


def child_environment(environment, path=None):
    """Playback settings belong to one child, never to the launcher/global env."""
    value = dict(environment)
    value.pop(FILE_ENV, None)
    value.pop(AUTO_ENV, None)
    if path:
        value[FILE_ENV] = checked_path(path)
        value[AUTO_ENV] = '1'
    return value


def validate(path, progress=None, cancelled=None):
    path = checked_path(path)
    before = os.stat(path)
    count = total = 0
    stamp = 0.0
    next_log = time.monotonic() + 2
    schema, codec = _wire_modules()
    with gzip.open(path, 'rb') as stream:
        def read(size):
            nonlocal total
            if cancelled and cancelled():
                raise ValueError('Replay startup was cancelled.')
            raw = stream.read(size)
            total += len(raw)
            if len(raw) != size:
                raise ValueError('The replay is truncated.')
            if total > MAX_TOTAL:
                raise ValueError('The replay exceeds the decompressed size limit.')
            return raw

        if read(len(MAGIC)) != MAGIC:
            raise ValueError('This replay uses an unsupported format; the NG client requires WOTLANR2.')

        def row():
            kind, moment, size = RECORD_HEADER.unpack(read(RECORD_HEADER.size))
            if size > MAX_RECORD:
                raise ValueError('The replay has an oversized record.')
            payload = read(size)
            try:
                if kind in (4, 5):
                    value = schema.decode(payload)
                else:
                    value, end = codec._read_any(payload, 0)
                    if end != len(payload):
                        raise ValueError('Trailing bytes in a replay value.')
            except (ValueError, TypeError, IndexError, KeyError, struct.error, RecursionError) as error:
                raise ValueError('The replay contains an invalid binary record: %s' % error) from error
            return kind, moment, value

        first, unused, h = row()
        if (first != 0 or not isinstance(h, dict) or
                h.get('magic') != 'WOT_OFFLINE_REPLAY' or h.get('schema') != 1 or
                h.get('client') != '0.9.22.0.1-cn-1513'):
            raise ValueError('This is not a supported #1513 offline replay.')
        for key in ('welcome', 'start', 'snapshot', 'config'):
            if not isinstance(h.get(key), dict):
                raise ValueError('Replay header is missing %s.' % key)
        start, snapshot = h['start'], h['snapshot']
        if (start.get('type') != 'battle_start' or
                snapshot.get('type') != 'snapshot' or
                snapshot.get('round_id') != start.get('round_id')):
            raise ValueError('The replay initial round identity is inconsistent.')
        for kind in ('players', 'bots'):
            a = {v.get('id') for v in start.get(kind, ()) if isinstance(v, dict)}
            b = {v.get('id') for v in snapshot.get(kind, ()) if isinstance(v, dict)}
            if a != b or None in a:
                raise ValueError('The replay initial %s lineup is incomplete.' % kind)
        while True:
            kind, t, data = row()
            if not math.isfinite(t) or not stamp <= t <= MAX_DURATION:
                raise ValueError('The replay timestamps are invalid.')
            stamp = float(t)
            if kind == 3:
                if not isinstance(data, dict) or data.get('records') != count or stream.read(1):
                    raise ValueError('The replay footer/count is invalid.')
                break
            if kind not in (1, 2, 4, 5) or not isinstance(data, dict):
                raise ValueError('The replay contains an unsupported gameplay record.')
            # NativeReader filters non-playback wire messages and unmerged raw
            # replica deltas. Both still count in the writer's terminal record.
            # Replica admission/merging remains the native reader's owner.
            count += 1
            now = time.monotonic()
            if progress and now >= next_log:
                progress('REPLAY_ENTRY validating records=%d logical_bytes=%d' % (count, total))
                next_log = now + 2
    after = os.stat(path)
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError('The replay file changed during validation. Retry after saving finishes.')
    return dict(path=path, map=start.get('map'), vehicle=h['config'].get('vehicle'),
                players=len(start.get('players') or ()), bots=len(start.get('bots') or ()),
                duration=stamp, records=count, logical_bytes=total, file_bytes=after.st_size)
