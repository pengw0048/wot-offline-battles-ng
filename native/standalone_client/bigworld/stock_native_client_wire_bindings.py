"""Move the pinned LAN client transport from NDJSON lines to binary battle frames."""

PRELUDE = '''

import offline_client_wire_native as _wire_native


class _WireStreamDecoder(object):
    """Split received bytes into frames and decode them natively."""

    def __init__(self):
        self._buffer = bytearray()

    def feed(self, chunk, now):
        self._buffer.extend(chunk)
        messages = []
        while True:
            size = _wire_native.frame_size(self._buffer)
            if not size or len(self._buffer) < size:
                break
            if size > MAX_WIRE_FRAME_BYTES:
                raise ValueError('battle frame exceeds the wire limit')
            frame = bytes(self._buffer[:size])
            del self._buffer[:size]
            messages.append(_wire_native.decode(frame))
        return messages

    def check_timeout(self, now):
        return None

    def finish(self):
        return None


MAX_WIRE_FRAME_BYTES = 4 * 1024 * 1024
'''

DECODE_SNAPSHOT = '    def _decode_received_snapshot(self, message, generation):\n        """Advance raw actor state before any main-thread queue coalescing.\n\n        The wire baseline is independent of ``last_snapshot``: gameplay\n        admission may retain an invalid actor, and runtime consumers may\n        enrich or mutate their own rows. Neither operation may change the\n        ordered transport\'s next patch base. Only snapshots selected for\n        dispatch need mutable actor copies; the queue holds private versions.\n        """\n        if not isinstance(message, dict) or message.get(\'type\') != \'snapshot\':\n            return message\n        with self._outbound_lock:\n            if (generation != self._transport_generation or\n                    self._stopping or not self.running):\n                return None\n            baseline = self._snapshot_wire_baseline\n        try:\n            restored, next_baseline = snapshot_delta.decode(\n                message, baseline, return_pending_actors=True)\n        except snapshot_delta.SnapshotDeltaError:\n            with self._outbound_lock:\n                if (generation != self._transport_generation or\n                        self._stopping or not self.running):\n                    return None\n                # Only a new full message can repair a broken wire chain.\n                # Retaining an invalid high sequence would reject the\n                # server\'s otherwise valid recovery snapshot indefinitely.\n                self._snapshot_wire_baseline = None\n            self._request_snapshot_resync(message, generation)\n            return None\n        with self._outbound_lock:\n            if (generation != self._transport_generation or\n                    self._stopping or not self.running):\n                return None\n            self._snapshot_wire_baseline = next_baseline\n            self._snapshot_wire_sequence = _exact_int(\n                message.get(\'snapshot_seq\'))\n            if message.get(\'snapshot_delta\') is not True:\n                self._snapshot_resync_pending = False\n        return restored\n\n'

EDITS = (
    # Each decoded frame is a private complete snapshot; no wire baseline remains.
    (DECODE_SNAPSHOT,
     '    def _decode_received_snapshot(self, message, generation):\n'
     '        return message\n\n', 1),
    ('from gui.mods.offline_lan_0922 import effective_params as effective_params_wire\n',
     'from gui.mods.offline_lan_0922 import effective_params as effective_params_wire\n' + PRELUDE, 1),
    ('                    sock.sendall(LEAVE_PAYLOAD)\n',
     "                    sock.sendall(_wire_native.encode({'type': 'leave'}))\n", 1),
    ('        decoder = state_transfer.StreamDecoder(self._state_transfer_diagnostic)\n',
     '        decoder = _WireStreamDecoder()\n', 1),
    ("            payload = (json.dumps(\n"
     "                hello, separators=(',', ':')) + '\\n').encode('utf-8')\n",
     "            payload = _wire_native.encode(hello)\n", 1),
    ("                payload = (json.dumps(\n"
     "                    message, separators=(',', ':')) + '\\n').encode('utf-8')\n",
     "                payload = _wire_native.encode(message)\n", 1),
    ("            payload = (json.dumps(\n"
     "                message, separators=(',', ':'), allow_nan=False) +\n"
     "                '\\n').encode('utf-8')\n",
     "            payload = _wire_native.encode(message)\n", 1),
)


def client(source):
    for before, after, count in EDITS:
        found = source.count(before)
        if found != count:
            raise ValueError('Pinned LAN transport seam changed (%d != %d): %r' % (found, count, before[:70]))
        source = source.replace(before, after)
    return source


REPLAY_EDITS = (
    ('        from gui.mods.offline_lan_0922.replay_process import ProcessRecorder\n',
     '        from gui.mods.offline_lan_0922.native_replay import NativeRecorder\n', 1),
    ('        client._offline_replay_recorder = ProcessRecorder(\n',
     '        client._offline_replay_recorder = NativeRecorder(\n', 1),
    ("        log('record_start_requested mode=%s independent_process=True path=%s' %\n",
     "        log('record_start_requested mode=%s native_writer=True path=%s' %\n", 1),
)
TRANSPORT_EDITS = (
    ('from gui.mods.offline_lan_0922 import offline_replay, replay_reader, replay_resources\n',
     'from gui.mods.offline_lan_0922 import offline_replay, replay_reader, replay_resources\n'
     'from gui.mods.offline_lan_0922 import native_replay\n', 1),
    ("        reader_factory = kwargs.pop('replay_reader_factory', replay_reader.ProcessReader)\n",
     "        reader_factory = kwargs.pop('replay_reader_factory', native_replay.NativeReader)\n", 1),
)


def _apply(source, edits, label):
    for before, after, count in edits:
        found = source.count(before)
        if found != count:
            raise ValueError('Pinned %s seam changed (%d != %d): %r' % (label, found, count, before[:70]))
        source = source.replace(before, after)
    return source


def offline_replay(source):
    return _apply(source, REPLAY_EDITS, 'replay recorder')


def replay_transport(source):
    return _apply(source, TRANSPORT_EDITS, 'replay transport')
