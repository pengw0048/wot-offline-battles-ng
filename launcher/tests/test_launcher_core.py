import ast
import base64
import hashlib
import io
import json
import os
import shutil
import stat
import sys
import tempfile
import unittest
import zipfile
from unittest import mock

import core
import ng_client
import server_imports
import stage_payload


class EndpointTest(unittest.TestCase):
    def test_address_without_port_uses_the_default(self):
        self.assertEqual(core.parse_endpoint("192.168.1.10"),
                         ("192.168.1.10", core.DEFAULT_SERVER_PORT))

    def test_address_with_port(self):
        self.assertEqual(core.parse_endpoint(" host.lan:9000 "),
                         ("host.lan", 9000))

    def test_empty_address_is_rejected(self):
        self.assertRaises(core.LauncherError, core.parse_endpoint, "  ")

    def test_port_out_of_range_is_rejected(self):
        self.assertRaises(core.LauncherError, core.parse_endpoint, "host:70000")

    def test_port_text_is_rejected(self):
        self.assertRaises(core.LauncherError, core.parse_endpoint, "host:abc")

    def test_single_player_uses_the_local_endpoint(self):
        for mode in (core.MODE_SINGLE, core.MODE_SINGLE):
            self.assertEqual(core.endpoint_for_mode(mode, "10.0.0.5"),
                             (core.LOCAL_HOST, core.DEFAULT_SERVER_PORT))

    def test_join_uses_the_typed_endpoint(self):
        self.assertEqual(core.endpoint_for_mode(core.MODE_JOIN, "10.0.0.5:1234"),
                         ("10.0.0.5", 1234))


class ServerRequirementTest(unittest.TestCase):
    def test_single_player_needs_a_server(self):
        for port_version in core.SUPPORTED_PORTS:
            self.assertTrue(core.server_required(port_version, core.MODE_SINGLE))

    def test_join_never_starts_a_local_server(self):
        for port_version in core.SUPPORTED_PORTS:
            self.assertFalse(core.server_required(port_version, core.MODE_JOIN))

    def test_single_player_needs_a_server(self):
        for port_version in core.SUPPORTED_PORTS:
            self.assertTrue(
                core.server_required(port_version, core.MODE_SINGLE))


class GameRootTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, True)

    def _write(self, relative_path, text=""):
        path = os.path.join(self.root, relative_path)
        directory = os.path.dirname(path)
        if directory and not os.path.isdir(directory):
            os.makedirs(directory)
        with open(path, "w") as stream:
            stream.write(text)
        return path

    def test_version_file_identifies_the_port(self):
        self._write("version.xml", "<version> v.0.9.22.0.1 #1513 </version>")
        self.assertEqual(core.read_client_version(self.root), "0.9.22.0.1")
        self.assertEqual(core.read_client_identity(self.root),
                         ("0.9.22.0.1", "1513"))
        self.assertEqual(core.detect_port(self.root), core.PORT_0_9_22)

    def test_another_0_9_22_build_uses_the_compatible_port(self):
        self._write("version.xml", "<version> v.0.9.22.0.1 #0789 </version>")
        self._write(
            "mods/0.9.22.0.1/legacy.offline_lan_0922_0.6.1.wotmod")
        self.assertEqual(core.PORT_0_9_22, core.detect_port(self.root))

    def test_another_0_9_22_patch_uses_the_compatible_port(self):
        self._write("version.xml", "<version> v.0.9.22.1 #1513 </version>")
        self.assertEqual(core.PORT_0_9_22, core.detect_port(self.root))

    def test_unsupported_client_reports_no_port(self):
        self._write("version.xml", "<version> v.1.0.0 #1 </version>")
        self._write(
            "mods/0.9.22.0.1/legacy.offline_lan_0922_0.6.1.wotmod")
        self.assertIsNone(core.detect_port(self.root))

    def test_an_empty_stock_mod_directory_is_not_an_install_marker(self):
        os.makedirs(os.path.join(self.root, "mods", "0.9.22.0.1"))
        self.assertIsNone(core.installed_port(self.root))

    def test_0_8_2_and_its_old_install_marker_are_not_supported(self):
        self._write("version.xml", "<version> v.0.8.2 #100 </version>")
        self._write(core.GAME_EXECUTABLE)
        self._write(os.path.join(
            "res_mods", "0.8.2", "scripts", "client", "gui", "mods",
            "offhangar", "__init__.py"))
        status = core.inspect_game_root(self.root)
        self.assertTrue(status["has_executable"])
        self.assertIsNone(status["client"])
        self.assertFalse(status["mod_installed"])


class SessionPlanTest(unittest.TestCase):
    @staticmethod
    def _status(**overrides):
        status = {
            "path": "C:\\Games\\WoT",
            "has_executable": True,
            "version": "0.9.22.0.1",
            "client": core.PORT_0_9_22,
            "mod_installed": True,
        }
        status.update(overrides)
        return status

    def test_join_plan_carries_the_typed_endpoint(self):
        session = core.plan_session(self._status(), core.MODE_JOIN,
                                    "10.0.0.5:1234")
        self.assertEqual(session["host"], "10.0.0.5")
        self.assertEqual(session["tcp_port"], 1234)
        self.assertFalse(session["needs_server"])

    def test_0_9_22_single_player_plan_starts_a_local_server(self):
        session = core.plan_session(
            self._status(), core.MODE_SINGLE, team_size="7",
            vehicle_profile="Fast MS-1")
        self.assertEqual(session["host"], core.LOCAL_HOST)
        self.assertTrue(session["needs_server"])
        self.assertEqual(7, session["team_size"])
        self.assertEqual("Fast MS-1", session["vehicle_profile"])

    def test_0_9_22_plan_carries_independent_team_sizes_and_preference(self):
        session = core.plan_session(
            self._status(), core.MODE_SINGLE,
            team1_size="3", team2_size="11", preferred_team="2")
        self.assertEqual(11, session["team_size"])
        self.assertEqual(3, session["team1_size"])
        self.assertEqual(11, session["team2_size"])
        self.assertEqual(2, session["preferred_team"])

    def test_vehicle_profile_is_planned_for_online_mode(self):
        # The room host may pin a vehicle profile for an Online battle; the
        # launcher verifies it against the room data at start time.
        session = core.plan_session(
            self._status(), core.MODE_JOIN, "10.0.0.5",
            vehicle_profile="Fast MS-1")
        self.assertEqual("Fast MS-1", session["vehicle_profile"])
        self.assertEqual(core.MODE_JOIN, session["mode"])

    def test_0_9_22_team_size_must_be_between_one_and_fifteen(self):
        for value in ("", "four", 0, 16, 1.5, True):
            with self.assertRaises(core.LauncherError, msg=value):
                core.plan_session(
                    self._status(), core.MODE_SINGLE, team_size=value)

    def test_join_does_not_apply_the_local_team_size(self):
        session = core.plan_session(
            self._status(), core.MODE_JOIN, "10.0.0.5", team_size="invalid")
        self.assertEqual(core.DEFAULT_TEAM_SIZE, session["team_size"])

    def test_join_keeps_team_preference_without_applying_local_capacities(self):
        session = core.plan_session(
            self._status(), core.MODE_JOIN, "10.0.0.5",
            team1_size="invalid", team2_size="invalid", preferred_team="1")
        self.assertEqual(core.DEFAULT_TEAM_SIZE, session["team1_size"])
        self.assertEqual(core.DEFAULT_TEAM_SIZE, session["team2_size"])
        self.assertEqual(1, session["preferred_team"])

    def test_preferred_team_must_be_auto_one_or_two(self):
        for value in ("three", 3, -1, 1.5, True):
            with self.assertRaises(core.LauncherError, msg=value):
                core.plan_session(
                    self._status(), core.MODE_JOIN, "10.0.0.5",
                    preferred_team=value)

    def test_a_missing_executable_stops_the_session(self):
        self.assertRaises(core.LauncherError, core.plan_session,
                          self._status(has_executable=False), core.MODE_SINGLE)

    def test_an_unsupported_client_stops_the_session(self):
        self.assertRaises(core.LauncherError, core.plan_session,
                          self._status(client=None), core.MODE_SINGLE)

    def test_an_unknown_mode_stops_the_session(self):
        self.assertRaises(core.LauncherError, core.plan_session,
                          self._status(), "spectate")

    def test_an_invalid_join_address_stops_the_session(self):
        self.assertRaises(core.LauncherError, core.plan_session,
                          self._status(), core.MODE_JOIN, "")

    def test_a_missing_mod_still_plans_a_session(self):
        session = core.plan_session(self._status(mod_installed=False),
                                    core.MODE_SINGLE)
        self.assertTrue(session["needs_server"])


class SettingsFileTest(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, True)

    def _read(self, relative_path):
        with open(os.path.join(self.root, relative_path), "rb") as stream:
            return json.load(stream)

    def test_0_9_22_writes_the_user_owned_endpoint(self):
        written = core.write_settings(self.root, core.PORT_0_9_22,
                                      core.MODE_JOIN, "10.0.0.5", 1234)
        endpoint = self._read(os.path.join(
            "mods", "configs", "offline_lan_0922", "server_endpoint.json"))
        self.assertEqual(endpoint, {"schema": 1, "host": "10.0.0.5",
                                    "port": 1234})
        self.assertEqual(len(written), 1)

    def test_0_9_22_name_updates_an_existing_config(self):
        config_path = os.path.join(self.root, "mods", "configs",
                                   "offline_lan_0922", "config.json")
        os.makedirs(os.path.dirname(config_path))
        with open(config_path, "w") as stream:
            json.dump({"schema": 1, "name": "Player", "max_health": 90}, stream)
        core.write_settings(self.root, core.PORT_0_9_22, core.MODE_SINGLE,
                            core.LOCAL_HOST, core.DEFAULT_SERVER_PORT, "PlayerOne")
        config = self._read(os.path.join(
            "mods", "configs", "offline_lan_0922", "config.json"))
        self.assertEqual(config["name"], "PlayerOne")
        self.assertEqual(config["max_health"], 90)

    def test_0_9_22_name_updates_a_windows_read_only_config(self):
        config_path = os.path.join(self.root, "mods", "configs",
                                   "offline_lan_0922", "config.json")
        os.makedirs(os.path.dirname(config_path))
        with open(config_path, "w") as stream:
            json.dump({"schema": 1, "name": "Player", "max_health": 90},
                      stream)
        os.chmod(config_path, stat.S_IREAD)
        original_replace = os.replace
        config_replace_attempts = []

        def windows_replace(source, target):
            if target == config_path:
                config_replace_attempts.append((source, target))
            if target == config_path and len(config_replace_attempts) == 1:
                error = PermissionError(13, "Access is denied", target)
                error.winerror = 5
                raise error
            return original_replace(source, target)

        with mock.patch("core.os.replace", side_effect=windows_replace):
            core.write_settings(
                self.root, core.PORT_0_9_22, core.MODE_SINGLE,
                core.LOCAL_HOST, core.DEFAULT_SERVER_PORT, "PlayerOne")

        config = self._read(os.path.join(
            "mods", "configs", "offline_lan_0922", "config.json"))
        self.assertEqual(config["name"], "PlayerOne")
        self.assertEqual(config["max_health"], 90)
        self.assertEqual(len(config_replace_attempts), 2)
        self.assertFalse(os.path.exists(config_path + ".tmp"))

    def test_unsupported_port_is_rejected(self):
        self.assertRaises(core.LauncherError, core.write_settings, self.root,
                          "0.1.0", core.MODE_SINGLE, core.LOCAL_HOST, 1)


class ProcDumpDownloadTest(unittest.TestCase):
    EXECUTABLE = (
        b"MZ" + b"\x00" * 58 + b"\x80\x00\x00\x00" +
        b"\x00" * 64 + b"PE\x00\x00" + b"\x4c\x01" +
        b"\x00" * 18 + b"\x0b\x01" + b"\x00" * 32)

    class _Response(io.BytesIO):
        def __init__(self, payload, url=None, content_length=None):
            super().__init__(payload)
            if content_length is None:
                content_length = len(payload)
            self.headers = {"Content-Length": str(content_length)}
            self._url = url or core.PROCDUMP_DOWNLOAD_URL

        def geturl(self):
            return self._url

    def setUp(self):
        self.root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.root, True)
        self.path = os.path.join(self.root, "tools", "procdump.exe")
        signature = mock.patch(
            "core._procdump_authenticode_is_trusted", return_value=True)
        self.signature_is_trusted = signature.start()
        self.addCleanup(signature.stop)

    @staticmethod
    def _archive(*entries):
        stream = io.BytesIO()
        with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as archive:
            for name, payload in entries:
                archive.writestr(name, payload)
        return stream.getvalue()

    @staticmethod
    def _mark_members_encrypted(payload):
        payload = bytearray(payload)
        for signature, flags_offset in ((b"PK\x03\x04", 6),
                                        (b"PK\x01\x02", 8)):
            cursor = 0
            while True:
                cursor = payload.find(signature, cursor)
                if cursor < 0:
                    break
                offset = cursor + flags_offset
                flags = int.from_bytes(payload[offset:offset + 2], "little")
                payload[offset:offset + 2] = (flags | 1).to_bytes(2, "little")
                cursor += len(signature)
        return bytes(payload)

    def _opener(self, payload):
        return mock.Mock(return_value=self._Response(payload))

    def test_default_path_lives_beside_launcher_settings_in_user_cache(self):
        settings = os.path.join(self.root, "launcher.json")
        with mock.patch("core.settings_path", return_value=settings):
            path = core.procdump_executable()

        self.assertEqual(self.path, path)

    def test_frozen_bundle_state_does_not_change_the_user_cache_path(self):
        settings = os.path.join(self.root, "launcher.json")
        bundle = os.path.join(self.root, "launcher", "_internal")
        with mock.patch("core.settings_path", return_value=settings), \
                mock.patch.object(core.sys, "_MEIPASS", bundle, create=True):
            path = core.procdump_executable()

        self.assertEqual(self.path, path)
        self.assertFalse(path.startswith(bundle))

    def test_explicit_cache_base_takes_priority_over_bundle_state(self):
        with mock.patch.object(
                core.sys, "_MEIPASS", "/ignored", create=True):
            path = core.procdump_executable(self.root)

        self.assertEqual(self.path, path)

    def test_download_installs_the_exact_root_32_bit_executable(self):
        payload = self._archive(
            ("procdump.exe", self.EXECUTABLE),
            ("procdump64.exe", b"not-the-selected-executable"),
            ("Eula.txt", b"license"))
        opener = self._opener(payload)

        installed = core.download_procdump(self.path, opener=opener)

        self.assertEqual(self.path, installed)
        with open(self.path, "rb") as stream:
            self.assertEqual(self.EXECUTABLE, stream.read())
        opener.assert_called_once_with(
            core.PROCDUMP_DOWNLOAD_URL,
            timeout=core.PROCDUMP_DOWNLOAD_TIMEOUT_SECONDS)

    def test_valid_cached_executable_is_reused_without_a_download(self):
        os.makedirs(os.path.dirname(self.path))
        with open(self.path, "wb") as stream:
            stream.write(self.EXECUTABLE)
        opener = mock.Mock(side_effect=AssertionError("must not download"))

        installed = core.download_procdump(self.path, opener=opener)

        self.assertEqual(self.path, installed)
        opener.assert_not_called()

    def test_untrusted_cached_executable_is_not_reused(self):
        os.makedirs(os.path.dirname(self.path))
        with open(self.path, "wb") as stream:
            stream.write(self.EXECUTABLE)
        self.signature_is_trusted.return_value = False

        self.assertFalse(core.procdump_is_installed(self.path))

    def test_untrusted_download_is_not_installed(self):
        payload = self._archive(("procdump.exe", self.EXECUTABLE))
        self.signature_is_trusted.return_value = False

        with self.assertRaisesRegex(core.LauncherError, "signature"):
            core.download_procdump(self.path, opener=self._opener(payload))

        self.assertFalse(os.path.exists(self.path))
        self.assertEqual([], [
            name for name in os.listdir(os.path.dirname(self.path))
            if name.startswith(".procdump-")])

    def test_bad_zip_is_rejected(self):
        with self.assertRaisesRegex(core.LauncherError, "valid ZIP"):
            core.download_procdump(
                self.path, opener=self._opener(b"not a zip"))
        self.assertFalse(os.path.exists(self.path))

    def test_duplicate_root_executable_members_are_rejected(self):
        payload = self._archive(
            ("procdump.exe", self.EXECUTABLE),
            ("PROCDUMP.EXE", self.EXECUTABLE))

        with self.assertRaisesRegex(core.LauncherError, "unique procdump.exe"):
            core.download_procdump(self.path, opener=self._opener(payload))
        self.assertFalse(os.path.exists(self.path))

    def test_non_pe_executable_is_rejected(self):
        payload = self._archive(("procdump.exe", b"not-a-pe-executable"))

        with self.assertRaisesRegex(core.LauncherError, "executable is invalid"):
            core.download_procdump(self.path, opener=self._opener(payload))
        self.assertFalse(os.path.exists(self.path))

    def test_64_bit_pe_executable_is_rejected(self):
        executable = bytearray(self.EXECUTABLE)
        executable[132:134] = b"\x64\x86"
        executable[152:154] = b"\x0b\x02"
        payload = self._archive(("procdump.exe", bytes(executable)))

        with self.assertRaises(core.LauncherError):
            core.download_procdump(self.path, opener=self._opener(payload))
        self.assertFalse(os.path.exists(self.path))

    def test_redirect_away_from_official_https_host_is_rejected(self):
        payload = self._archive(("procdump.exe", self.EXECUTABLE))
        response = self._Response(
            payload, url="https://downloads.example.test/Procdump.zip")

        with self.assertRaises(core.LauncherError):
            core.download_procdump(
                self.path, opener=mock.Mock(return_value=response))
        self.assertFalse(os.path.exists(self.path))

    def test_oversized_download_is_rejected(self):
        with mock.patch.object(core, "PROCDUMP_ARCHIVE_MAX_BYTES", 4):
            with self.assertRaisesRegex(
                    core.LauncherError, "download is unexpectedly large"):
                core.download_procdump(
                    self.path, opener=self._opener(b"12345"))
        self.assertFalse(os.path.exists(self.path))

    def test_oversized_declared_download_is_rejected(self):
        payload = self._archive(("procdump.exe", self.EXECUTABLE))
        response = self._Response(
            payload, content_length=core.PROCDUMP_ARCHIVE_MAX_BYTES + 1)

        with self.assertRaises(core.LauncherError):
            core.download_procdump(
                self.path, opener=mock.Mock(return_value=response))
        self.assertFalse(os.path.exists(self.path))

    def test_oversized_executable_member_is_rejected_before_extraction(self):
        payload = self._archive(("procdump.exe", self.EXECUTABLE))

        with mock.patch.object(core, "PROCDUMP_EXECUTABLE_MAX_BYTES", 8):
            with self.assertRaisesRegex(
                    core.LauncherError, "executable has an invalid size"):
                core.download_procdump(
                    self.path, opener=self._opener(payload))
        self.assertFalse(os.path.exists(self.path))

    def test_encrypted_executable_member_is_rejected(self):
        payload = self._mark_members_encrypted(
            self._archive(("procdump.exe", self.EXECUTABLE)))

        with self.assertRaises(core.LauncherError):
            core.download_procdump(self.path, opener=self._opener(payload))
        self.assertFalse(os.path.exists(self.path))

    def test_failed_atomic_install_does_not_overwrite_existing_file(self):
        os.makedirs(os.path.dirname(self.path))
        original = b"existing invalid cache"
        with open(self.path, "wb") as stream:
            stream.write(original)
        payload = self._archive(("procdump.exe", self.EXECUTABLE))

        with mock.patch("core.os.replace", side_effect=OSError("locked")):
            with self.assertRaises(core.LauncherError):
                core.download_procdump(
                    self.path, opener=self._opener(payload))

        with open(self.path, "rb") as stream:
            self.assertEqual(original, stream.read())
        self.assertEqual([], [
            name for name in os.listdir(os.path.dirname(self.path))
            if name.startswith(".procdump-")])


class ServerPayloadTest(unittest.TestCase):

    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.work, True)
        environment = mock.patch.dict(os.environ, {
            ng_client.DATA_ROOT_ENV: os.path.join(self.work, "ng")})
        environment.start()
        self.addCleanup(environment.stop)
        self.game = os.path.join(self.work, "ng", "instances", "player")

    def test_launcher_log_lives_beside_settings(self):
        settings = os.path.join(tempfile.gettempdir(), "state", "launcher.json")
        with mock.patch.object(core, "settings_path", return_value=settings):
            self.assertEqual(
                os.path.join(os.path.dirname(settings), "launcher.log"),
                core.launcher_log_path())

    def test_server_log_lives_beside_the_frozen_launcher(self):
        executable = os.path.join(
            tempfile.gettempdir(), "portable-launcher", "Launcher.exe")

        self.assertEqual(
            os.path.join(os.path.dirname(executable), "server.log"),
            core.server_log_path(executable=executable, frozen=True))

    def test_source_server_log_lives_beside_the_launcher_script(self):
        self.assertEqual(
            os.path.join(os.path.dirname(core.__file__), "server.log"),
            core.server_log_path(frozen=False))

    def test_repository_layout_resolves_the_server(self):
        script = core.server_script(core.PORT_0_9_22)
        self.assertTrue(os.path.isfile(script))
        self.assertEqual(
            os.path.join(core.repository_root(), "server", "windows_server.py"),
            script)

    def test_0_9_22_server_binds_without_arguments(self):
        argv = core.server_argv(core.PORT_0_9_22, "/payload")
        self.assertEqual(argv[1:], [])

    def test_frozen_command_reruns_the_launcher_executable(self):
        command = core.server_child_command(
            core.PORT_0_9_22, executable="C:\\launcher.exe", frozen=True)
        self.assertEqual(command, ["C:\\launcher.exe", core.SERVE_FLAG,
                                   core.PORT_0_9_22])

    def test_source_command_passes_the_launcher_script(self):
        command = core.server_child_command(
            core.PORT_0_9_22, launcher_script="/repo/launcher/wot_launcher.py",
            executable="/usr/bin/python3", frozen=False)
        self.assertEqual(command, ["/usr/bin/python3",
                                   "/repo/launcher/wot_launcher.py",
                                   core.SERVE_FLAG, core.PORT_0_9_22])

    def test_server_environment_keeps_only_live_server_inputs(self):
        environment = core.server_environment(core.PORT_0_9_22, "/game", {})
        self.assertEqual(
            str(core.DEFAULT_TEAM_SIZE),
            environment[core.SERVER_TEAM_SIZE_ENV_0922])
        self.assertEqual("[]", environment[core.SERVER_BOT_LINEUP_ENV_0922])

    def test_server_and_clients_receive_the_bundled_diagnostic_identity(self):
        identity = {
            "schema": 1,
            "semanticVersion": "0.6.1",
            "buildIdentity": "test-build-a",
        }
        with mock.patch.object(
                core, "bundled_payload_identity", return_value=identity):
            environments = (
                core.server_environment(
                    core.PORT_0_9_22, "/game", {}),
                core.worker_environment(self.game, environment={}),
                core.visible_client_environment(
                    self.game, core.PORT_0_9_22, environment={}),
            )

        for environment in environments:
            self.assertEqual(
                "0.6.1", environment[core.BUILD_SEMANTIC_VERSION_ENV])
            self.assertEqual(
                "test-build-a", environment[core.BUILD_IDENTITY_ENV])

    def test_0_9_22_server_receives_the_selected_team_size(self):
        environment = core.server_environment(
            core.PORT_0_9_22, "/game", {}, team_size=4)
        self.assertEqual("4", environment[core.SERVER_TEAM_SIZE_ENV_0922])

    def test_0_9_22_server_receives_independent_team_sizes(self):
        environment = core.server_environment(
            core.PORT_0_9_22, "/game", {},
            team1_size=4, team2_size=9)
        self.assertEqual("9", environment[core.SERVER_TEAM_SIZE_ENV_0922])
        self.assertEqual("4", environment[core.SERVER_TEAM1_SIZE_ENV_0922])
        self.assertEqual("9", environment[core.SERVER_TEAM2_SIZE_ENV_0922])

    def test_0_9_22_server_receives_the_selected_bot_lineup(self):
        environment = core.server_environment(
            core.PORT_0_9_22, "/game", {}, bot_lineup=[{
                "team": 1, "slot": 2,
                "vehicle": "ussr:R11_MS-1",
            }])
        self.assertEqual(
            '[{"team":1,"slot":2,"vehicle":"ussr:R11_MS-1"}]',
            environment[core.SERVER_BOT_LINEUP_ENV_0922])

    def test_server_receives_profile_exclusions_and_clears_inherited_values(self):
        name = "ussr:R11_MS-1"
        key = core.SERVER_BOT_EXCLUDED_VEHICLES_ENV_0922
        environment = core.server_environment(
            core.PORT_0_9_22, "/game", {}, bot_excluded_vehicles=[name])
        self.assertEqual('["ussr:R11_MS-1"]', environment[key])
        self.assertEqual("[]", core.server_environment(
            core.PORT_0_9_22, "/game", environment)[key])

    def test_single_player_server_is_explicitly_loopback_only(self):
        environment = core.server_environment(
            core.PORT_0_9_22, "/game", {}, loopback_only=True)
        self.assertEqual(
            "1", environment[core.SERVER_LOOPBACK_ONLY_ENV_0922])
        lan_environment = core.server_environment(
            core.PORT_0_9_22, "/game",
            {core.SERVER_LOOPBACK_ONLY_ENV_0922: "1"})
        self.assertNotIn(core.SERVER_LOOPBACK_ONLY_ENV_0922, lan_environment)

    def test_hidden_worker_inherits_the_selected_server_endpoint(self):
        worker = os.path.join(self.work, "ng", "instances", "worker")
        environment = core.worker_environment(
            self.game, "10.0.0.5", 1234, team_size=7, environment={
                core.CLIENT_MODE_ENV_0922: "player",
                "BW_RES_PATH": "stale",
                "WOT_OFFLINE_REPLAY_FILE": "stale",
            })
        self.assertEqual(
            "10.0.0.5", environment[core.CLIENT_SERVER_HOST_ENV_0922])
        self.assertEqual(
            "1234", environment[core.CLIENT_SERVER_PORT_ENV_0922])
        self.assertEqual("7", environment[core.SERVER_TEAM_SIZE_ENV_0922])
        self.assertEqual(
            ng_client.SIMULATION_WORKER_MODE,
            environment[core.CLIENT_MODE_ENV_0922])
        self.assertEqual(
            os.path.join(worker, ng_client.WORKER_READY_MARKER),
            environment[ng_client.WORKER_INTERNAL_READY_ENV])
        self.assertEqual(
            ng_client.user_data_paths(ng_client.WORKER),
            (environment["APPDATA"], environment["LOCALAPPDATA"]))
        for name in ("BW_RES_PATH", "WOT_OFFLINE_REPLAY_FILE"):
            self.assertNotIn(name, environment)
        self.assertEqual(
            [os.path.join(worker, ng_client.EXECUTABLE),
             "-noConversion", "-unattended"],
            core.worker_child_command(self.game))

    def test_hidden_worker_inherits_independent_team_sizes(self):
        environment = core.worker_environment(
            "/game", team1_size=2, team2_size=8, environment={})
        self.assertEqual("2", environment[core.SERVER_TEAM1_SIZE_ENV_0922])
        self.assertEqual("8", environment[core.SERVER_TEAM2_SIZE_ENV_0922])

    def test_visible_0_9_22_client_uses_isolated_config_and_endpoint(self):
        self.assertEqual(
            [os.path.join(self.game, ng_client.EXECUTABLE), "-noConversion"],
            core.visible_client_command(self.game, core.PORT_0_9_22))
        environment = core.visible_client_environment(
            self.game, core.PORT_0_9_22, "10.0.0.5", 1234,
            environment={
                core.CLIENT_MODE_ENV_0922: "simulation_worker",
                ng_client.WORKER_INTERNAL_READY_ENV: "stale",
            })
        self.assertEqual(
            "10.0.0.5", environment[core.CLIENT_SERVER_HOST_ENV_0922])
        self.assertEqual(
            "1234", environment[core.CLIENT_SERVER_PORT_ENV_0922])
        self.assertEqual(
            "1", environment[core.ALLOW_MULTIPLE_CLIENTS_ENV_0922])
        self.assertEqual(
            core.PLAYER_MODE_0922,
            environment[core.CLIENT_MODE_ENV_0922])
        self.assertNotIn(ng_client.WORKER_INTERNAL_READY_ENV, environment)
        self.assertEqual(
            os.path.join(self.game, ng_client.PLAYER_READY_MARKER),
            environment[ng_client.PLAYER_READY_ENV])
        self.assertEqual(
            ng_client.user_data_paths(ng_client.PLAYER),
            (environment["APPDATA"], environment["LOCALAPPDATA"]))

    def test_visible_client_language_overrides_inherited_selection(self):
        for language, expected in (("zh", "zh"), ("en", "en"),
                                   ("invalid", "en")):
            inherited = {"WOT_OFFLINE_UI_LANGUAGE": "zh"}
            environment = core.visible_client_environment(
                self.game, core.PORT_0_9_22, environment=inherited,
                language=language)
            self.assertEqual(expected, environment["WOT_OFFLINE_UI_LANGUAGE"])
            self.assertEqual({"WOT_OFFLINE_UI_LANGUAGE": "zh"}, inherited)

    def test_visible_client_carries_the_preferred_team(self):
        environment = core.visible_client_environment(
            self.game, core.PORT_0_9_22, environment={}, preferred_team=2)
        self.assertEqual(
            "2", environment[core.CLIENT_PREFERRED_TEAM_ENV_0922])

    def test_missing_payload_reports_a_launcher_error(self):
        self.assertRaises(core.LauncherError, core.run_server_payload,
                          core.PORT_0_9_22, tempfile.mkdtemp())


class ClientInstallTest(unittest.TestCase):
    """The launcher refreshes NG instances and keeps every user-owned file."""

    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.work, True)
        # Maintenance tests must never discover the developer's real saves.
        # The fallback-path cases deliberately run without APPDATA; tests of
        # external saves provide their own temporary APPDATA explicitly.
        environment = mock.patch.dict(os.environ, {
            "APPDATA": "", "LOCALAPPDATA": os.path.join(self.work, "local"),
            ng_client.DATA_ROOT_ENV: os.path.join(self.work, "ng"),
            ng_client.PACKAGE_ENV: os.path.join(
                self.work, "payload", ng_client.PACKAGE_PAYLOAD_DIR)})
        environment.start()
        self.addCleanup(environment.stop)
        self.original = os.path.join(self.work, "game")
        self.payload = os.path.join(self.work, "payload")
        self.package = os.path.join(self.payload, ng_client.PACKAGE_PAYLOAD_DIR)
        self._make_0_9_22_target()
        self.game = ng_client.player_instance(self.original)
        self.worker = core.worker_root(self.game)

    def _write(self, root, relative_path, text="x"):
        path = os.path.join(root, *relative_path.split("/"))
        directory = os.path.dirname(path)
        if directory and not os.path.isdir(directory):
            os.makedirs(directory)
        with open(path, "w") as stream:
            stream.write(text)
        return path

    def _read(self, relative_path, root=None):
        with open(os.path.join(root or self.game,
                               *relative_path.split("/"))) as stream:
            return stream.read()

    def _stage_0_9_22(self, content="new", build_identity="test-build-a",
                      extra=None):
        if os.path.isdir(self.package):
            shutil.rmtree(self.package)
        self._write(self.package, ng_client.EXECUTABLE, content)
        self._write(self.package, "offline-res.json",
                    json.dumps({"revision": "pinned"}))
        self._write(
            self.package, "tools/prepare_native_client.py",
            "import os\n"
            "def prepare(client, package, overlays=()):\n"
            "    lines = [str(path) for path in overlays] + [str(client)]\n"
            "    with open(os.path.join(str(package), 'paths.xml'), 'w') "
            "as stream:\n"
            "        stream.write('\\n'.join(lines))\n")
        self._write(self.package, ng_client.CONFIG_RELATIVE, content)
        self._write(
            self.package, "mods/configs/offline_lan_0922/navgraphs/"
            "manifest.json", json.dumps({"maps": []}))
        if build_identity is not None:
            self._write(
                self.package, core.BUILD_IDENTITY_RELATIVE_PATH_0922,
                json.dumps({
                    "schema": 1,
                    "semanticVersion": "0.6.1",
                    "buildIdentity": build_identity,
                }))
        for relative, text in (extra or {}).items():
            self._write(self.package, relative, text)
        return self.package

    def _make_0_9_22_target(self):
        self._write(self.original, core.GAME_EXECUTABLE, "")
        self._write(
            self.original, "version.xml",
            "<version> v.0.9.22.0.1 #1513 </version>")

    def test_install_prepares_both_instances_from_the_package(self):
        self._stage_0_9_22()

        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)

        for instance in (self.game, self.worker):
            self.assertEqual(
                "new", self._read(ng_client.EXECUTABLE, instance))
            self.assertEqual(self.original,
                             ng_client.original_client_root(instance))
            paths = self._read("paths.xml", instance).splitlines()
            self.assertEqual(ng_client.overlay_root(self.game), paths[0])
            self.assertEqual(self.original, paths[-1])
        status = core.inspect_game_root(self.game)
        self.assertEqual(core.PORT_0_9_22, status["client"])
        self.assertTrue(status["mod_installed"])
        self.assertEqual(self.game, status["path"])
        self.assertEqual(self.original, status["original_path"])

    def test_install_needs_the_original_client_binding(self):
        self._stage_0_9_22()
        unbound = os.path.join(self.work, "unbound")
        os.makedirs(unbound)

        with self.assertRaises(core.LauncherError):
            core.install_client_mod(unbound, core.PORT_0_9_22, self.payload)

    def test_install_removes_files_the_package_no_longer_carries(self):
        self._stage_0_9_22(extra={"tools/retired.py": "old"})
        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)
        self._write(self.game, "mods/configs/offline_lan_0922/notes.json",
                    "keep")
        self._write(self.game, "replays/offline/one.wotlanreplay", "keep")
        self._write(self.game, "preferences.xml", "keep")
        self._stage_0_9_22(content="newer")

        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)

        self.assertFalse(os.path.exists(
            os.path.join(self.game, "tools", "retired.py")))
        self.assertEqual("newer", self._read(ng_client.EXECUTABLE))
        for relative in ("mods/configs/offline_lan_0922/notes.json",
                         "replays/offline/one.wotlanreplay",
                         "preferences.xml"):
            self.assertEqual("keep", self._read(relative))

    def test_a_deleted_package_file_is_restored(self):
        self._stage_0_9_22()
        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)
        manifest = os.path.join(
            self.game, "mods", "configs", "offline_lan_0922", "navgraphs",
            "manifest.json")
        os.unlink(manifest)

        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)

        self.assertTrue(os.path.isfile(manifest))

    def test_0_9_22_install_keeps_the_saved_settings(self):
        self._stage_0_9_22()
        self._write(self.game,
                    "mods/configs/offline_lan_0922/server_endpoint.json", "mine")
        self._write(self.game, "mods/configs/offline_lan_0922/config.json",
                    "mine")
        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)
        self.assertEqual("mine", self._read(
            "mods/configs/offline_lan_0922/server_endpoint.json"))
        self.assertEqual("mine", self._read(
            "mods/configs/offline_lan_0922/config.json"))

    def test_0_9_22_install_writes_a_missing_configuration(self):
        self._stage_0_9_22()
        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)
        self.assertEqual("new", self._read(
            "mods/configs/offline_lan_0922/config.json"))

    def test_a_package_without_configuration_seeds_the_client_schema(self):
        self._stage_0_9_22()
        os.unlink(os.path.join(self.package,
                               *ng_client.CONFIG_RELATIVE.split("/")))

        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)

        self.assertEqual({"schema": 3}, json.loads(self._read(
            ng_client.CONFIG_RELATIVE, self.worker)))

    def test_the_selected_save_reaches_the_client_configuration(self):
        self._stage_0_9_22()
        self._write(self.game, "mods/configs/offline_lan_0922/config.json",
                    json.dumps({"schema": 3, "name": "Player",
                                "save_slot": "default"}))

        core.write_settings(
            self.game, core.PORT_0_9_22, core.MODE_SINGLE, "127.0.0.1",
            28792, "PlayerOne", "career-2")

        config = json.loads(
            self._read("mods/configs/offline_lan_0922/config.json"))
        self.assertEqual("career-2", config["save_slot"])
        self.assertEqual("PlayerOne", config["name"])

    def test_a_launch_without_a_save_leaves_the_configured_one_alone(self):
        self._stage_0_9_22()
        self._write(self.game, "mods/configs/offline_lan_0922/config.json",
                    json.dumps({"schema": 3, "save_slot": "career-2"}))

        core.write_settings(
            self.game, core.PORT_0_9_22, core.MODE_SINGLE, "127.0.0.1", 28792)

        config = json.loads(
            self._read("mods/configs/offline_lan_0922/config.json"))
        self.assertEqual("career-2", config["save_slot"])

    def test_an_unusable_save_id_never_reaches_the_client(self):
        self._stage_0_9_22()
        self._write(self.game, "mods/configs/offline_lan_0922/config.json",
                    json.dumps({"schema": 3, "save_slot": "default"}))

        with self.assertRaises(core.LauncherError):
            core.write_settings(
                self.game, core.PORT_0_9_22, core.MODE_SINGLE, "127.0.0.1",
                28792, "PlayerOne", "../escape")

        config = json.loads(
            self._read("mods/configs/offline_lan_0922/config.json"))
        self.assertEqual("default", config["save_slot"])

    def test_startup_repair_quarantines_only_an_invalid_config(self):
        default_config = json.dumps({
            "enabled": True,
            "startupTimeoutSeconds": 30.0,
            "physics_tuning": {},
            "he_tuning": {},
            "perfect_accuracy": False,
        })
        self._stage_0_9_22(default_config)
        self._write(
            self.game, "mods/configs/offline_lan_0922/config.json",
            "{broken")
        for name in ("server_endpoint.json", "account_state.json",
                     "garage_state.json"):
            self._write(
                self.game, "mods/configs/offline_lan_0922/" + name,
                "saved-" + name)
        postbattle = json.dumps({
            "schema": 1,
            "accountKey": "offline",
            "pending": [{"arenaUniqueID": 17}],
            "history": [],
            "progress": {},
        })
        self._write(
            self.game,
            "mods/configs/offline_lan_0922/postbattle_state.json",
            postbattle)

        actions = core.repair_0_9_22_startup(
            self.game, self.payload, is_running=lambda: False)

        self.assertEqual(default_config, self._read(
            "mods/configs/offline_lan_0922/config.json"))
        self.assertEqual("{broken", self._read(
            "mods/configs/offline_lan_0922/config.json.invalid"))
        for name in ("server_endpoint.json", "account_state.json",
                     "garage_state.json"):
            self.assertEqual("saved-" + name, self._read(
                "mods/configs/offline_lan_0922/" + name))
        self.assertEqual(postbattle, self._read(
            "mods/configs/offline_lan_0922/postbattle_state.json"))
        self.assertIn("kept the saved endpoint", " ".join(actions))

    def test_startup_repair_quarantines_an_unusable_save_id(self):
        """The client refuses to start on it, so repair has to notice it too."""
        default_config = json.dumps({"enabled": True, "save_slot": "default"})
        self._stage_0_9_22(default_config)
        broken = json.dumps({"enabled": True, "save_slot": "../escape"})
        self._write(
            self.game, "mods/configs/offline_lan_0922/config.json", broken)

        core.repair_0_9_22_startup(
            self.game, self.payload, is_running=lambda: False)

        self.assertEqual(default_config, self._read(
            "mods/configs/offline_lan_0922/config.json"))
        self.assertEqual(broken, self._read(
            "mods/configs/offline_lan_0922/config.json.invalid"))

    def test_startup_repair_keeps_a_named_save(self):
        default_config = json.dumps({"enabled": True})
        self._stage_0_9_22(default_config)
        saved_config = json.dumps({"enabled": True, "save_slot": "career-2"})
        self._write(
            self.game, "mods/configs/offline_lan_0922/config.json",
            saved_config)

        core.repair_0_9_22_startup(
            self.game, self.payload, is_running=lambda: False)

        self.assertEqual(saved_config, self._read(
            "mods/configs/offline_lan_0922/config.json"))

    def test_updates_and_startup_repair_preserve_all_external_save_slots(self):
        self._stage_0_9_22(json.dumps({"enabled": True}))
        app_data = os.path.join(self.work, "app-data")
        relative_root = "Wargaming.net/WorldOfTanks/offline_lan_0922"
        saved_files = {}
        for slot in ("default", "career-2"):
            for name in ("garage_state.json", "account_state.json",
                         "postbattle_state.json", "garage_state.backup1.json"):
                content = "player-owned-%s-%s" % (slot, name)
                path = self._write(app_data, "%s/saves/%s/%s" % (
                    relative_root, slot, name), content)
                saved_files[path] = content
        legacy = self._write(
            app_data, relative_root + "/garage_state.json", "legacy-progress")
        saved_files[legacy] = "legacy-progress"
        with mock.patch.dict(os.environ, {"APPDATA": app_data}):
            core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)
            core.install_client_mod(
                self.game, core.PORT_0_9_22, self.payload, force=True)
            core.repair_0_9_22_startup(
                self.game, self.payload, is_running=lambda: False)
        for path, expected in saved_files.items():
            with open(path) as stream:
                self.assertEqual(expected, stream.read(), path)

    def test_startup_repair_keeps_a_valid_config(self):
        default_config = json.dumps({"enabled": True})
        self._stage_0_9_22(default_config)
        saved_config = json.dumps({
            "enabled": False, "startupTimeoutSeconds": 45.0})
        self._write(
            self.game, "mods/configs/offline_lan_0922/config.json",
            saved_config)

        core.repair_0_9_22_startup(
            self.game, self.payload, is_running=lambda: False)

        self.assertEqual(saved_config, self._read(
            "mods/configs/offline_lan_0922/config.json"))
        self.assertFalse(os.path.exists(os.path.join(
            self.game, "mods", "configs", "offline_lan_0922",
            "config.json.invalid")))

    def test_startup_repair_refuses_to_touch_a_running_game(self):
        self._stage_0_9_22(json.dumps({"enabled": True}))
        self._write(
            self.game, "mods/configs/offline_lan_0922/config.json",
            "{broken")

        with self.assertRaisesRegex(core.LauncherError,
                                    "Close the NG client"):
            core.repair_0_9_22_startup(
                self.game, self.payload, is_running=lambda: True)

        self.assertEqual("{broken", self._read(
            "mods/configs/offline_lan_0922/config.json"))

    def test_failed_startup_repair_restores_the_invalid_config(self):
        self._write(
            self.game, "mods/configs/offline_lan_0922/config.json",
            "{broken")

        def fail_install(game_root, port_version, base_dir, force):
            self._write(
                self.game, "mods/configs/offline_lan_0922/config.json",
                "partial-default")
            raise core.LauncherError("install failed")

        with mock.patch.object(
                core, "install_client_mod", side_effect=fail_install):
            with self.assertRaisesRegex(core.LauncherError, "install failed"):
                core.repair_0_9_22_startup(
                    self.game, self.payload, is_running=lambda: False)

        self.assertEqual("{broken", self._read(
            "mods/configs/offline_lan_0922/config.json"))
        self.assertFalse(os.path.exists(os.path.join(
            self.game, "mods", "configs", "offline_lan_0922",
            "config.json.invalid")))

    def test_reset_deletes_only_known_offline_state_after_confirmation(self):
        default_config = json.dumps({"enabled": True})
        self._stage_0_9_22(default_config)
        state_root = "mods/configs/offline_lan_0922/"
        for name in ("config.json", "config.json.invalid",
                     "server_endpoint.json", "server_endpoint.json.tmp",
                     "account_state.json", "garage_state.json.bak",
                     "postbattle_state.json"):
            self._write(self.game, state_root + name, "saved-" + name)
        self._write(self.game, state_root + "notes.json", "keep")
        self._write(
            self.game, state_root + "vehicle_profiles.json", "profiles")

        actions = core.reset_0_9_22_state(
            self.game, self.payload, is_running=lambda: False)

        self.assertEqual(default_config, self._read(state_root + "config.json"))
        for name in ("config.json.invalid", "server_endpoint.json",
                     "server_endpoint.json.tmp", "account_state.json",
                     "garage_state.json.bak", "postbattle_state.json"):
            self.assertFalse(os.path.exists(os.path.join(
                self.game, *(state_root + name).split("/"))))
        self.assertEqual("keep", self._read(state_root + "notes.json"))
        self.assertEqual(
            "profiles", self._read(state_root + "vehicle_profiles.json"))
        self.assertIn("Deleted 7 offline saved-data file(s).", actions)

    def test_reset_deletes_the_save_slots_of_an_installation_without_appdata(
            self):
        """Without APPDATA the client keeps its saves inside the game folder.

        Leaving them behind would contradict the reset confirmation and would
        also leave the next start reading a garage the player asked to drop.
        """
        default_config = json.dumps({"enabled": True})
        self._stage_0_9_22(default_config)
        state_root = "mods/configs/offline_lan_0922/"
        saves_root = state_root + core.SAVES_DIR_NAME_0_9_22 + "/"
        self._write(self.game, saves_root + "default/garage_state.json", "a")
        self._write(self.game, saves_root + "career/postbattle_state.json", "b")
        self._write(self.game, state_root + "notes.json", "keep")

        core.reset_0_9_22_state(
            self.game, self.payload, is_running=lambda: False)

        self.assertFalse(os.path.exists(os.path.join(
            self.game, *saves_root.rstrip("/").split("/"))))
        self.assertEqual("keep", self._read(state_root + "notes.json"))

    def test_reset_deletes_save_slots_from_appdata(self):
        default_config = json.dumps({"enabled": True})
        self._stage_0_9_22(default_config)
        appdata = os.path.join(self.work, "appdata")
        saves_root = os.path.join(
            appdata, "Wargaming.net", "WorldOfTanks", "offline_lan_0922",
            core.SAVES_DIR_NAME_0_9_22)
        state_root = os.path.dirname(saves_root)
        self._write(
            saves_root, "default/garage_state.json", "default garage")
        self._write(
            saves_root, "career/postbattle_state.json", "career results")
        self._write(state_root, "garage_state.json", "legacy garage")
        self._write(state_root, "notes.json", "keep")

        with mock.patch.dict(os.environ, {"APPDATA": appdata}):
            core.reset_0_9_22_state(
                self.game, self.payload, is_running=lambda: False)

        self.assertFalse(os.path.exists(saves_root))
        self.assertFalse(os.path.exists(os.path.join(
            state_root, "garage_state.json")))
        with open(os.path.join(state_root, "notes.json")) as stream:
            self.assertEqual("keep", stream.read())

    def test_failed_reset_restores_appdata_saves_and_legacy_state(self):
        appdata = os.path.join(self.work, "appdata")
        saves_root = os.path.join(
            appdata, "Wargaming.net", "WorldOfTanks", "offline_lan_0922",
            core.SAVES_DIR_NAME_0_9_22)
        state_root = os.path.dirname(saves_root)
        self._write(
            saves_root, "default/garage_state.json", "default garage")
        self._write(state_root, "garage_state.json", "legacy garage")

        with mock.patch.dict(os.environ, {"APPDATA": appdata}), \
                mock.patch.object(
                    core, "install_client_mod",
                    side_effect=core.LauncherError("install failed")):
            with self.assertRaisesRegex(core.LauncherError, "install failed"):
                core.reset_0_9_22_state(
                    self.game, self.payload, is_running=lambda: False)

        with open(os.path.join(
                saves_root, "default", "garage_state.json")) as stream:
            self.assertEqual("default garage", stream.read())
        with open(os.path.join(state_root, "garage_state.json")) as stream:
            self.assertEqual("legacy garage", stream.read())

    def test_reset_also_deletes_both_instance_preferences(self):
        default_config = json.dumps({"enabled": True})
        self._stage_0_9_22(default_config)
        player = self._write(self.game, "preferences.xml", "player settings")
        worker = self._write(self.worker, "preferences.xml", "worker settings")
        original = self._write(self.original, "preferences.xml", "retail")

        actions = core.reset_0_9_22_state(
            self.game, self.payload, is_running=lambda: False)

        self.assertFalse(os.path.exists(player))
        self.assertFalse(os.path.exists(worker))
        with open(original) as stream:
            self.assertEqual("retail", stream.read())
        self.assertIn("Deleted 2 offline saved-data file(s).", actions)

    def test_failed_reset_restores_the_instance_preferences(self):
        preferences = self._write(self.game, "preferences.xml", "keep me")

        with mock.patch.object(
                core, "install_client_mod",
                side_effect=core.LauncherError("install failed")):
            with self.assertRaisesRegex(core.LauncherError, "install failed"):
                core.reset_0_9_22_state(
                    self.game, self.payload, is_running=lambda: False)

        with open(preferences) as stream:
            self.assertEqual("keep me", stream.read())

    def test_failed_reset_restores_every_saved_file(self):
        state_root = "mods/configs/offline_lan_0922/"
        for name in ("config.json", "server_endpoint.json",
                     "garage_state.json", "postbattle_state.json"):
            self._write(self.game, state_root + name, "saved-" + name)

        with mock.patch.object(
                core, "install_client_mod",
                side_effect=core.LauncherError("install failed")):
            with self.assertRaisesRegex(core.LauncherError, "install failed"):
                core.reset_0_9_22_state(
                    self.game, self.payload, is_running=lambda: False)

        for name in ("config.json", "server_endpoint.json",
                     "garage_state.json", "postbattle_state.json"):
            self.assertEqual("saved-" + name,
                             self._read(state_root + name))

    def test_a_new_build_identity_refreshes_the_instances(self):
        self._stage_0_9_22(content="first", build_identity="test-build-a")
        core.install_client_mod(self.game, core.PORT_0_9_22, self.payload)
        self._stage_0_9_22(content="second", build_identity="test-build-b")

        actions = core.install_client_mod(
            self.game, core.PORT_0_9_22, self.payload)

        self.assertEqual("second", self._read(ng_client.EXECUTABLE))
        self.assertIn("build=test-build-b", " ".join(actions))
        self.assertEqual({
            "schema": 1,
            "semanticVersion": "0.6.1",
            "buildIdentity": "test-build-b",
        }, core.installed_payload_identity(self.game, core.PORT_0_9_22))

    def test_a_missing_build_identity_is_soft(self):
        self._stage_0_9_22(build_identity=None)

        actions = core.install_client_mod(
            self.game, core.PORT_0_9_22, self.payload)

        self.assertIsNone(core.installed_payload_identity(
            self.game, core.PORT_0_9_22))
        self.assertIn("version=unknown build=unknown", " ".join(actions))

    def test_a_launcher_without_the_ng_client_reports_it(self):
        self.assertRaises(core.LauncherError, core.install_client_mod,
                          self.game, core.PORT_0_9_22, self.payload)

    def test_an_incomplete_ng_client_package_is_refused(self):
        self._stage_0_9_22()
        os.unlink(os.path.join(self.package, ng_client.EXECUTABLE))

        self.assertRaises(core.LauncherError, core.install_client_mod,
                          self.game, core.PORT_0_9_22, self.payload)


class PayloadStagingTest(unittest.TestCase):
    def setUp(self):
        self.root = os.path.join(tempfile.mkdtemp(), "payload")
        self.addCleanup(shutil.rmtree, os.path.dirname(self.root), True)
        self.written = stage_payload.stage(self.root, include_clients=False)
        self.target = os.path.join(self.root, stage_payload.SERVER_DIR)

    @staticmethod
    def _write_ng_client(package):
        for relative in stage_payload.NG_CLIENT_REQUIRED + (
                "offline-res/scripts/client/gui/mods/mod_offline.pyc",
                "mods/configs/offline_lan_0922/navgraphs/manifest.json"):
            path = os.path.join(package, *relative.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as stream:
                stream.write("x")

    def test_supported_server_entry_points_are_staged(self):
        expected_ports = {core.PORT_0_9_22}
        self.assertEqual(expected_ports, set(stage_payload.PAYLOAD_FILES))
        self.assertEqual(expected_ports, set(stage_payload.PAYLOAD_TREES))
        for port_version in core.SUPPORTED_PORTS:
            self.assertTrue(
                os.path.isfile(core.server_script(port_version, self.target)),
                port_version)

    def test_windows_distribution_guard_rejects_map_studio_paths(self):
        script_path = os.path.join(
            os.path.dirname(os.path.dirname(__file__)), "build_launcher.ps1")
        with open(script_path, "r", encoding="utf-8") as stream:
            script = stream.read()

        self.assertIn('"mapstudio"', script)
        self.assertIn("Map Studio", script)

    def test_windows_distribution_omits_historical_test_guides(self):
        launcher_root = os.path.dirname(os.path.dirname(__file__))
        repo_root = os.path.dirname(launcher_root)
        guide_name = "TESTING_20260916_GROUP1_ZH.md"
        self.assertTrue(os.path.isfile(os.path.join(repo_root, guide_name)))

        script_path = os.path.join(launcher_root, "build_launcher.ps1")
        with open(script_path, "r", encoding="utf-8") as stream:
            script = stream.read()
        self.assertNotIn(guide_name, script)
        self.assertNotIn("TESTING_20260915_ZH.md", script)

    def test_windows_distribution_does_not_bundle_procdump(self):
        launcher_root = os.path.dirname(os.path.dirname(__file__))
        vendor_root = os.path.join(launcher_root, "vendor", "procdump")
        self.assertFalse(os.path.isfile(os.path.join(
            vendor_root, "procdump.exe")))
        self.assertFalse(os.path.isfile(os.path.join(
            vendor_root, "Eula.txt")))

        script_path = os.path.join(launcher_root, "build_launcher.ps1")
        with open(script_path, "r", encoding="utf-8") as stream:
            script = stream.read()
        self.assertNotIn("ProcDumpExecutable", script)
        self.assertNotIn("ProcDumpEula", script)
        self.assertNotIn("procdump.exe", script.lower())
        self.assertNotIn('"_internal\\tools\\Eula.txt"', script)

    def test_the_0_9_22_server_finds_its_client_modules(self):
        self.assertTrue(os.path.isfile(os.path.join(
            self.target, "0.9.22", "src", "res", "scripts", "client", "gui",
            "mods", "offline_lan_0922", "ai", "maps.py")))

    def test_the_0_9_22_server_stages_its_reward_module(self):
        self.assertTrue(os.path.isfile(os.path.join(
            self.target, "0.9.22", "server", "offline_rewards.py")))

    def test_the_0_9_22_server_stages_its_vehicle_overlay_store(self):
        self.assertTrue(os.path.isfile(os.path.join(
            self.target, "0.9.22", "server", "vehicle_overlay_store.py")))

    def test_the_navigation_graphs_stay_out_of_the_bundle(self):
        self.assertFalse(any(
            os.path.sep + "navgraphs" + os.path.sep in path
            for path in self.written))

    def test_the_server_stages_its_binary_wire_modules(self):
        for name in ("battle_wire_codec.py", "battle_wire_schema.py",
                     "battle_wire_replica.py"):
            self.assertTrue(os.path.isfile(os.path.join(
                self.target, "0.9.22", "server", name)), name)

    def test_ng_client_staging_carries_the_package_and_its_identity(self):
        source = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, source, True)
        package = os.path.join(source, "bigworld-client-win32")
        self._write_ng_client(package)
        target = os.path.join(source, "staged")

        stage_payload.stage_ng_client(
            target, package, identity="test-staging")

        for relative in stage_payload.NG_CLIENT_REQUIRED + (
                "offline-res/scripts/client/gui/mods/mod_offline.pyc",):
            self.assertTrue(os.path.isfile(
                os.path.join(target, *relative.split("/"))), relative)
        with open(os.path.join(target, *core.BUILD_IDENTITY_RELATIVE_PATH_0922
                               .split("/"))) as stream:
            identity = json.load(stream)
        self.assertEqual("test-staging", identity["buildIdentity"])
        self.assertEqual(
            stage_payload.launcher_version(), identity["semanticVersion"])
        self.assertIsNotNone(core._normalize_payload_identity(identity))

    def test_ng_client_debug_symbols_stay_out_of_the_payload(self):
        source = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, source, True)
        package = os.path.join(source, "bigworld-client-win32")
        self._write_ng_client(package)
        with open(os.path.join(package, "bwclient_h.PDB"), "w") as stream:
            stream.write("symbols")
        target = os.path.join(source, "staged")

        written = stage_payload.stage_ng_client(
            target, package, identity="test-staging")

        self.assertFalse(os.path.exists(os.path.join(target, "bwclient_h.PDB")))
        self.assertFalse(any(path.lower().endswith(".pdb") for path in written))
        self.assertTrue(os.path.isfile(os.path.join(target, "bwclient_h.exe")))

    def test_launcher_prepares_without_the_standalone_development_interpreter(self):
        source = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, source, True)
        package = os.path.join(source, "bigworld-client-win32")
        self._write_ng_client(package)
        entries = {
            "prepare-runtime/python.exe": "development interpreter",
            "prepare-runtime/python314.dll": "development runtime",
            "prepare-runtime/python314.zip": "development standard library",
            "Start-Client.cmd": "prepare-runtime\\python.exe",
            "engine-res/scripts/common/Lib/test/test_socket.py": "regression suite",
            "engine-res/scripts/common/Lib/idlelib/idle.py": "IDLE editor",
            "engine-res/scripts/client/DLLs/win64/_socket.pyd": "AMD64 extension",
            "engine-res/scripts/common/pydev/eclipse/pydev/pydevd.py": "IDE debugger",
            "engine-res/scripts/common/bw_site.py": "automatic startup entry",
            "engine-res/scripts/common/bwpydevd.py": "guarded debugger startup",
            "engine-res/scripts/common/Lib/string.py": "engine Python 2 library",
            "engine-res/scripts/common/Lib/unittest/__init__.py": "runtime library",
            "engine-res/scripts/client/DLLs/win32/_socket.pyd": "x86 extension",
            "engine-res/scripts/client/DLLs/win64_resources/data": "sibling resource",
            "offline-res/scripts/common/Lib/test/owned.py": "different resource owner",
            "engine-res/shaders/std_effects/shader.fx": "render resource",
            "engine-res/helpers/props/standin.model": "fallback model",
            "tools/inspect_client.py": "preparation dependency",
            "tools/prepare_native_client.py": (
                "import sys\n"
                "def prepare(client, package, overlays=()):\n"
                "    assert getattr(sys, 'frozen', False)\n"
                "    return {'executable': sys.executable, 'original': str(client), "
                "'overlay': [str(path) for path in overlays]}\n"),
        }
        for relative, text in entries.items():
            path = os.path.join(package, *relative.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as stream:
                stream.write(text)
        target = os.path.join(source, "staged")

        written = stage_payload.stage_ng_client(target, package, identity="test-staging")

        for relative in ("prepare-runtime", "Start-Client.cmd",
                "engine-res/scripts/common/Lib/test", "engine-res/scripts/common/Lib/idlelib",
                "engine-res/scripts/client/DLLs/win64", "engine-res/scripts/common/pydev"):
            self.assertFalse(os.path.exists(os.path.join(target, *relative.split("/"))))
            self.assertTrue(os.path.exists(os.path.join(package, *relative.split("/"))))
        retained = tuple(relative for relative in entries if not any(
            relative == excluded or relative.startswith(excluded + "/")
            for excluded in stage_payload.NG_CLIENT_EXCLUDED_RELATIVE_PATHS))
        for relative in stage_payload.NG_CLIENT_REQUIRED + retained:
            self.assertIn(os.path.join(target, *relative.split("/")), written)
        with mock.patch.object(sys, "frozen", True, create=True), \
                mock.patch.object(ng_client.subprocess, "Popen", side_effect=AssertionError("unexpected interpreter")):
            result = ng_client.prepare_instance(target, source, os.path.join(source, "overlay"))
        self.assertEqual(sys.executable, result["executable"])
        self.assertEqual(source, result["original"])
        self.assertEqual([os.path.join(source, "overlay")], result["overlay"])

    def test_editor_example_closure_and_authoring_tools_stay_in_the_development_package(self):
        source = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, source, True)
        package = os.path.join(source, "bigworld-client-win32")
        self._write_ng_client(package)
        retired = (
            "engine-res/helpers/spaces/pe/space.settings",
            "engine-res/helpers/flora/flora.xml",
            "engine-res/helpers/maps/lev_dirt01.tga",
            "engine-res/helpers/maps/lev_dirt02.tga",
            "engine-res/helpers/maps/lev_grass_01.tga",
            "engine-res/helpers/maps/lev_grassflowers.tga",
            "d3dx10_43.dll", "d3dx11_43.dll",
            "tools/prepare_native_static_models.py", "tools/prepare_native_vehicle.py",
            "tools/bake_navigation_0922.py", "tools/bake_destructibles_0922.py",
            "tools/bw_primitives_geometry.py", "tools/space_bin_0922.py",
            "tools/navigation_graph_schema.py", "tools/vendor/wot_space_bin_utils/space.py",
            "native/standalone_client/bigworld/res/resources.xml",
            "run-client.cmd", "README-FIRST.txt",
        )
        retained = (
            "engine-res/helpers/props/standin.model",
            "engine-res/helpers/maps/aid_builder.tga",
            "engine-res/helpers/maps/lev_dirt01_custom.tga",
            "engine-res/helpers/spaces/runtime/space.settings",
            "engine-res/system/data/flora.xml", "engine-res/system/fonts/default_medium.font",
            "engine-res/shaders/flora/flora.fx", "D3DX9_43.dll", "D3DCompiler_43.dll",
            "tools/inspect_client.py", "tools/packed_xml.py",
            "offline-res/helpers/flora/flora.xml",
            "mods/configs/offline_lan_0922/navgraphs/map.json",
            "mods/configs/offline_lan_0922/destructibles/map.json",
        )
        for relative in retired + retained:
            path = os.path.join(package, *relative.split("/"))
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as stream:
                stream.write(relative)
        target = os.path.join(source, "staged")

        written = stage_payload.stage_ng_client(target, package, identity="test-staging")

        for relative in retired:
            self.assertFalse(os.path.exists(os.path.join(target, *relative.split("/"))), relative)
            self.assertTrue(os.path.isfile(os.path.join(package, *relative.split("/"))), relative)
        for relative in stage_payload.NG_CLIENT_REQUIRED + retained:
            self.assertIn(os.path.join(target, *relative.split("/")), written)

    def test_an_incomplete_ng_client_package_is_rejected(self):
        source = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, source, True)
        package = os.path.join(source, "bigworld-client-win32")
        self._write_ng_client(package)
        os.unlink(os.path.join(package, "bwclient_h.exe"))

        self.assertRaises(ValueError, stage_payload.stage_ng_client,
                          os.path.join(source, "staged"), package)
        self.assertRaises(ValueError, stage_payload.stage_ng_client,
                          os.path.join(source, "staged"), None)

    def test_staging_replaces_an_earlier_payload(self):
        stale = os.path.join(self.root, "stale.txt")
        with open(stale, "w") as stream:
            stream.write("old")
        stage_payload.stage(self.root, include_clients=False)
        self.assertFalse(os.path.exists(stale))



class ServerImportTest(unittest.TestCase):
    """The bundle must carry every module the servers import.

    PyInstaller cannot see through ``runpy``, so a server import that is not
    declared in ``server_imports`` is missing from the packaged launcher.
    """

    ENTRY_POINTS = {
        core.PORT_0_9_22: ('server/windows_server.py',),
    }

    def setUp(self):
        root = os.path.join(tempfile.mkdtemp(), "payload")
        self.addCleanup(shutil.rmtree, os.path.dirname(root), True)
        stage_payload.stage(root, include_clients=False)
        self.payload = os.path.join(root, stage_payload.SERVER_DIR)

    def _module_file(self, root, name):
        relative = name.replace('.', os.path.sep)
        for candidate in (relative + '.py',
                          os.path.join(relative, '__init__.py')):
            path = os.path.join(root, candidate)
            if os.path.isfile(path):
                return path
        return None

    def _imports(self, path):
        with open(path, 'rb') as stream:
            tree = ast.parse(stream.read())
        names = set()
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                for alias in node.names:
                    names.add(alias.name)
            elif isinstance(node, ast.ImportFrom) and node.level == 0:
                if node.module:
                    names.add(node.module)
        return names

    def _closure(self, port_version):
        port_root = os.path.join(self.payload, port_version)
        search_roots = [port_root]
        if port_version == core.PORT_0_9_22:
            search_roots.append(os.path.join(port_root, 'server'))
            search_roots.append(os.path.join(
                port_root, 'src', 'res', 'scripts', 'client'))
        pending = [os.path.join(port_root, *entry.split('/'))
                   for entry in self.ENTRY_POINTS[port_version]]
        seen = set()
        external = set()
        while pending:
            path = pending.pop()
            if path in seen:
                continue
            seen.add(path)
            for name in self._imports(path):
                local = None
                for root in search_roots:
                    local = self._module_file(root, name)
                    if local is not None:
                        break
                if local is None:
                    external.add(name.split('.')[0])
                else:
                    pending.append(local)
        return external

    def test_every_server_import_is_declared(self):
        declared = set(server_imports.SERVER_STDLIB_MODULES)
        for port_version in core.SUPPORTED_PORTS:
            external = self._closure(port_version)
            required = {name for name in external
                        if name in sys.stdlib_module_names and
                        name != '__future__'}
            self.assertLessEqual(required, declared, port_version)

    def test_the_declared_modules_all_import(self):
        for name in server_imports.SERVER_STDLIB_MODULES:
            self.assertIn(name, sys.modules, name)


class ListenerTest(unittest.TestCase):
    class _ProtocolConnection(object):
        def __init__(self, reply_overrides=None):
            self.schema = core._battle_wire_schema()
            self.reply_overrides = dict(reply_overrides or {})
            self.reply = b""
            self.hello = None
            self.sent = []

        def settimeout(self, unused):
            pass

        def sendall(self, payload):
            hello = self.schema.decode(payload)
            self.sent.append(hello)
            if hello.get("type") == "leave":
                self.reply = b""
                return
            self.hello = hello
            reply = {
                "type": "welcome",
                "protocol": hello["protocol"],
                "client_build": hello["client_build"],
                "capabilities": hello.get("capabilities", []),
                "server_capabilities": [
                    "destructible_catalog_v5", "ram_contact_ledger_v2",
                    "human_ram_timeline_v1", "player_projectile_owner_v1",
                    "player_environment_v2", "effective_params_v1",
                    "ricochet_continuation_v1"],
            }
            reply.update(self.reply_overrides)
            self.reply = self.schema.encode(reply)

        def recv(self, size):
            reply, self.reply = self.reply[:size], self.reply[size:]
            return reply

        def close(self):
            pass

    def test_probe_reports_a_closed_port(self):
        def refuse(address, timeout):
            raise OSError("refused")

        self.assertFalse(core.probe_endpoint("127.0.0.1", 1, connect=refuse))

    def test_wait_returns_when_the_server_answers(self):
        attempts = []

        class Connection(object):
            def close(self):
                pass

        def connect(address, timeout):
            attempts.append(address)
            if len(attempts) < 3:
                raise OSError("not yet")
            return Connection()

        self.assertTrue(core.wait_for_listener(
            "127.0.0.1", 28782, timeout=5.0, connect=connect,
            clock=lambda: 0.0, sleep=lambda seconds: None))
        self.assertEqual(len(attempts), 3)

    def test_wait_gives_up_after_the_timeout(self):
        times = iter([0.0, 1.0, 2.0, 3.0])

        def connect(address, timeout):
            raise OSError("refused")

        self.assertFalse(core.wait_for_listener(
            "127.0.0.1", 28782, timeout=1.0, connect=connect,
            clock=lambda: next(times), sleep=lambda seconds: None))

    def test_protocol_probe_accepts_each_matching_server(self):
        for port_version in core.SUPPORTED_PORTS:
            self.assertTrue(core.probe_server_protocol(
                port_version, "127.0.0.1", 28782,
                connect=lambda address, timeout: self._ProtocolConnection()))

    def test_protocol_probe_rejects_an_unrelated_listener(self):
        connection = self._ProtocolConnection({"client_build": "wrong"})
        self.assertFalse(core.probe_server_protocol(
            core.PORT_0_9_22, "127.0.0.1", 28782,
            connect=lambda address, timeout: connection))

    def test_protocol_probe_rejects_a_pre_schema_5_server(self):
        connection = self._ProtocolConnection({"server_capabilities": []})
        self.assertFalse(core.probe_server_protocol(
            core.PORT_0_9_22, "127.0.0.1", 28782,
            connect=lambda address, timeout: connection))

    def test_0922_probe_uses_the_non_player_probe_role(self):
        connection = self._ProtocolConnection()

        self.assertTrue(core.probe_server_protocol(
            core.PORT_0_9_22, "127.0.0.1", 28782,
            connect=lambda address, timeout: connection))
        self.assertEqual("probe", connection.hello["role"])
        self.assertEqual("AA==", connection.hello["vehicle_compact_descr"])
        self.assertEqual("leave", connection.sent[-1]["type"])

    def test_protocol_probe_rejects_a_line_json_reply(self):
        class LineServer(self._ProtocolConnection):
            def sendall(self, payload):
                super(LineServer, self).sendall(payload)
                if self.reply:
                    self.reply = b'{"type": "welcome"}\n'

        self.assertFalse(core.probe_server_protocol(
            core.PORT_0_9_22, "127.0.0.1", 28792,
            connect=lambda address, timeout: LineServer()))

    def test_listener_status_distinguishes_protocol_from_raw_tcp(self):
        endpoint = lambda host, port, timeout=None: True
        compatible = lambda version, host, port, timeout=None: True
        incompatible = lambda version, host, port, timeout=None: False
        self.assertEqual(core.LISTENER_COMPATIBLE, core.listener_status(
            core.PORT_0_9_22, "127.0.0.1", 28782,
            endpoint_probe=endpoint, protocol_probe=compatible))
        self.assertEqual(core.LISTENER_OCCUPIED, core.listener_status(
            core.PORT_0_9_22, "127.0.0.1", 28782,
            endpoint_probe=endpoint, protocol_probe=incompatible))
        self.assertEqual(core.LISTENER_FREE, core.listener_status(
            core.PORT_0_9_22, "127.0.0.1", 28782,
            endpoint_probe=lambda host, port, timeout=None: False,
            protocol_probe=compatible))

    def test_wait_for_server_requires_the_protocol_probe(self):
        attempts = []

        def probe(port_version, host, port, timeout=None):
            attempts.append((port_version, host, port))
            return len(attempts) == 3

        self.assertTrue(core.wait_for_server(
            core.PORT_0_9_22, "127.0.0.1", 28782, timeout=5.0,
            probe=probe, clock=lambda: 0.0,
            sleep=lambda seconds: None))
        self.assertEqual(3, len(attempts))

    def test_probe_contracts_match_the_bundled_servers(self):
        def constants(path):
            with open(path, "rb") as stream:
                tree = ast.parse(stream.read())
            values = {}
            for statement in tree.body:
                if (isinstance(statement, ast.Assign) and
                        len(statement.targets) == 1 and
                        isinstance(statement.targets[0], ast.Name)):
                    try:
                        values[statement.targets[0].id] = ast.literal_eval(
                            statement.value)
                    except (TypeError, ValueError):
                        pass
            return values

        server0922 = constants(os.path.join(
            stage_payload.repository_root(), "server",
            "lan_battle_server.py"))
        self.assertEqual(server0922["PROTOCOL_VERSION"],
                         core._SERVER_PROBES[core.PORT_0_9_22]["protocol"])
        self.assertEqual(server0922["CLIENT_BUILD_0922"],
                         core._SERVER_PROBES[core.PORT_0_9_22]["client_build"])
        probe = core._SERVER_PROBES[core.PORT_0_9_22]
        for name in (
                "PROJECTILE_CAPABILITY",
                "DESTRUCTIBLE_CATALOG_V5_CAPABILITY",
                "RAM_CONTACT_LEDGER_CAPABILITY",
                "HUMAN_RAM_TIMELINE_CAPABILITY",
                "PLAYER_PROJECTILE_OWNER_CAPABILITY",
                "PLAYER_ENVIRONMENT_CAPABILITY",
                "RICOCHET_CONTINUATION_CAPABILITY"):
            self.assertIn(server0922[name], probe["capabilities"])
        self.assertIn("effective_params_v1", probe["capabilities"])
        for name in (
                "DESTRUCTIBLE_CATALOG_V5_CAPABILITY",
                "RAM_CONTACT_LEDGER_CAPABILITY",
                "HUMAN_RAM_TIMELINE_CAPABILITY",
                "PLAYER_PROJECTILE_OWNER_CAPABILITY",
                "PLAYER_ENVIRONMENT_CAPABILITY",
                "RICOCHET_CONTINUATION_CAPABILITY"):
            self.assertIn(server0922[name], probe["server_capabilities"])
        self.assertIn("effective_params_v1", probe["server_capabilities"])


class ConnectionReportTest(unittest.TestCase):
    def test_a_reachable_join_target_is_confirmed(self):
        self.assertIn("answered", core.connection_report(
            core.MODE_JOIN, "10.0.0.5", 28782, True))

    def test_an_unreachable_join_target_names_the_firewall(self):
        message = core.connection_report(core.MODE_JOIN, "10.0.0.5", 28782,
                                         False)
        self.assertIn("No answer from 10.0.0.5:28782", message)
        self.assertIn("firewall", message)

    def test_a_busy_port_warns_the_local_player(self):
        message = core.connection_report(core.MODE_SINGLE, core.LOCAL_HOST,
                                         28782, True)
        self.assertIn("already listens", message)

    def test_an_unrelated_listener_is_not_reported_as_the_server(self):
        message = core.listener_report(
            core.MODE_SINGLE, core.LOCAL_HOST, 28782,
            core.LISTENER_OCCUPIED)
        self.assertIn("Another program", message)

    def test_a_free_port_tells_the_host_what_happens_next(self):
        message = core.connection_report(core.MODE_SINGLE, core.LOCAL_HOST,
                                         28782, False)
        self.assertIn("Start game runs the server", message)


class LocalAddressTest(unittest.TestCase):
    def test_loopback_is_never_offered_to_other_players(self):
        self.assertEqual(
            core.local_addresses(lambda: ['127.0.0.1', '192.168.1.20']),
            ['192.168.1.20'])

    def test_duplicate_addresses_collapse(self):
        self.assertEqual(
            core.local_addresses(lambda: ['10.0.0.5', '10.0.0.5']),
            ['10.0.0.5'])

    def test_a_failed_lookup_reports_no_address(self):
        def fail():
            raise OSError('no name')

        self.assertEqual(core.local_addresses(fail), [])


class GameProcessTest(unittest.TestCase):
    """The client can restart itself once while it starts up."""

    class _Process(object):
        def __init__(self, states):
            self.states = list(states)

        def poll(self):
            if len(self.states) > 1:
                return self.states.pop(0)
            return self.states[0]

    class _TerminableProcess(object):
        def __init__(self):
            self.terminated = False
            self.killed = False

        def poll(self):
            return None

        def terminate(self):
            self.terminated = True

        def wait(self, timeout=None):
            return 1

        def kill(self):
            self.killed = True

    def test_native_process_enumerator_matches_the_image_name(self):
        names = ["explorer.exe", "WorldOfTanks.exe", "bwclient_h.exe"]

        self.assertTrue(core.game_is_running(enumerator=lambda: names))
        self.assertFalse(core.game_is_running(
            enumerator=lambda: ["WorldOfTanks.exe"]))
        self.assertFalse(core.game_is_running(
            executable="OtherGame.exe", enumerator=lambda: names))

    def test_restarted_game_is_the_exited_clients_own_child(self):
        rows = [(10, 1, "explorer.exe"), (20, 10, "bwclient_h.exe"),
                (30, 20, "procdump.exe"), (40, 20, "bwclient_h.exe"),
                (50, 99, "bwclient_h.exe")]

        self.assertEqual(
            core.restarted_game_pid(20, enumerator=lambda: rows), 40)
        self.assertIsNone(
            core.restarted_game_pid(40, enumerator=lambda: rows))

        def fail():
            raise OSError("process snapshot is unavailable")

        self.assertIsNone(core.restarted_game_pid(20, enumerator=fail))

    def test_failed_native_process_enumerator_reports_the_game_as_gone(self):
        def fail():
            raise OSError("process snapshot is unavailable")

        self.assertFalse(core.game_is_running(enumerator=fail))

    def test_missing_window_does_not_latch_paired_player_as_closed(self):
        process = self._Process([None, None, 0])

        self.assertEqual(
            (0, False),
            core.wait_for_paired_player_exit(
                process, "/game", window_visible=lambda: False,
                close_grace=1.0, poll=1.0,
                sleep=lambda unused: None))

    def test_paired_player_ignores_window_loss_until_the_job_exits(self):
        process = self._Process([None, None, None, None, 0])
        visibility_checks = []
        stop = mock.Mock()

        self.assertEqual(
            (0, False),
            core.wait_for_paired_player_exit(
                process, "/game",
                window_visible=lambda: visibility_checks.append(True),
                close_grace=2.0, poll=1.0,
                clock=lambda: 999.0, stop_process=stop,
                shutdown_timeout=45.0,
                sleep=lambda unused: None))
        self.assertEqual([], visibility_checks)
        stop.assert_not_called()

    def test_paired_player_retires_when_required_worker_exits(self):
        process = self._TerminableProcess()
        worker = self._Process([None, 7])

        self.assertEqual(
            (1, True),
            core.wait_for_paired_player_exit(
                process, "/game", required_process=worker,
                window_visible=lambda: True, poll=1.0,
                clock=lambda: 0.0, sleep=lambda unused: None))
        self.assertTrue(process.terminated)
        self.assertFalse(process.killed)

    def test_the_wait_ends_after_a_quiet_grace_period(self):
        ticks = iter([0.0, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0])
        self.assertFalse(core.wait_for_game_exit(
            lambda: False, grace=3.0, poll=1.0,
            clock=lambda: next(ticks), sleep=lambda seconds: None))

    def test_a_restarted_game_keeps_the_wait_open(self):
        seen = []
        running = [True, True, False, False, False, False]
        ticks = iter([float(index) for index in range(20)])

        def is_running():
            return running.pop(0) if running else False

        self.assertTrue(core.wait_for_game_exit(
            is_running, on_restart=lambda: seen.append(1), grace=2.0, poll=1.0,
            clock=lambda: next(ticks), sleep=lambda seconds: None))
        self.assertEqual([1], seen)

    def test_shutdown_waits_for_terminated_processes_to_disappear(self):
        running = [True, True, False]
        ticks = iter([0.0, 0.0, 0.1])
        sleeps = []

        self.assertTrue(core.wait_for_game_shutdown(
            is_running=lambda: running.pop(0), timeout=1.0, poll=0.1,
            clock=lambda: next(ticks), sleep=sleeps.append))
        self.assertEqual([0.1, 0.1], sleeps)

    def test_shutdown_wait_is_bounded(self):
        ticks = iter([0.0, 1.0])

        self.assertFalse(core.wait_for_game_shutdown(
            is_running=lambda: True, timeout=1.0, poll=0.1,
            clock=lambda: next(ticks), sleep=lambda unused: None))

    def test_worker_loader_failure_explains_unsigned_and_signed_status(self):
        for code in (0xc0000135, -1073741515):
            hint = core.worker_startup_exit_hint(code)
            self.assertIn("0xC0000135", hint)
            self.assertIn("x86", hint)
            self.assertIn("does not identify", hint)
        for code in (None, 0, 3, 1):
            self.assertEqual("", core.worker_startup_exit_hint(code))

    def test_worker_ready_needs_its_marker_and_the_server_admission(self):
        instance = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, instance, True)
        marker = ng_client.worker_ready_marker(instance)
        joined = []
        attempts = []

        def sleep(unused):
            attempts.append(1)
            if len(attempts) == 1:
                with open(marker, "w") as stream:
                    stream.write("ready")
            else:
                joined.append(True)

        self.assertTrue(ng_client.wait_for_worker_ready(
            self._Process([None]), instance, lambda: bool(joined),
            timeout=1.0, interval=0.1, clock=lambda: 0.0, sleep=sleep))
        self.assertEqual([1, 1], attempts)

    def test_a_previous_worker_marker_is_cleared_before_a_start(self):
        instance = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, instance, True)
        with open(ng_client.worker_ready_marker(instance), "w") as stream:
            stream.write("ready")

        ng_client.clear_markers(instance)

        self.assertFalse(ng_client.worker_is_ready(instance, lambda: True))

    def test_worker_exit_or_cancellation_rejects_readiness(self):
        instance = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, instance, True)
        with open(ng_client.worker_ready_marker(instance), "w") as stream:
            stream.write("ready")
        self.assertFalse(ng_client.wait_for_worker_ready(
            self._Process([9]), instance, lambda: True, clock=lambda: 0.0,
            sleep=lambda unused: None))
        self.assertFalse(ng_client.wait_for_worker_ready(
            self._Process([None]), instance, lambda: True,
            cancelled=lambda: True, clock=lambda: 0.0,
            sleep=lambda unused: None))


class KnownFolderTest(unittest.TestCase):
    def test_a_folder_moves_to_the_top_without_duplicates(self):
        folders = core.remember_folder([], os.path.join("C:", "Games", "WoT"))
        folders = core.remember_folder(folders, os.path.join("D:", "WoT922"))
        folders = core.remember_folder(folders, os.path.join("C:", "Games",
                                                             "WoT"))
        self.assertEqual([os.path.join("C:", "Games", "WoT"),
                          os.path.join("D:", "WoT922")], folders)

    def test_the_list_stays_bounded(self):
        folders = []
        for index in range(15):
            folders = core.remember_folder(folders, "/games/wot%d" % index,
                                           limit=4)
        self.assertEqual(4, len(folders))
        self.assertEqual(os.path.normpath("/games/wot14"), folders[0])

    def test_an_empty_folder_is_ignored(self):
        self.assertEqual(["/games/wot"],
                         core.remember_folder(["/games/wot"], "   "))

    def test_discovery_finds_a_game_beside_the_common_roots(self):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root, True)
        for name in ("World_of_Tanks_Custom", "Some Other Game"):
            os.makedirs(os.path.join(root, name))
        with open(os.path.join(root, "World_of_Tanks_Custom",
                               core.GAME_EXECUTABLE), "w") as stream:
            stream.write("")
        self.assertEqual(
            [os.path.join(root, "World_of_Tanks_Custom")],
            core.discover_game_folders(roots=(root,)))

    def test_discovery_accepts_a_root_that_is_the_game(self):
        root = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, root, True)
        with open(os.path.join(root, core.GAME_EXECUTABLE), "w") as stream:
            stream.write("")
        self.assertEqual([root], core.discover_game_folders(roots=(root,)))

    def test_discovery_survives_a_missing_root(self):
        self.assertEqual([], core.discover_game_folders(
            roots=("/nonexistent-root",)))

    def test_remembered_folders_come_before_discovered_ones(self):
        folders = core.known_folders(
            {"folders": ["/games/remembered"]},
            discovered=["/games/discovered", "/games/remembered"])
        self.assertEqual([os.path.normpath("/games/remembered"),
                          os.path.normpath("/games/discovered")], folders)


class LauncherSettingsTest(unittest.TestCase):
    def test_settings_round_trip(self):
        directory = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, directory, True)
        path = os.path.join(directory, "launcher.json")
        self.assertTrue(core.save_settings({"mode": core.MODE_SINGLE}, path))
        self.assertEqual(core.load_settings(path), {"mode": core.MODE_SINGLE})

    def test_missing_settings_are_empty(self):
        self.assertEqual(core.load_settings("/nonexistent/launcher.json"), {})


class VehicleOverlayFetchTest(unittest.TestCase):
    def test_joining_another_rooms_vehicle_data_is_unsupported(self):
        with self.assertRaisesRegex(core.LauncherError, "not supported"):
            core.fetch_vehicle_overlay("127.0.0.1", 28792)


class ResetStateNameTests(unittest.TestCase):
    """A confirmed reset removes copies of earned progress too."""

    def test_the_clients_rotated_and_quarantined_copies_are_reset(self):
        for name in ("garage_state.json", "garage_state.json.tmp",
                     "garage_state.backup1.json",
                     "garage_state.rejected-20260908-000000-000.json",
                     "garage_state.shrunk-20260908-000000-000.json",
                     "postbattle_state.backup2.json"):
            self.assertTrue(core._reset_state_name(name), name)

    def test_files_that_are_not_this_mods_state_are_left_alone(self):
        for name in ("garage_state", "vehicle_profiles.json", "python.log",
                     "garage_state.json.zip", "other_state.backup1.json"):
            self.assertFalse(core._reset_state_name(name), name)


class WorkerClientPathsTest(unittest.TestCase):
    """A hidden worker starts from its own instance and user data."""

    def setUp(self):
        self.work = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.work, True)
        environment = mock.patch.dict(os.environ, {
            ng_client.DATA_ROOT_ENV: os.path.join(self.work, "ng")})
        environment.start()
        self.addCleanup(environment.stop)
        self.game = os.path.join(self.work, "ng", "instances", "player")

    def test_worker_clears_inherited_resource_overrides(self):
        inherited = {"BW_RES_PATH": "old-worker-res",
                     "WOT_OFFLINE_WORKER_RES_PATH": "old-worker-res"}
        environment = core.worker_environment(
            self.game, "192.168.1.3", 28783, environment=inherited)
        self.assertNotIn("BW_RES_PATH", environment)
        self.assertNotIn("WOT_OFFLINE_WORKER_RES_PATH", environment)
        self.assertEqual("192.168.1.3",
                         environment[core.CLIENT_SERVER_HOST_ENV_0922])
        self.assertEqual("28783",
                         environment[core.CLIENT_SERVER_PORT_ENV_0922])
        self.assertEqual("old-worker-res", inherited["BW_RES_PATH"])

    def test_worker_starts_once_from_its_instance(self):
        import threading
        import wot_launcher

        worker_root = core.worker_root(self.game)
        for ready in (True, False):
            with self.subTest(ready=ready):
                window = object.__new__(wot_launcher.LauncherWindow)
                window._active_report_session = None
                window._stop_requested = False
                window._stop_requested_roles = set()
                window._worker_joined = threading.Event()
                window._log = mock.Mock()
                window._log_worker_failure = mock.Mock()
                window._observe_process_exit = mock.Mock(return_value=None)
                window._stop_worker = mock.Mock()
                window._start_dump_monitor = mock.Mock()
                window._crash_capture_environment = lambda value, role: value
                with mock.patch.object(wot_launcher.os.path, "isfile", return_value=True), \
                        mock.patch.object(ng_client, "clear_markers") as clear, \
                        mock.patch.object(ng_client, "wait_for_worker_ready", return_value=ready) as wait, \
                        mock.patch.object(wot_launcher.subprocess, "Popen") as spawn:
                    self.assertEqual(ready, window._start_worker(
                        self.game, "127.0.0.1", 28792))
                self.assertEqual(1, spawn.call_count)
                self.assertEqual(worker_root, spawn.call_args.kwargs["cwd"])
                self.assertEqual(
                    core.worker_child_command(self.game),
                    spawn.call_args.args[0])
                clear.assert_called_once_with(worker_root)
                self.assertEqual(worker_root, wait.call_args.args[1])
                self.assertEqual(window._worker_joined.is_set,
                                 wait.call_args.args[2])
                if ready:
                    window._stop_worker.assert_not_called()
                else:
                    window._stop_worker.assert_called_once_with(room_owned=False)


class ExitCodeDescriptionTest(unittest.TestCase):
    def test_a_missing_runtime_dll_reads_as_one(self):
        text = core.describe_exit_code(3221225781)
        self.assertIn("0xC0000135", text)
        self.assertIn("DirectX 9", text)

    def test_an_abort_names_the_client_fatal_error(self):
        self.assertIn("abort", core.describe_exit_code(3))

    def test_an_ordinary_code_is_left_alone(self):
        self.assertEqual("0", core.describe_exit_code(0))
        self.assertEqual("7", core.describe_exit_code(7))

    def test_an_unlisted_windows_status_is_still_named_as_one(self):
        self.assertIn("Windows fatal status",
                      core.describe_exit_code(0xC0000409))
