"""Stage the pinned LAN server with the binary battle wire (python3 stage_server.py PINNED_SERVER OUTPUT)."""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

HELPERS = '''
import battle_wire_schema as _battle_wire_schema
_WIRE = _battle_wire_schema.SCHEMA
MAX_FRAME_BYTES = 4 * 1024 * 1024


def _frame_ready(buffer):
    return len(buffer) >= 8 and len(buffer) >= 8 + int.from_bytes(buffer[:4], "little")


def _take_frame(buffer):
    size = 8 + int.from_bytes(buffer[:4], "little")
    return buffer[:size], buffer[size:]


def _decode_frame(frame):
    return _WIRE.decode(frame)

'''

SERIALIZE_BEFORE = '    def _serialize_message(self, message, report_failure=True):\n        outgoing = _monotonic_endpoint_server_time(self, message)\n        try:\n            payload = (json.dumps(\n                outgoing, separators=(",", ":")) + "\\n").encode("utf-8")\n        except Exception as error:\n            self._record_outbound_failure(\n                "encode_error", message, error=error, sent_bytes=0)\n            raise\n        if len(payload) > MAX_LINE_BYTES:\n            try:\n                framed = state_transfer.frame_payload(\n                    outgoing, payload,\n                    state_transfer.CAPABILITY in getattr(self, "capabilities", ()))\n            except state_transfer.TransferError:\n                if report_failure:\n                    self._record_outbound_failure(\n                        "message_too_large", message, payload, sent_bytes=0)\n                return None\n            _server_log_limited(\n                "state-transfer:%s:%s" % (id(self), outgoing.get("type")),\n                "STATE_TRANSFER send kind=%s round=%s bytes=%d "\n                        "frames=%d wire_bytes=%d endpoint=%s" % (\n                outgoing.get("type"), outgoing.get("round_id"), len(payload),\n                framed.count(b"\\n"), len(framed),\n                getattr(self, "player_id", getattr(self, "worker_id", None))))\n            return framed\n        return payload\n\n'
SERIALIZE_AFTER = '''    def _serialize_message(self, message, report_failure=True):
        outgoing = _monotonic_endpoint_server_time(self, message)
        try:
            payload = _WIRE.encode(outgoing)
        except Exception as error:
            self._record_outbound_failure(
                "encode_error", message, error=error, sent_bytes=0)
            raise
        if len(payload) > MAX_FRAME_BYTES:
            if report_failure:
                self._record_outbound_failure(
                    "message_too_large", message, payload, sent_bytes=0)
            return None
        return payload

'''

EDITS = (
    # Module helpers after the last top-level import of the json module.
    ('import json\n', 'import json\n' + HELPERS, 1),
    (SERIALIZE_BEFORE, SERIALIZE_AFTER, 1),
    # The binary wire carries complete snapshots; the JSON row delta is retired.
    ('        actor_snapshot = self._is_actor_snapshot(message)\n',
     '        actor_snapshot = False\n', 1),
    ('        payload = (json.dumps(message, separators=(",", ":")) + "\\n").encode("utf-8")\n'
     '        conn.sendall(payload)\n',
     '        conn.sendall(_WIRE.encode(message))\n', 1),
    # Hello.
    ('            while b"\\n" not in buffer and len(buffer) < MAX_LINE_BYTES:\n',
     '            while not _frame_ready(buffer) and len(buffer) < MAX_FRAME_BYTES:\n', 1),
    ('            line, _, buffer = buffer.partition(b"\\n")\n'
     '            hello = json.loads(line.decode("utf-8"))\n',
     '            line, buffer = _take_frame(buffer)\n'
     '            hello = _decode_frame(line)\n', 1),
    # Launcher probe overlay exchange.
    ('                    while b"\\n" not in buffer:\n',
     '                    while not _frame_ready(buffer):\n', 1),
    # Worker and player session loops.
    ('                while b"\\n" in buffer:\n'
     '                    line, _, buffer = buffer.partition(b"\\n")\n',
     '                while _frame_ready(buffer):\n'
     '                    line, buffer = _take_frame(buffer)\n', 2),
    # Launcher probe overlay exchange (after the session loops share its text).
    ('                    line, _, buffer = buffer.partition(b"\\n")\n',
     '                    line, buffer = _take_frame(buffer)\n', 1),
    ('json.loads(line.decode("utf-8"))', '_decode_frame(line)', 3),
    ('                    if len(line) > MAX_LINE_BYTES:\n',
     '                    if len(line) > MAX_FRAME_BYTES:\n', 2),
    ('                if len(buffer) > MAX_LINE_BYTES * 4:\n',
     '                if len(buffer) > MAX_FRAME_BYTES * 2:\n', 2),
)


def stage_text(text):
    for before, after, count in EDITS:
        found = text.count(before)
        if found != count:
            raise ValueError('pinned server seam changed (%d != %d): %r' % (found, count, before[:80]))
        if before == 'import json\n':
            index = text.index(before) + len(before)
            text = text[:index] + HELPERS + text[index:]
        else:
            text = text.replace(before, after)
    return text


def stage(pinned, output):
    if os.path.exists(output):
        raise ValueError('output exists: %s' % output)
    shutil.copytree(pinned, output, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    path = os.path.join(output, 'lan_battle_server.py')
    with open(path, encoding='utf-8', newline='') as source:
        text = source.read()
    with open(path, 'w', encoding='utf-8', newline='') as target:
        target.write(stage_text(text))
    shutil.copyfile(os.path.join(HERE, 'codec.py'), os.path.join(output, 'battle_wire_codec.py'))
    with open(os.path.join(HERE, 'schema.py'), encoding='utf-8') as source:
        schema = source.read().replace('from codec import', 'from battle_wire_codec import')
    with open(os.path.join(output, 'battle_wire_schema.py'), 'w', encoding='utf-8') as target:
        target.write(schema)


if __name__ == '__main__':
    stage(sys.argv[1], sys.argv[2])
