"""Run the independent (NG) client instead of the original WorldOfTanks.exe.

The original #1513 installation stays read-only: each NG instance mounts its
packages through ``paths.xml``. The NG client keeps ``preferences.xml``,
``mods/configs``, logs and replays beside its executable, so the visible
player and the hidden worker each run from their own instance directory.

Every launcher-owned file lives below one data root that no retail-client
mod reads:

``<LOCALAPPDATA>/WoT-Offline-NG/``
    ``user-data/``       APPDATA/LOCALAPPDATA of the launcher and the player
                         (launcher settings, saves, shop, vehicle profiles)
    ``worker-data/``     APPDATA/LOCALAPPDATA of the hidden worker
    ``instances/player`` the visible client and the vehicle-data overlay
    ``instances/worker`` the hidden simulation worker
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import time

# The packaged preparation tool runs inside the frozen launcher; PyInstaller
# only bundles the standard-library modules it can see imported here.
import argparse  # noqa: F401
import datetime  # noqa: F401
import hashlib  # noqa: F401
import io  # noqa: F401
import logging  # noqa: F401
import pathlib  # noqa: F401
import re  # noqa: F401
import tempfile  # noqa: F401
import xml.etree.ElementTree  # noqa: F401
import zipfile  # noqa: F401

EXECUTABLE = "bwclient_h.exe"
ORIGINAL_EXECUTABLE = "WorldOfTanks.exe"
PLAYER = "player"
WORKER = "worker"
ROLES = (PLAYER, WORKER)
DATA_DIR_NAME = "WoT-Offline-NG"
DATA_ROOT_ENV = "WOT_OFFLINE_NG_DATA_ROOT"
ORIGINAL_APPDATA_ENV = "WOT_OFFLINE_NG_ORIGINAL_APPDATA"
ORIGINAL_LOCALAPPDATA_ENV = "WOT_OFFLINE_NG_ORIGINAL_LOCALAPPDATA"
PACKAGE_ENV = "WOT_OFFLINE_NG_CLIENT_PACKAGE"
PACKAGE_PAYLOAD_DIR = "ng-client"
INSTANCE_MANIFEST = "ng-instance.json"
ORIGINAL_BINDING = "ng-original-client.json"
OVERLAY_RELATIVE = "res_mods/0.9.22.0.1"
WORKER_READY_MARKER = "worker-internal.ready"
PLAYER_READY_MARKER = "player.ready"
PLAYER_FINISHED_MARKER = "player.finished"
PYTHON_LOG = "python.log"
WORKER_JOIN_LOG = "WORKER JOIN"
WORKER_READY_TIMEOUT_SECONDS = 180.0
PROCDUMP_ATTACH_SECONDS = 10.0

PLAYER_ARGUMENTS = ("-noConversion",)
WORKER_ARGUMENTS = ("-noConversion", "-unattended")
WORKER_INTERNAL_READY_ENV = "OFFLINE_LAN_0922_WORKER_INTERNAL_READY_MARKER"
PLAYER_READY_ENV = "OFFLINE_LAN_0922_PLAYER_READY_MARKER"
PLAYER_FINISHED_ENV = "OFFLINE_LAN_0922_PLAYER_FINISHED_MARKER"
SIMULATION_WORKER_MODE = "simulation_worker"
_CLEARED_ENV_PREFIXES = ("OFFLINE_LAN_0922_", "WOT_OFFLINE_", "WOT_PRIVATE_")
# Callers add session values (language, crash capture, replay) afterwards.
_KEPT_ENV = (DATA_ROOT_ENV, ORIGINAL_APPDATA_ENV, ORIGINAL_LOCALAPPDATA_ENV)

# Written by preparation or by the running client; never package content.
_GENERATED = frozenset((
    "paths.xml", "app_type.xml", "original-client.json", "prepare.log",
    "standalone.log", PYTHON_LOG, INSTANCE_MANIFEST, ORIGINAL_BINDING,
    WORKER_READY_MARKER, PLAYER_READY_MARKER, PLAYER_FINISHED_MARKER,
    "preferences.xml"))
_GENERATED_DIRS = frozenset(("logs", "replays", "res_mods", "user-data"))
CONFIG_RELATIVE = "mods/configs/offline_lan_0922/config.json"
# The client fills every absent key from its own defaults. Seeding only the
# schema lets the launcher write the player name and save slot before the
# first client start, as the original package's shipped config.json did.
_SEEDED_CONFIG = {"schema": 3}
# The user's own copy wins once it exists.
_KEEP_IF_PRESENT = frozenset((CONFIG_RELATIVE,))
_REQUIRED = (EXECUTABLE, "offline-res.json", "tools/prepare_native_client.py")


class NGClientError(Exception):
    """A user-correctable NG client failure."""


def _original_environment_value(environment, name, original_name):
    return environment.get(original_name) or environment.get(name) or ""


def default_data_root(environment=None):
    environment = os.environ if environment is None else environment
    base = (_original_environment_value(
                environment, "LOCALAPPDATA", ORIGINAL_LOCALAPPDATA_ENV) or
            _original_environment_value(
                environment, "APPDATA", ORIGINAL_APPDATA_ENV) or
            os.path.join(os.path.expanduser("~"), ".config"))
    return os.path.join(base, DATA_DIR_NAME)


def data_root(environment=None):
    environment = os.environ if environment is None else environment
    return environment.get(DATA_ROOT_ENV) or default_data_root(environment)


def user_data_paths(role, root=None):
    """Return (APPDATA, LOCALAPPDATA) for one NG role."""
    if role not in ROLES:
        raise NGClientError("Unknown NG client role: %s" % role)
    base = os.path.join(root or data_root(),
                        "user-data" if role == PLAYER else "worker-data")
    return os.path.join(base, "Roaming"), os.path.join(base, "Local")


def isolate_environment(environment=None):
    """Point this launcher's APPDATA at the NG data root, once.

    Settings, saves, the shop, vehicle profiles and reports derive their paths
    from APPDATA/LOCALAPPDATA. Redirecting them before any of those is read
    keeps all NG state apart from the original-mod launcher.
    """
    environment = os.environ if environment is None else environment
    root = environment.get(DATA_ROOT_ENV) or default_data_root(environment)
    if ORIGINAL_APPDATA_ENV in environment:
        return root
    environment[ORIGINAL_APPDATA_ENV] = environment.get("APPDATA", "")
    environment[ORIGINAL_LOCALAPPDATA_ENV] = environment.get("LOCALAPPDATA", "")
    roaming, local = user_data_paths(PLAYER, root)
    for path in (roaming, local):
        os.makedirs(path, exist_ok=True)
    environment["APPDATA"] = roaming
    environment["LOCALAPPDATA"] = local
    environment[DATA_ROOT_ENV] = root
    return root


def instance_root(role, root=None):
    if role not in ROLES:
        raise NGClientError("Unknown NG client role: %s" % role)
    return os.path.join(root or data_root(), "instances", role)


def sibling_instance(player_root, role):
    """Return one role's instance beside a player instance."""
    return os.path.join(os.path.dirname(os.path.abspath(player_root)), role)


def overlay_root(player_root):
    return os.path.join(os.path.abspath(player_root), *OVERLAY_RELATIVE.split("/"))


def package_root(base_dir=None, environment=None):
    """Return the bundled NG client package, or None when absent."""
    environment = os.environ if environment is None else environment
    base = base_dir or getattr(sys, "_MEIPASS", None)
    if base:
        candidate = os.path.join(base, PACKAGE_PAYLOAD_DIR)
    else:
        candidate = environment.get(PACKAGE_ENV, "")
    return candidate if candidate and os.path.isdir(candidate) else None


def _read_json(path):
    try:
        with open(path, "rb") as stream:
            value = json.load(stream)
    except (IOError, OSError, ValueError):
        return None
    return value if isinstance(value, dict) else None


def _write_json(path, value):
    directory = os.path.dirname(path)
    os.makedirs(directory, exist_ok=True)
    temporary = path + ".tmp"
    with open(temporary, "wb") as stream:
        stream.write((json.dumps(value, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    os.replace(temporary, path)


def package_identity(package):
    """Identify the gameplay pin and native binary one package carries."""
    runtime = _read_json(os.path.join(package, "offline-res.json")) or {}
    build = _read_json(os.path.join(
        package, "mods", "configs", "offline_lan_0922", "build_identity.json")) or {}
    try:
        stat = os.stat(os.path.join(package, EXECUTABLE))
        native = [stat.st_size, int(stat.st_mtime)]
    except OSError:
        native = None
    return {"offlineRevision": runtime.get("revision"),
            "buildIdentity": build.get("buildIdentity"), "executable": native}


def is_instance(root):
    return bool(root) and os.path.isfile(os.path.join(root, ORIGINAL_BINDING))


def bind_original_client(instance, original):
    """Record which original installation this instance mounts."""
    original = os.path.abspath(original)
    path = os.path.join(instance, ORIGINAL_BINDING)
    current = _read_json(path) or {}
    if current.get("originalClient") != original:
        _write_json(path, {"schema": 1, "originalClient": original})
    return path


def original_client_root(root):
    """Map an NG instance to its original installation; other roots stay."""
    record = _read_json(os.path.join(root or "", ORIGINAL_BINDING))
    if record is not None and isinstance(record.get("originalClient"), str):
        return record["originalClient"]
    return root


def player_instance(original, root=None):
    """Map a selected original client folder to the player instance.

    The selection is bound to the instance so every later read uses the
    original installation while every write stays in the instance. A folder
    without the original executable is returned unchanged for diagnosis.
    """
    if (not original or is_instance(original) or
            not os.path.isfile(os.path.join(original, ORIGINAL_EXECUTABLE))):
        return original
    player = instance_root(PLAYER, root)
    bind_original_client(player, original)
    return player


def _package_files(package):
    files = {}
    for directory, directories, names in os.walk(package):
        relative_directory = os.path.relpath(directory, package)
        top = relative_directory == "."
        directories[:] = sorted(
            name for name in directories
            if name != "__pycache__" and not os.path.islink(os.path.join(directory, name))
            and not (top and name in _GENERATED_DIRS))
        for name in sorted(names):
            if top and name in _GENERATED:
                continue
            source = os.path.join(directory, name)
            if os.path.islink(source):
                continue
            relative = name if top else "/".join(
                relative_directory.split(os.sep) + [name])
            files[relative] = source
    return files


def _copy_file(source, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    temporary = target + ".ng-copy"
    shutil.copy2(source, temporary)
    os.replace(temporary, target)


def sync_instance(package, instance):
    """Copy the package into one instance, preserving user-owned state.

    Only package-owned paths are written or removed. Preferences, saves,
    logs, replays, the user's config.json and the vehicle overlay survive.
    """
    for required in _REQUIRED:
        if not os.path.isfile(os.path.join(package, *required.split("/"))):
            raise NGClientError("The NG client package is missing %s." % required)
    os.makedirs(instance, exist_ok=True)
    manifest_path = os.path.join(instance, INSTANCE_MANIFEST)
    record = _read_json(manifest_path) or {}
    previous = record.get("files") or {}
    identity = package_identity(package)
    # A different build may reuse a file's size and timestamp, for example
    # after ZIP extraction; only an unchanged package may skip a copy.
    same_package = record.get("package") == identity
    files = _package_files(package)
    recorded = {}
    copied = 0
    for relative, source in files.items():
        target = os.path.join(instance, *relative.split("/"))
        stat = os.stat(source)
        signature = [stat.st_size, stat.st_mtime_ns]
        recorded[relative] = signature
        if relative in _KEEP_IF_PRESENT and os.path.isfile(target):
            continue
        if (same_package and previous.get(relative) == signature and
                os.path.isfile(target) and
                os.path.getsize(target) == stat.st_size):
            continue
        _copy_file(source, target)
        copied += 1
    removed = 0
    for relative in sorted(set(previous) - set(files)):
        if relative in _KEEP_IF_PRESENT:
            continue
        target = os.path.join(instance, *relative.split("/"))
        if os.path.isfile(target) and not os.path.islink(target):
            os.unlink(target)
            removed += 1
    config = os.path.join(instance, *CONFIG_RELATIVE.split("/"))
    if not os.path.exists(config):
        _write_json(config, _SEEDED_CONFIG)
    _write_json(manifest_path, {"schema": 1, "package": identity,
                                "files": recorded})
    return copied, removed


def instance_current(instance, package):
    record = _read_json(os.path.join(instance, INSTANCE_MANIFEST))
    return bool(record) and record.get("package") == package_identity(package)


def _load_preparation(instance):
    import importlib.util

    tools = os.path.join(instance, "tools")
    if tools not in sys.path:
        sys.path.insert(0, tools)
    spec = importlib.util.spec_from_file_location(
        "ng_prepare_native_client", os.path.join(tools, "prepare_native_client.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def prepare_instance(instance, original, overlay):
    """Mount the original packages, after the launcher's vehicle overlay."""
    from pathlib import Path

    os.makedirs(overlay, exist_ok=True)
    try:
        return _load_preparation(instance).prepare(
            Path(original), Path(instance), overlays=[Path(overlay)])
    except (OSError, ValueError, KeyError) as error:
        raise NGClientError("The NG client could not mount %s: %s" % (original, error))


def install(original, player, base_dir=None):
    """Refresh both instances from the bundled package and report the actions."""
    package = package_root(base_dir)
    if package is None:
        raise NGClientError("This launcher carries no NG client package.")
    overlay = overlay_root(player)
    actions = ["NG client package: %s." % json.dumps(
        package_identity(package), sort_keys=True)]
    for role in ROLES:
        instance = sibling_instance(player, role)
        copied, removed = sync_instance(package, instance)
        prepare_instance(instance, original, overlay)
        bind_original_client(instance, original)
        actions.append("Prepared the NG %s in %s (%d updated, %d removed file(s))." % (
            role, instance, copied, removed))
    return actions


def _child_environment(environment, role, host, port):
    environment = dict(os.environ if environment is None else environment)
    for name in list(environment):
        if name.upper().startswith(_CLEARED_ENV_PREFIXES) and name not in _KEPT_ENV:
            environment.pop(name)
    environment.pop("BW_RES_PATH", None)
    roaming, local = user_data_paths(role, environment.get(DATA_ROOT_ENV) or None)
    for path in (roaming, local):
        os.makedirs(path, exist_ok=True)
    environment["APPDATA"] = roaming
    environment["LOCALAPPDATA"] = local
    environment["OFFLINE_LAN_0922_ALLOW_MULTIPLE_CLIENTS"] = "1"
    environment["OFFLINE_LAN_0922_SERVER_HOST"] = str(host)
    environment["OFFLINE_LAN_0922_SERVER_PORT"] = str(int(port))
    return environment


def player_environment(instance, host, port, environment=None, preferred_team=0):
    environment = _child_environment(environment, PLAYER, host, port)
    environment["OFFLINE_LAN_0922_CLIENT_MODE"] = PLAYER
    environment["OFFLINE_LAN_0922_PREFERRED_TEAM"] = str(int(preferred_team))
    environment[PLAYER_READY_ENV] = os.path.join(instance, PLAYER_READY_MARKER)
    environment[PLAYER_FINISHED_ENV] = os.path.join(instance, PLAYER_FINISHED_MARKER)
    return environment


def worker_environment(instance, host, port, environment=None):
    environment = _child_environment(environment, WORKER, host, port)
    environment["OFFLINE_LAN_0922_CLIENT_MODE"] = SIMULATION_WORKER_MODE
    environment[WORKER_INTERNAL_READY_ENV] = worker_ready_marker(instance)
    return environment


def executable(instance):
    return os.path.join(instance, EXECUTABLE)


def player_command(instance):
    return [executable(instance)] + list(PLAYER_ARGUMENTS)


def worker_command(instance):
    return [executable(instance)] + list(WORKER_ARGUMENTS)


def worker_ready_marker(instance):
    return os.path.join(instance, WORKER_READY_MARKER)


def clear_markers(instance):
    for name in (WORKER_READY_MARKER, PLAYER_READY_MARKER, PLAYER_FINISHED_MARKER):
        for path in (os.path.join(instance, name), os.path.join(instance, name + ".tmp")):
            try:
                os.unlink(path)
            except FileNotFoundError:
                pass


def hidden_startup_options():
    """Return Popen options that keep the worker off the player's desktop."""
    if os.name != "nt":
        return {}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0  # SW_HIDE
    return {"startupinfo": startup,
            "creationflags": getattr(subprocess, "CREATE_NO_WINDOW", 0)}


def worker_is_ready(instance, server_joined):
    """The worker owns readiness only after the server admitted it."""
    try:
        with open(worker_ready_marker(instance), "rb") as stream:
            published = stream.read(64).strip() == b"ready"
    except (IOError, OSError):
        return False
    return published and bool(server_joined())


def wait_for_worker_ready(process, instance, server_joined,
                          timeout=WORKER_READY_TIMEOUT_SECONDS, interval=0.1,
                          clock=None, sleep=None, cancelled=None):
    clock = clock or time.monotonic
    sleep = sleep or time.sleep
    deadline = clock() + float(timeout)
    while True:
        if callable(cancelled) and cancelled():
            return False
        if process.poll() is not None:
            return False
        if worker_is_ready(instance, server_joined):
            return process.poll() is None
        if clock() >= deadline:
            return False
        sleep(interval)


def procdump_monitor_command(procdump, process_id, dump_path, full_memory=False):
    """Write one dump when the process raises or terminates (as the starter did)."""
    return [procdump, "-accepteula", "-ma" if full_memory else "-mm",
            "-n", "1", "-e", "-t", str(int(process_id)), dump_path]


def procdump_cancel_command(procdump, process_id):
    return [procdump, "-accepteula", "-cancel", str(int(process_id))]
