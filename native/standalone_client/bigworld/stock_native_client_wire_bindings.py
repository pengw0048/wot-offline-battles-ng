"""Move the pinned LAN client transport from NDJSON lines to binary battle frames."""

PRELUDE = '''

import offline_client_wire_native as _wire_native


class _NativeSocket(object):
    """Socket-shaped handle for the native transport threads."""

    def __init__(self, host, port, timeout):
        self._handle = _wire_native.net_open(str(host), int(port), float(timeout))

    def sendall(self, payload):
        if not _wire_native.net_send(self._handle, payload):
            raise socket.error('server closed the connection')

    def settimeout(self, value):
        self._flush = value

    def setsockopt(self, *args):
        return None

    def receive(self):
        return _wire_native.net_receive(self._handle, 256)

    def close(self):
        _wire_native.net_close(self._handle, float(getattr(self, '_flush', None) or 0.5))
'''

DECODE_SNAPSHOT = '    def _decode_received_snapshot(self, message, generation):\n        """Advance raw actor state before any main-thread queue coalescing.\n\n        The wire baseline is independent of ``last_snapshot``: gameplay\n        admission may retain an invalid actor, and runtime consumers may\n        enrich or mutate their own rows. Neither operation may change the\n        ordered transport\'s next patch base. Only snapshots selected for\n        dispatch need mutable actor copies; the queue holds private versions.\n        """\n        if not isinstance(message, dict) or message.get(\'type\') != \'snapshot\':\n            return message\n        with self._outbound_lock:\n            if (generation != self._transport_generation or\n                    self._stopping or not self.running):\n                return None\n            baseline = self._snapshot_wire_baseline\n        try:\n            restored, next_baseline = snapshot_delta.decode(\n                message, baseline, return_pending_actors=True)\n        except snapshot_delta.SnapshotDeltaError:\n            with self._outbound_lock:\n                if (generation != self._transport_generation or\n                        self._stopping or not self.running):\n                    return None\n                # Only a new full message can repair a broken wire chain.\n                # Retaining an invalid high sequence would reject the\n                # server\'s otherwise valid recovery snapshot indefinitely.\n                self._snapshot_wire_baseline = None\n            self._request_snapshot_resync(message, generation)\n            return None\n        with self._outbound_lock:\n            if (generation != self._transport_generation or\n                    self._stopping or not self.running):\n                return None\n            self._snapshot_wire_baseline = next_baseline\n            self._snapshot_wire_sequence = _exact_int(\n                message.get(\'snapshot_seq\'))\n            if message.get(\'snapshot_delta\') is not True:\n                self._snapshot_resync_pending = False\n        return restored\n\n'

WORKER = "    def _worker(self, generation=None):\n        if generation is None:\n            generation = self._transport_generation\n        sock = None\n        decoder = state_transfer.StreamDecoder(self._state_transfer_diagnostic)\n        try:\n            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)\n            sock.settimeout(3.0)\n            sock.connect((self.host, self.port))\n            try:\n                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)\n            except Exception:\n                pass\n            with self._outbound_lock:\n                if (generation != self._transport_generation or\n                        self._stopping or not self.running):\n                    return\n                self.sock = sock\n            # The server requires hello to be the first wire message.  Do not\n            # expose the socket to the BigWorld poller until it is on the\n            # wire, or the first main-thread ping can win this race.\n            hello = self._hello_payload()\n            payload = (json.dumps(\n                hello, separators=(',', ':')) + '\\n').encode('utf-8')\n            with self._send_lock:\n                with self._outbound_lock:\n                    if (generation != self._transport_generation or\n                            self._stopping or not self.running or\n                            self.sock is not sock):\n                        return\n                sock.sendall(payload)\n            # Keep receive polling responsive.  Queued writes use\n            # _send_payload(), which can safely resume after a partial send\n            # instead of treating this socket-wide timeout as fatal.\n            sock.settimeout(0.5)\n            if not self._publish_connected_transport(sock, generation):\n                return\n            while (self.running and\n                   generation == self._transport_generation):\n                try:\n                    chunk = sock.recv(8192)\n                except socket.timeout:\n                    decoder.check_timeout(_monotonic_time())\n                    continue\n                if generation != self._transport_generation:\n                    break\n                if not chunk:\n                    decoder.finish()\n                    self._record_peer_close(generation, sock)\n                    break\n                received_time = _monotonic_time()\n                for message in decoder.feed(chunk, received_time):\n                    if generation != self._transport_generation:\n                        break\n                    message = self._decode_received_snapshot(\n                        message, generation)\n                    if message is None:\n                        continue\n                    if isinstance(message, dict):\n                        # Frame stalls must not inflate RTT or countdown\n                        # projection: both end in this network thread, not\n                        # when the BigWorld main thread eventually drains.\n                        message['_client_received_time'] = received_time\n                    if generation != self._transport_generation:\n                        break\n                    self._queue_message(message, generation)\n        except Exception as error:\n            if not (isinstance(error, socket.error) and\n                    self._transport_close_expected(generation, sock)):\n                self._record_transport_error(error, generation, sock)\n        finally:\n            wake_sender = False\n            with self._outbound_lock:\n                if (generation == self._transport_generation and\n                        self.sock is sock):\n                    self.connected = False\n                    self.running = False\n                    self._outbound_accepting = False\n                    self._outbound_queue = []\n                    self._outbound_bytes = 0\n                    self.sock = None\n                    wake_sender = True\n            if wake_sender:\n                self._outbound_event.set()\n            try:\n                if sock is not None:\n                    sock.close()\n            except Exception:\n                pass\n\n"
PUBLISH = '    def _publish_connected_transport(self, sock, generation):\n        """Atomically publish one hello-complete transport generation."""\n        sender = threading.Thread(\n            target=self._sender_worker, args=(sock, generation),\n            name=\'offline-lan-0922-sender\')\n        sender.setDaemon(True)\n        with self._outbound_lock:\n            if (generation != self._transport_generation or\n                    self._stopping or not self.running or\n                    self.sock is not sock):\n                return False\n            self.connected = True\n            self._outbound_accepting = True\n            self._sender_thread = sender\n            # Starting under the lifecycle lock closes the window where stop\n            # could join an assigned-but-not-yet-started sender.\n            try:\n                sender.start()\n            except Exception:\n                self.connected = False\n                self._outbound_accepting = False\n                self._sender_thread = None\n                raise\n        return True\n\n'
TRAILER = '\n\ndef _flush_native_outbound(self, generation):\n    """Hand queued messages to the native send queue in FIFO order."""\n    sock = self.sock\n    while isinstance(sock, _NativeSocket):\n        item = self._dequeue_outbound(generation)\n        if item is None:\n            return\n        result = self._send_wire(item[1], sock, generation)\n        if result is not True:\n            if result is False:\n                self._abort_outbound(self.last_error or \'LAN sender stopped\', generation)\n            return\n\n\ndef _release_native_transport(self, generation, sock):\n    with self._outbound_lock:\n        owned = generation == self._transport_generation and self.sock is sock\n        if owned:\n            self.connected = False\n            self.running = False\n            self._outbound_accepting = False\n            self._outbound_queue = []\n            self._outbound_bytes = 0\n            self.sock = None\n    if owned:\n        self._outbound_event.set()\n    try:\n        sock.close()\n    except Exception:\n        pass\n\n\ndef _drain_native_transport(self, generation):\n    """Queue received frames; replica snapshots arrive decoded and merged."""\n    sock = self.sock\n    if not isinstance(sock, _NativeSocket):\n        return\n    frames, state, error = sock.receive()\n    now = _monotonic_time()\n    try:\n        for frame, age, native in frames:\n            if generation != self._transport_generation:\n                return\n            if native is None:\n                message = self._decode_received_snapshot(_wire_native.decode(frame), generation)\n            elif isinstance(native, str):\n                # The receive thread could not merge a replica delta; ask for a full one.\n                self._request_snapshot_resync({\'round_id\': self.round_id,\n                                               \'authority_epoch\': self.authority_epoch}, generation)\n                continue\n            else:\n                with self._outbound_lock:\n                    self._snapshot_resync_pending = False\n                message = _wire_native.replica_message(native)\n            if isinstance(message, dict):\n                message[\'_client_received_time\'] = now - age\n                self._queue_message(message, generation)\n    except Exception as error:\n        self._record_transport_error(error, generation, sock)\n        self._release_native_transport(generation, sock)\n        return\n    if state == \'closed\':\n        self._record_peer_close(generation, sock)\n        self._release_native_transport(generation, sock)\n    elif state == \'failed\':\n        if not self._transport_close_expected(generation, sock):\n            self._record_transport_error(error or \'LAN transport failed\', generation, sock)\n        self._release_native_transport(generation, sock)\n\n\nLANClient._flush_native_outbound = _flush_native_outbound\nLANClient._release_native_transport = _release_native_transport\nLANClient._drain_native_transport = _drain_native_transport\n'

EDITS = (
    # Each decoded frame is a private complete snapshot; no wire baseline remains.
    (DECODE_SNAPSHOT,
     '    def _decode_received_snapshot(self, message, generation):\n'
     '        return message\n\n', 1),
    ('from gui.mods.offline_lan_0922 import effective_params as effective_params_wire\n',
     'from gui.mods.offline_lan_0922 import effective_params as effective_params_wire\n' + PRELUDE, 1),
    ('                    sock.sendall(LEAVE_PAYLOAD)\n',
     "                    sock.sendall(_wire_native.encode({'type': 'leave'}))\n", 1),
    (WORKER, '    def _worker(self, generation=None):\n        """Start the native transport; its threads own connect, receive and send."""\n        if generation is None:\n            generation = self._transport_generation\n        sock = None\n        try:\n            sock = _NativeSocket(self.host, self.port, 3.0)\n            with self._outbound_lock:\n                if (generation != self._transport_generation or\n                        self._stopping or not self.running):\n                    sock.close()\n                    return\n                self.sock = sock\n            # The server requires hello to be the first frame on the wire.\n            sock.sendall(_wire_native.encode(self._hello_payload()))\n            self._publish_connected_transport(sock, generation)\n        except Exception as error:\n            if sock is not None:\n                self._release_native_transport(generation, sock)\n            self._record_transport_error(error, generation, sock)\n\n', 1),
    (PUBLISH, '    def _publish_connected_transport(self, sock, generation):\n        """Publish one hello-complete generation; native threads send its frames."""\n        with self._outbound_lock:\n            if (generation != self._transport_generation or\n                    self._stopping or not self.running or\n                    self.sock is not sock):\n                return False\n            self.connected = True\n            self._outbound_accepting = True\n        return True\n\n', 1),
    ("                payload = (json.dumps(\n"
     "                    message, separators=(',', ':')) + '\\n').encode('utf-8')\n",
     "                payload = _wire_native.encode(message)\n", 1),
    ("            payload = (json.dumps(\n"
     "                message, separators=(',', ':'), allow_nan=False) +\n"
     "                '\\n').encode('utf-8')\n",
     "            payload = _wire_native.encode(message)\n", 1),
    # Queued messages leave immediately; no Python sender thread polls the queue.
    ('        self._outbound_event.set()\n        return True\n',
     '        self._outbound_event.set()\n        self._flush_native_outbound(generation)\n        return True\n', 1),
    ('        generation = self._transport_generation\n        self._poll_callback = None\n'
     '        self._take_ready_messages(generation)\n',
     '        generation = self._transport_generation\n        self._poll_callback = None\n'
     '        self._drain_native_transport(generation)\n        self._take_ready_messages(generation)\n', 1),
)


def client(source):
    return _apply(source, EDITS, 'LAN transport') + TRAILER


REPLAY_EDITS = (
    ('        from gui.mods.offline_lan_0922.replay_process import ProcessRecorder\n',
     '        from gui.mods.offline_lan_0922.native_replay import NativeRecorder\n', 1),
    ('        client._offline_replay_recorder = ProcessRecorder(\n',
     '        client._offline_replay_recorder = NativeRecorder(\n', 1),
    ("        log('record_start_requested mode=%s independent_process=True path=%s' %\n",
     "        log('record_start_requested mode=%s native_writer=True path=%s' %\n", 1),
    ("            clock=runtime._clock)\n",
     "            clock=runtime._clock)\n        client._offline_replay_recorder.attach(client)\n", 1),
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
