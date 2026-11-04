"""Stage the pinned LAN server with the binary battle wire (python3 stage_server.py PINNED_SERVER OUTPUT)."""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

HELPERS = '''
import battle_wire_schema as _battle_wire_schema
import battle_wire_replica as _replica_split
_WIRE = _battle_wire_schema.SCHEMA
MAX_FRAME_BYTES = 4 * 1024 * 1024


def _hydraulic_body_receipt(raw):
    if not isinstance(raw, (list, tuple)) or len(raw) != 8:
        raise ValueError("hydraulic_body must contain eight numbers")
    bounds = (2000.0, 1000.0, 2000.0) + (math.pi * 2.0,) * 5
    values = []
    for value, bound in zip(raw, bounds):
        if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                not math.isfinite(float(value)) or abs(float(value)) > bound):
            raise ValueError("hydraulic_body is outside the input bounds")
        values.append(float(value))
    return tuple(values)


def _frame_ready(buffer):
    return len(buffer) >= 8 and len(buffer) >= 8 + int.from_bytes(buffer[:4], "little")


def _take_frame(buffer):
    size = 8 + int.from_bytes(buffer[:4], "little")
    return buffer[:size], buffer[size:]


def _decode_frame(frame):
    return _WIRE.decode(frame)


def _ng_probe_identity():
    return {
        "schema": 1,
        "semanticVersion": os.environ.get("WOT_OFFLINE_SEMANTIC_VERSION", ""),
        "buildIdentity": os.environ.get("WOT_OFFLINE_BUILD_IDENTITY", ""),
    }

'''

SERIALIZE_BEFORE = '    def _serialize_message(self, message, report_failure=True):\n        outgoing = _monotonic_endpoint_server_time(self, message)\n        try:\n            payload = (json.dumps(\n                outgoing, separators=(",", ":")) + "\\n").encode("utf-8")\n        except Exception as error:\n            self._record_outbound_failure(\n                "encode_error", message, error=error, sent_bytes=0)\n            raise\n        if len(payload) > MAX_LINE_BYTES:\n            try:\n                framed = state_transfer.frame_payload(\n                    outgoing, payload,\n                    state_transfer.CAPABILITY in getattr(self, "capabilities", ()))\n            except state_transfer.TransferError:\n                if report_failure:\n                    self._record_outbound_failure(\n                        "message_too_large", message, payload, sent_bytes=0)\n                return None\n            _server_log_limited(\n                "state-transfer:%s:%s" % (id(self), outgoing.get("type")),\n                "STATE_TRANSFER send kind=%s round=%s bytes=%d "\n                        "frames=%d wire_bytes=%d endpoint=%s" % (\n                outgoing.get("type"), outgoing.get("round_id"), len(payload),\n                framed.count(b"\\n"), len(framed),\n                getattr(self, "player_id", getattr(self, "worker_id", None))))\n            return framed\n        return payload\n\n'
SERIALIZE_AFTER = '''    def _serialize_message(self, message, report_failure=True):
        outgoing = _monotonic_endpoint_server_time(self, message)
        if "bot_orders" in outgoing:
            # Native control and order signatures ignore these server-only
            # receipts. Keep their richer planner data in server logs without
            # serializing it or mutating the server-owned orders.
            outgoing = dict(outgoing, bot_orders=[
                {key: value for key, value in order.items()
                 if key not in ("route_attempt", "route_assignment", "tactical_evidence")}
                for order in outgoing["bot_orders"]])
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
    # A delayed main-loop probe remains useful after the next probe is sent.
    # The trusted worker echoes the original client clock, so the relay does
    # not need a latest-sequence ledger that would discard slow responses.
    ('        self.worker_ping_pending = {}\n', '', 1),
    ('''            self.worker_ping_pending[player.player_id] = (
                seq, stamp, self.round_id, self.authority_epoch, worker)
            # Remove departed players; one outstanding probe per live player.
            for player_id in list(self.worker_ping_pending):
                if player_id not in self.players:
                    self.worker_ping_pending.pop(player_id, None)
            return worker.offer_reliable({
                'type': 'worker_ping', 'player_id': player.player_id,
                'seq': seq, 'round_id': self.round_id,
                'authority_epoch': self.authority_epoch})
''', '''            return worker.offer_reliable({
                'type': 'worker_ping', 'player_id': player.player_id,
                'seq': seq, 'client_time': stamp, 'round_id': self.round_id,
                'authority_epoch': self.authority_epoch})
''', 1),
    ('''            pending = self.worker_ping_pending.get(player_id)
            if pending is None:
                return False
            seq, stamp, round_id, epoch, owner = pending
            if (worker is not owner or worker is not self.simulation_worker or
                    not worker.connected or message.get('seq') != seq or
                    message.get('round_id') != round_id or
                    message.get('authority_epoch') != epoch or
                    self.round_id != round_id or self.authority_epoch != epoch):
                return False
            self.worker_ping_pending.pop(player_id, None)
''', '''            if (worker is not self.simulation_worker or
                    not worker.connected or
                    message.get('round_id') != self.round_id or
                    message.get('authority_epoch') != self.authority_epoch):
                return False
            seq, stamp = message.get('seq'), message.get('client_time')
            round_id, epoch = self.round_id, self.authority_epoch
''', 1),
    ('from gui.mods.offline_lan_0922 import ram_history, ram_motion\n',
     'from gui.mods.offline_lan_0922 import ram_history\nimport battle_wire_ram_motion as ram_motion\n', 1),
    ('from gui.mods.offline_lan_0922 import bot_state_codec, tank_contact_ledger, turret_contact_ledger\n',
     'from gui.mods.offline_lan_0922 import tank_contact_ledger, turret_contact_ledger\nimport battle_wire_bot_state_codec as bot_state_codec\n', 1),
    # Carry the visible native body's applied pose with the accepted input.
    ('            player.vehicle = vehicle\n',
     '            player.vehicle = vehicle\n            player.hydraulic_body = None\n', 1),
    ('    "up_cosine", "wreck_pose",\n))\n',
     '    "up_cosine", "wreck_pose", "hydraulic_body",\n))\n', 1),
    ('    up_cosine: float = 1.0\n',
     '    up_cosine: float = 1.0\n    hydraulic_body: Optional[tuple] = None\n', 1),
    ('            player.roll = 0.0\n',
     '            player.roll = 0.0\n            player.hydraulic_body = None\n', 1),
    ('            "up_cosine": None,\n',
     '            "up_cosine": None,\n            "hydraulic_body": None,\n', 1),
    ('        reason, field = self._modern_input_envelope_failure(\n'
     '            player, message, validate_contacts=inactive_modern)\n',
     '        if "hydraulic_body" in message:\n'
     '            if not all(name in message for name in ("x", "y", "z", "yaw")):\n'
     '                return ("hydraulic_body_context", "hydraulic_body"), parsed\n'
     '            try:\n'
     '                parsed["hydraulic_body"] = _hydraulic_body_receipt(message["hydraulic_body"])\n'
     '            except (TypeError, ValueError, OverflowError):\n'
     '                return ("hydraulic_body_shape", "hydraulic_body"), parsed\n'
     '        reason, field = self._modern_input_envelope_failure(\n'
     '            player, message, validate_contacts=inactive_modern)\n', 1),
    ('                    player.client_position = True\n'
     '                    self._record_vehicle_travel(\n',
     '                    player.hydraulic_body = parsed["hydraulic_body"]\n'
     '                    player.client_position = True\n'
     '                    self._record_vehicle_travel(\n', 1),
    ('        player.pose_history.append(sample)\n',
     '        if player.hydraulic_body is not None:\n'
     '            sample["hydraulic_body"] = player.hydraulic_body\n'
     '        player.pose_history.append(sample)\n', 1),
    ('            "up_cosine": round(player.up_cosine, 6),\n',
     '            "up_cosine": round(player.up_cosine, 6),\n'
     '            "hydraulic_body": player.hydraulic_body,\n', 1),
    # The visible client's tick-clock estimate trails by snapshot queueing
    # and tick catch-up; under load a legal trigger arrives ~1 s old.
    ('PLAYER_TRIGGER_MAX_LAG_MS = 500\n', 'PLAYER_TRIGGER_MAX_LAG_MS = 2000\n', 1),
    # Module helpers after the last top-level import of the json module.
    ('import json\n', 'import json\n' + HELPERS, 1),
    (SERIALIZE_BEFORE, SERIALIZE_AFTER, 1),
    # Players receive the replica split against their last completed write; the
    # simulation worker keeps complete rows.
    ('        actor_snapshot = self._is_actor_snapshot(message)\n',
     '        actor_snapshot = (self._is_actor_snapshot(message) and\n'
     '                          isinstance(self, Player))\n', 1),
    ('                outgoing, baseline = snapshot_delta.encode(\n'
     '                    message, self._snapshot_baseline, sequence,\n'
     '                    force_full=full_snapshot)\n'
     '            except snapshot_delta.SnapshotDeltaError:\n',
     '                outgoing, baseline = _replica_split.encode(\n'
     '                    message, self._snapshot_baseline, self.player_id,\n'
     '                    full_snapshot)\n'
     '            except (KeyError, TypeError):\n', 1),
    ('            delta = outgoing.get("snapshot_delta") is True\n',
     '            delta = outgoing.get("full") is False\n', 1),
    ('                outgoing, baseline = snapshot_delta.encode(\n'
     '                    message, None, sequence, force_full=True)\n',
     '                outgoing, baseline = _replica_split.encode(\n'
     '                    message, None, self.player_id, True)\n', 1),
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
    # Launcher preflight compares the packaged NG version before joining.
    ('                    "server_capabilities": list(SERVER_CAPABILITIES),\n'
     '                })\n'
     '                _server_log("PROBE OK %s:%d" % self.client_address)\n',
     '                    "server_capabilities": list(SERVER_CAPABILITIES),\n'
     '                    "ng_identity": _ng_probe_identity(),\n'
     '                })\n'
     '                _server_log("PROBE OK %s:%d" % self.client_address)\n', 1),
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


def _receipt_replace(source, before, after, count=1):
    found = source.count(before)
    if found != count:
        raise ValueError('pinned RAM receipt seam changed (%d != %d): %r' % (found, count, before[:80]))
    return source.replace(before, after, count)


def ram_motion_source(source):
    source = _receipt_replace(source, 'ROW_LENGTH = 21', 'ROW_LENGTH = 22', 1)
    source = _receipt_replace(source, 'len(row) != ROW_LENGTH', 'len(row) not in (21, ROW_LENGTH)', 1)
    source = _receipt_replace(source, 'row[3:]', 'row[3:21]', 1)
    source = _receipt_replace(source, 'result.append([peer, seq, stamp] + values)',
        'metadata = row[21] if len(row) == ROW_LENGTH else {"hydraulic_body": None}\n'
        '        if not isinstance(metadata, dict) or set(metadata) != set(("hydraulic_body",)):\n'
        '            raise ValueError("invalid ram body metadata")\n'
        '        body = metadata["hydraulic_body"]\n'
        '        if body is not None:\n'
        '            if not isinstance(body, (list, tuple)) or len(body) != 8:\n'
        '                raise ValueError("invalid ram body receipt")\n'
        '            bounds = (2000., 1000., 2000.) + (2 * math.pi,) * 5\n'
        '            if any(isinstance(v, bool) or not isinstance(v, INTEGER_TYPES + (float,)) or\n'
        '                   math.isnan(float(v)) or math.isinf(float(v)) or abs(v) > bound\n'
        '                   for v, bound in zip(body, bounds)):\n'
        '                raise ValueError("invalid ram body receipt")\n'
        '            body = [float(v) for v in body]\n'
        '        result.append([peer, seq, stamp] + values + [{"hydraulic_body": body}])', 1)
    return source


def bot_state_codec_source(source):
    source = _receipt_replace(source, 'from gui.mods.offline_lan_0922 import tank_contact_ledger, ram_motion',
        'from gui.mods.offline_lan_0922 import tank_contact_ledger\nimport battle_wire_ram_motion as ram_motion', 1)
    source = _receipt_replace(source, 'F_HAS_RAM_MOTION = 1 << 21', 'F_HAS_RAM_MOTION = 1 << 21\nF_HAS_RAM_BODIES = 1 << 26', 1)
    source = _receipt_replace(source, '    row = []\n    for name, scale in SCALARS:',
        '    if any(len(m) == 22 and m[21].get("hydraulic_body") is not None for m in state.get("ram_motion", ())):\n'
        '        flags |= F_HAS_RAM_BODIES\n    row = []\n    for name, scale in SCALARS:', 1)
    source = _receipt_replace(source, 'zip(motion[3:], ram_motion.SCALES))',
        'zip(motion[3:21], ram_motion.SCALES))\n'
        '            if flags & F_HAS_RAM_BODIES:\n'
        '                body = motion[21]["hydraulic_body"]\n'
        '                row.append(0 if body is None else 1)\n'
        '                if body is not None:\n'
        '                    row.extend(_fixed(v, POSITION_SCALE if i < 3 else ANGLE_SCALE) for i, v in enumerate(body))', 1)
    source = _receipt_replace(source, '[_real(cursor.take(), scale) for scale in ram_motion.SCALES])',
        '[_real(cursor.take(), scale) for scale in ram_motion.SCALES])\n'
        '            body = None\n'
        '            if flags & F_HAS_RAM_BODIES:\n'
        '                present = cursor.take()\n'
        '                if present not in (0, 1):\n'
        '                    raise BotStateCodecError("invalid ram body presence")\n'
        '                if present:\n'
        '                    body = [_real(cursor.take(), POSITION_SCALE if i < 3 else ANGLE_SCALE) for i in range(8)]\n'
        '            motions[-1].append({"hydraulic_body": body})', 1)
    return source


def stage(pinned, output, client_source=None):
    if os.path.exists(output):
        raise ValueError('output exists: %s' % output)
    shutil.copytree(pinned, output, ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    path = os.path.join(output, 'lan_battle_server.py')
    with open(path, encoding='utf-8', newline='') as source:
        text = source.read()
    with open(path, 'w', encoding='utf-8', newline='') as target:
        target.write(stage_text(text))
    if client_source is None:
        client_source = os.path.join(os.path.dirname(pinned), 'src', 'res', 'scripts', 'client')
    package = os.path.join(client_source, 'gui', 'mods', 'offline_lan_0922')
    for name, adapt in (('ram_motion', ram_motion_source), ('bot_state_codec', bot_state_codec_source)):
        with open(os.path.join(package, name + '.py'), encoding='utf-8') as source:
            adapted = adapt(source.read())
        compile(adapted, name + '.py', 'exec')
        with open(os.path.join(output, 'battle_wire_' + name + '.py'), 'w', encoding='utf-8') as target:
            target.write(adapted)
    shutil.copyfile(os.path.join(HERE, 'codec.py'), os.path.join(output, 'battle_wire_codec.py'))
    with open(os.path.join(HERE, 'schema.py'), encoding='utf-8') as source:
        schema = source.read().replace('from codec import', 'from battle_wire_codec import')
    with open(os.path.join(output, 'battle_wire_schema.py'), 'w', encoding='utf-8') as target:
        target.write(schema)
    with open(os.path.join(HERE, 'replica_split.py'), encoding='utf-8') as source:
        split = source.read().replace('from schema import', 'from battle_wire_schema import')
    with open(os.path.join(output, 'battle_wire_replica.py'), 'w', encoding='utf-8') as target:
        target.write(split)


if __name__ == '__main__':
    stage(sys.argv[1], sys.argv[2])
