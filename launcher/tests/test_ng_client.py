import json
import os
import tempfile
import unittest

import ng_client


class _Process:
    def __init__(self, exit_code=None):
        self.exit_code = exit_code

    def poll(self):
        return self.exit_code


def _write(root, relative, data=b"x"):
    path = os.path.join(root, *relative.split("/"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as stream:
        stream.write(data)
    return path


def _read(root, relative):
    with open(os.path.join(root, *relative.split("/")), "rb") as stream:
        return stream.read()


class DataRootTest(unittest.TestCase):
    def test_launcher_state_moves_below_the_ng_data_root_once(self):
        with tempfile.TemporaryDirectory() as temporary:
            environment = {"APPDATA": os.path.join(temporary, "Roaming"),
                           "LOCALAPPDATA": os.path.join(temporary, "Local")}
            root = ng_client.isolate_environment(environment)

            self.assertEqual(
                os.path.join(temporary, "Local", ng_client.DATA_DIR_NAME), root)
            roaming, local = ng_client.user_data_paths(ng_client.PLAYER, root)
            self.assertEqual(roaming, environment["APPDATA"])
            self.assertEqual(local, environment["LOCALAPPDATA"])
            self.assertTrue(os.path.isdir(roaming))
            self.assertEqual(root, ng_client.data_root(environment))
            # A second call, for example from a child process that inherited
            # the launcher environment, must not nest the root again.
            self.assertEqual(root, ng_client.isolate_environment(environment))
            self.assertEqual(roaming, environment["APPDATA"])
            self.assertEqual(root, ng_client.default_data_root(environment))

    def test_an_explicit_data_root_wins(self):
        with tempfile.TemporaryDirectory() as temporary:
            environment = {ng_client.DATA_ROOT_ENV: temporary,
                           "APPDATA": "C:\\Users\\p\\AppData\\Roaming"}
            self.assertEqual(temporary, ng_client.isolate_environment(environment))
            self.assertEqual(
                ng_client.user_data_paths(ng_client.PLAYER, temporary)[0],
                environment["APPDATA"])

    def test_player_and_worker_never_share_user_data(self):
        self.assertNotEqual(
            ng_client.user_data_paths(ng_client.PLAYER, "root"),
            ng_client.user_data_paths(ng_client.WORKER, "root"))
        with self.assertRaises(ng_client.NGClientError):
            ng_client.user_data_paths("observer", "root")


class PlayerInstanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.original = os.path.join(self.temp.name, "World_of_Tanks")
        _write(self.original, ng_client.ORIGINAL_EXECUTABLE)
        self.root = os.path.join(self.temp.name, "ng")

    def test_the_selected_original_maps_to_one_bound_instance(self):
        player = ng_client.player_instance(self.original, self.root)

        self.assertEqual(
            ng_client.instance_root(ng_client.PLAYER, self.root), player)
        self.assertTrue(ng_client.is_instance(player))
        self.assertEqual(
            os.path.abspath(self.original), ng_client.original_client_root(player))
        self.assertEqual(player, ng_client.player_instance(player, self.root))
        self.assertEqual(
            os.path.join(os.path.dirname(player), ng_client.WORKER),
            ng_client.sibling_instance(player, ng_client.WORKER))

    def test_selecting_another_original_rebinds_the_instance(self):
        other = os.path.join(self.temp.name, "Other")
        _write(other, ng_client.ORIGINAL_EXECUTABLE)
        ng_client.player_instance(self.original, self.root)
        player = ng_client.player_instance(other, self.root)
        self.assertEqual(
            os.path.abspath(other), ng_client.original_client_root(player))

    def test_a_folder_without_the_original_client_is_left_for_diagnosis(self):
        folder = os.path.join(self.temp.name, "empty")
        os.makedirs(folder)
        self.assertEqual(folder, ng_client.player_instance(folder, self.root))
        self.assertEqual("", ng_client.player_instance("", self.root))
        self.assertFalse(os.path.exists(self.root))
        self.assertEqual(folder, ng_client.original_client_root(folder))


class SyncInstanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.package = os.path.join(self.temp.name, "package")
        self.instance = os.path.join(self.temp.name, "instance")
        for relative in ng_client._REQUIRED:
            _write(self.package, relative, b"package " + relative.encode())
        _write(self.package, "res/scripts/client/game.pyc", b"bytecode")
        _write(self.package, "res/scripts/__pycache__/x.cpython-311.pyc")
        _write(self.package, ng_client.CONFIG_RELATIVE, b'{"shipped": true}')
        _write(self.package, "preferences.xml", b"developer preferences")
        _write(self.package, "logs/old.log")
        self._identity("first")

    def _identity(self, value):
        _write(self.package,
               "mods/configs/offline_lan_0922/build_identity.json",
               json.dumps({"buildIdentity": value}).encode())

    def test_package_content_is_copied_without_generated_files(self):
        copied, removed = ng_client.sync_instance(self.package, self.instance)

        self.assertGreater(copied, 0)
        self.assertEqual(0, removed)
        self.assertEqual(b"bytecode",
                         _read(self.instance, "res/scripts/client/game.pyc"))
        for absent in ("res/scripts/__pycache__", "preferences.xml", "logs"):
            self.assertFalse(os.path.exists(os.path.join(self.instance, absent)))
        self.assertTrue(ng_client.instance_current(self.instance, self.package))

    def test_user_state_survives_a_package_update(self):
        ng_client.sync_instance(self.package, self.instance)
        _write(self.instance, ng_client.CONFIG_RELATIVE, b'{"user": true}')
        _write(self.instance, "preferences.xml", b"user preferences")
        _write(self.instance, "mods/configs/offline_lan_0922/saves/career/save.json")
        _write(self.instance, ng_client.OVERLAY_RELATIVE + "/vehicles/a.xml")
        _write(self.package, "res/obsolete.txt")
        ng_client.sync_instance(self.package, self.instance)
        os.unlink(os.path.join(self.package, "res", "obsolete.txt"))
        self._identity("second")

        unused_copied, removed = ng_client.sync_instance(
            self.package, self.instance)

        self.assertEqual(1, removed)
        self.assertFalse(os.path.exists(
            os.path.join(self.instance, "res", "obsolete.txt")))
        self.assertEqual(b'{"user": true}',
                         _read(self.instance, ng_client.CONFIG_RELATIVE))
        self.assertEqual(b"user preferences",
                         _read(self.instance, "preferences.xml"))
        for kept in ("mods/configs/offline_lan_0922/saves/career/save.json",
                     ng_client.OVERLAY_RELATIVE + "/vehicles/a.xml"):
            self.assertTrue(os.path.isfile(
                os.path.join(self.instance, *kept.split("/"))))

    def test_an_unchanged_package_is_not_copied_again(self):
        ng_client.sync_instance(self.package, self.instance)
        self.assertEqual(
            (0, 0), ng_client.sync_instance(self.package, self.instance))

    def test_a_new_build_with_identical_file_stamps_is_recopied(self):
        ng_client.sync_instance(self.package, self.instance)
        source = os.path.join(self.package, "res", "scripts", "client", "game.pyc")
        stat = os.stat(source)
        with open(source, "wb") as stream:
            stream.write(b"BYTECODE")
        os.utime(source, ns=(stat.st_atime_ns, stat.st_mtime_ns))
        self._identity("second")

        ng_client.sync_instance(self.package, self.instance)

        self.assertEqual(b"BYTECODE",
                         _read(self.instance, "res/scripts/client/game.pyc"))

    def test_a_missing_config_is_seeded_for_the_launcher(self):
        os.unlink(os.path.join(self.package, *ng_client.CONFIG_RELATIVE.split("/")))
        ng_client.sync_instance(self.package, self.instance)
        self.assertEqual(
            ng_client._SEEDED_CONFIG,
            json.loads(_read(self.instance, ng_client.CONFIG_RELATIVE)))

    def test_an_incomplete_package_is_refused(self):
        os.unlink(os.path.join(self.package, ng_client.EXECUTABLE))
        with self.assertRaises(ng_client.NGClientError):
            ng_client.sync_instance(self.package, self.instance)
        self.assertFalse(os.path.exists(self.instance))


class ChildEnvironmentTest(unittest.TestCase):
    def test_each_role_gets_its_own_user_data_and_no_stale_session_values(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = {ng_client.DATA_ROOT_ENV: temporary,
                    ng_client.ORIGINAL_APPDATA_ENV: "retail",
                    "OFFLINE_LAN_0922_REPLAY_PATH": "stale.wotlanreplay",
                    "WOT_OFFLINE_STALE": "1", "BW_RES_PATH": "elsewhere",
                    "PATH": "kept"}
            player = ng_client.player_environment(
                "player-root", "127.0.0.1", 1234, base, preferred_team=2)
            worker = ng_client.worker_environment(
                "worker-root", "127.0.0.1", 1234, base)

        for environment in (player, worker):
            self.assertEqual("kept", environment["PATH"])
            self.assertEqual(temporary, environment[ng_client.DATA_ROOT_ENV])
            self.assertEqual("retail", environment[ng_client.ORIGINAL_APPDATA_ENV])
            for removed in ("OFFLINE_LAN_0922_REPLAY_PATH", "WOT_OFFLINE_STALE",
                            "BW_RES_PATH"):
                self.assertNotIn(removed, environment)
            self.assertEqual("1234", environment["OFFLINE_LAN_0922_SERVER_PORT"])
        self.assertNotEqual(player["APPDATA"], worker["APPDATA"])
        self.assertNotEqual(player["LOCALAPPDATA"], worker["LOCALAPPDATA"])
        self.assertEqual("2", player["OFFLINE_LAN_0922_PREFERRED_TEAM"])
        self.assertEqual(ng_client.PLAYER, player["OFFLINE_LAN_0922_CLIENT_MODE"])
        self.assertEqual(ng_client.SIMULATION_WORKER_MODE,
                         worker["OFFLINE_LAN_0922_CLIENT_MODE"])
        self.assertEqual(ng_client.worker_ready_marker("worker-root"),
                         worker[ng_client.WORKER_INTERNAL_READY_ENV])
        self.assertEqual(["-noConversion", "-unattended"],
                         ng_client.worker_command("worker-root")[1:])
        self.assertEqual(["-noConversion"],
                         ng_client.player_command("player-root")[1:])


class WorkerReadinessTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.instance = self.temp.name
        self.now = [0.0]

    def _wait(self, process, joined, **kwargs):
        def sleep(seconds):
            self.now[0] += seconds
        return ng_client.wait_for_worker_ready(
            process, self.instance, joined, timeout=1.0, interval=0.25,
            clock=lambda: self.now[0], sleep=sleep, **kwargs)

    def _publish(self, content=b"ready"):
        _write(self.instance, ng_client.WORKER_READY_MARKER, content)

    def test_ready_needs_the_marker_and_the_server_admission(self):
        self._publish()
        self.assertTrue(self._wait(_Process(), lambda: True))
        self.assertFalse(self._wait(_Process(), lambda: False))
        self.assertEqual(1.0, self.now[0])

    def test_a_partial_marker_is_not_ready(self):
        self._publish(b"rea")
        self.assertFalse(self._wait(_Process(), lambda: True))

    def test_an_exited_or_cancelled_worker_is_not_ready(self):
        self._publish()
        self.assertFalse(self._wait(_Process(exit_code=3), lambda: True))
        self.assertFalse(self._wait(
            _Process(), lambda: True, cancelled=lambda: True))

    def test_markers_from_an_earlier_session_are_cleared(self):
        self._publish()
        _write(self.instance, ng_client.PLAYER_FINISHED_MARKER + ".tmp")
        ng_client.clear_markers(self.instance)
        ng_client.clear_markers(self.instance)
        self.assertEqual([], os.listdir(self.instance))


class ProcDumpCommandTest(unittest.TestCase):
    def test_monitor_writes_one_dump_on_exception_or_termination(self):
        self.assertEqual(
            ["procdump.exe", "-accepteula", "-mm", "-n", "1", "-e", "-t",
             "42", "dump.dmp"],
            ng_client.procdump_monitor_command("procdump.exe", 42, "dump.dmp"))
        self.assertIn("-ma", ng_client.procdump_monitor_command(
            "procdump.exe", 42, "dump.dmp", full_memory=True))
        self.assertEqual(
            ["procdump.exe", "-accepteula", "-cancel", "42"],
            ng_client.procdump_cancel_command("procdump.exe", 42))


if __name__ == "__main__":
    unittest.main()
