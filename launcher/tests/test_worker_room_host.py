"""Native room ownership and persistent account-result configuration."""
import json
import os
import shutil
import tempfile
import unittest
from unittest import mock
import core
import stage_payload


class WorkerRoomHostTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.directory, True)
        self.game = os.path.join(self.directory, "instances", "player")

    def test_atomic_config_keeps_existing_receipts_and_room_options(self):
        identity = {"semanticVersion": "1.2.3", "buildIdentity": "test-build"}
        with mock.patch.object(core, "bundled_payload_identity", return_value=identity), \
                mock.patch.object(core, "repository_root", return_value=self.directory):
            path = core.write_room_host_config(self.game, loopback_only=True,
                bot_lineup=[{"vehicle": "ussr:R11_MS-1"}],
                bot_excluded_vehicles=["x", "x"], team1_size=4, team2_size=6)
        with open(path) as stream:
            value = json.load(stream)
        self.assertEqual(1, value["schema"])
        self.assertEqual("127.0.0.1", value["bind_address"])
        self.assertTrue(value["loopback"])
        self.assertEqual((4, 6), (value["team1_size"], value["team2_size"]))
        self.assertEqual(["x"], value["bot_excluded_vehicles"])
        self.assertEqual("test-build", value["build_identity"])
        self.assertEqual("1.2.3", value["semantic_version"])
        self.assertEqual(os.path.join(self.directory, "server",
            "unacked_battle_receipts-28792.json"), value["receipts_path"])
        self.assertEqual(self.game, value["overlay_root"])
        self.assertEqual(core.room_host_stop_path(self.game), value["stop_file"])
        self.assertFalse(os.path.exists(path + ".tmp"))

    def test_new_room_removes_only_its_stale_shutdown_request(self):
        core.write_room_host_config(self.game)
        stop = core.room_host_stop_path(self.game)
        with open(stop, "w") as stream:
            stream.write("stop")
        core.write_room_host_config(self.game)
        self.assertFalse(os.path.exists(stop))

    def test_only_the_worker_receives_the_current_room_configuration(self):
        inherited = {core.ROOM_HOST_CONFIG_ENV: "old-room.json",
                     "OFFLINE_LAN_0922_WORKER_INTERNAL_READY_MARKER": "old.ready"}
        worker = core.worker_environment(self.game, environment=inherited)
        player = core.visible_client_environment(
            self.game, core.PORT_0_9_22, environment=worker)
        self.assertEqual(core.room_host_config_path(self.game),
                         worker[core.ROOM_HOST_CONFIG_ENV])
        self.assertNotIn(core.ROOM_HOST_CONFIG_ENV, player)
        self.assertNotIn("OFFLINE_LAN_0922_WORKER_INTERNAL_READY_MARKER", player)
        self.assertEqual("old-room.json", inherited[core.ROOM_HOST_CONFIG_ENV])

    def test_frozen_receipts_stay_beside_the_existing_launcher(self):
        with mock.patch.object(core.sys, "frozen", True, create=True), \
                mock.patch.object(core, "launcher_directory", return_value=self.directory):
            path = core.write_room_host_config(self.game)
        with open(path) as stream:
            value = json.load(stream)
        self.assertEqual(os.path.join(self.directory,
            "unacked_battle_receipts-28792.json"), value["receipts_path"])

    def test_payload_staging_never_carries_a_python_battle_server(self):
        target = os.path.join(self.directory, "payload")
        self.assertEqual([], stage_payload.stage(target, include_clients=False))
        self.assertFalse(os.path.exists(os.path.join(target, "servers")))
        self.assertFalse(hasattr(core, "run_server_payload"))
        self.assertFalse(hasattr(core, "server_child_command"))
