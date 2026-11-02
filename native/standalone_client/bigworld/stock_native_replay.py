"""Offline replay recording and reading through the native writer thread and decoder."""
from __future__ import print_function

import datetime
import math
import os
import uuid

import offline_client_wire_native as _native
from gui.mods.offline_lan_0922 import offline_replay

HEADER, WIRE, LOCAL, END = range(4)
# A received replica delta that cannot be merged before the recording's first full snapshot.
UNMERGED = 5
_KINDS = {'wire': WIRE, 'local': LOCAL}
_NAMES = {WIRE: 'wire', LOCAL: 'local', END: 'end'}


class Pending(Exception):
    """An injected reader has no next record ready yet."""


class NativeRecorder(object):
    """Records local state from the main thread; the receive thread records every wire frame."""

    def __init__(self, directory, mode, header, clock):
        if mode not in (1, 2):
            raise ValueError('recording mode must be last or all')
        self.clock = clock
        self.started = float(clock())
        self.last_time = 0.0
        self.last_pose_at = -1.0
        self.bytes = 0
        self.count = 0
        self.closed = False
        self.failed = False
        if not os.path.isdir(directory):
            os.makedirs(directory)
        basename = ('replay_last_battle' if mode == 1 else
                    datetime.datetime.now().strftime('%Y%m%d_%H%M%S_') + uuid.uuid4().hex[:8])
        self.path = os.path.abspath(os.path.join(directory, basename + offline_replay.EXTENSION))
        self.temp = self.path + '.' + uuid.uuid4().hex[:8] + '.part'
        self._writer = _native.replay_writer(self.temp)
        self._handle = None
        snapshot = header.get('snapshot') or {}
        if '_native_snapshot' in snapshot:
            # The receive thread merged the rows; the header seeds playback with all of them.
            header = dict(header, snapshot=dict(_native.complete_snapshot(snapshot['_native_snapshot']),
                                                type='snapshot'))
        value = dict(header, magic=offline_replay.MAGIC, schema=offline_replay.SCHEMA,
                     client=offline_replay.CLIENT, created=datetime.datetime.now().isoformat())
        self._write(HEADER, 0.0, value)
        offline_replay.log('record_start mode=%s native_writer=True path=%s' % (mode, self.path))

    def _write(self, kind, moment, value):
        if self.closed:
            raise ValueError('recording is closed')
        self.bytes = _native.replay_write(self._writer, kind, moment, value)
        if self.bytes > offline_replay.MAX_TOTAL:
            raise ValueError('recording exceeded total byte limit')

    def attach(self, client):
        """Hand the writer to the native receive thread and start from a full snapshot."""
        handle = getattr(getattr(client, 'sock', None), '_handle', None)
        if handle is None:
            raise ValueError('recording requires the native transport')
        _native.net_record(handle, self._writer, float(self.clock()) - self.started)
        self._handle = handle
        client._request_snapshot_resync({'round_id': client.round_id,
                                         'authority_epoch': client.authority_epoch},
                                        client._transport_generation)

    def _detach(self):
        if self._handle is not None:
            _native.net_record(self._handle, None)
            self._handle = None

    def append(self, kind, data, moment=None):
        if self.closed:
            return False
        if kind == 'wire':
            # Wire frames are recorded in arrival order by the receive thread.
            return True
        elapsed = (float(self.clock()) - self.started if moment is None else float(moment))
        if math.isnan(elapsed) or math.isinf(elapsed) or elapsed < 0 or elapsed > offline_replay.MAX_DURATION:
            raise ValueError('invalid recording timestamp')
        elapsed = max(self.last_time, elapsed)
        self._write(_KINDS[kind], elapsed, data)
        self.last_time = elapsed
        self.count += 1
        return True

    def close(self, reason='leave'):
        if self.closed:
            return None if self.failed else self.path
        try:
            self._detach()
            self.count = _native.replay_count(self._writer)
            self._write(END, self.last_time, {'records': self.count, 'reason': str(reason)[:80]})
            self.closed = True
            ok, error = _native.replay_finish(self._writer)
            if not ok:
                raise IOError('replay writer failed: %s' % error)
            offline_replay._publish(self.temp, self.path)
            offline_replay.log('record_saved records=%s bytes=%s path=%s' % (self.count, self.bytes, self.path))
            return self.path
        except Exception:
            self.abort()
            raise

    def abort(self):
        try:
            self._detach()
        except Exception:
            pass
        self.failed = True
        self.closed = True
        try:
            _native.replay_finish(self._writer, 1)
        except Exception:
            pass
        # Retain the .part for diagnosis; it is not advertised as playable.


class NativeReader(object):
    """Forward reader with the former validation: header, clock, record count and EOF."""

    def __init__(self, path):
        if not path.lower().endswith(offline_replay.EXTENSION):
            raise ValueError('select an offline .wotlanreplay, not a native replay')
        self._reader = _native.replay_reader(path)
        self.last_time = 0.0
        self.count = 0
        self.ended = False
        first = _native.replay_read(self._reader)
        if first is None or first[0] != HEADER:
            raise ValueError('replay has no header')
        self.header = first[2]
        if (not isinstance(self.header, dict) or self.header.get('magic') != offline_replay.MAGIC or
                self.header.get('schema') != offline_replay.SCHEMA or
                self.header.get('client') != offline_replay.CLIENT):
            raise ValueError('unsupported offline replay format/client')
        for key in ('welcome', 'start', 'snapshot', 'config'):
            if not isinstance(self.header.get(key), dict):
                raise ValueError('replay header lacks %s' % key)
        if self.header['start'].get('type') != 'battle_start':
            raise ValueError('invalid replay battle start')

    def next(self):
        while True:
            row = self._next()
            if row is not False:
                return row

    def _next(self):
        """One playable record, or False for a record playback does not consume."""
        if self.ended:
            return None
        row = _native.replay_read(self._reader)
        if row is None:
            raise ValueError('replay ended without terminal record')
        kind, stamp, data = row
        if not self.last_time <= stamp <= offline_replay.MAX_DURATION:
            raise ValueError('non-monotonic replay clock')
        self.last_time = stamp
        if kind == END:
            if not isinstance(data, dict) or data.get('records') != self.count:
                raise ValueError('incomplete replay record count')
            if _native.replay_trailing(self._reader):
                raise ValueError('data follows replay end')
            self.ended = True
            return dict(data, type='end', t=stamp)
        self.count += 1
        if kind == UNMERGED:
            return False
        if kind not in (WIRE, LOCAL) or not isinstance(data, dict):
            raise ValueError('unsupported replay record')
        if kind == WIRE and data.get('type') not in offline_replay.MESSAGE_TYPES:
            return False
        return {'type': _NAMES[kind], 't': stamp, 'data': data}

    def close(self):
        self._reader = None
