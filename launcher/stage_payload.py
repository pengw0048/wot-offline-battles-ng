#!/usr/bin/env python3
"""Copy the native worker-host and player package the launcher carries."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import time
import uuid

NG_CLIENT_DIR = "ng-client"
LAUNCHER_CONTRACTS_DIR = "launcher-contracts"
# Existing launcher editors and save tools use these Python-2-compatible
# data helpers. Keep them separate from the native room's battle authority.
LAUNCHER_CONTRACTS = (
    "__init__.py", "bot_editor_maps.py", "bot_gunnery.py", "bot_tactics.py",
    "bot_tactics_runtime.py", "config.py", "crew_recruits.py",
    "crew_seat_data.py", "crew_skills.py", "initial_allocation.py",
    "internal_layout_console.py", "native_input_diagnostics.py", "native_math.py",
    "offline_services.py", "price_catalogue.py", "route_network.py",
    "spg_position_data.py", "spg_positions.py", "worker_diagnostics.py",
    "account_rpc/__init__.py", "account_rpc/economy.py",
)
LAUNCHER_CONTRACTS_RELATIVE = "gui/mods/offline_lan_0922"

# Files every NG client package must carry before the launcher can run it.
NG_CLIENT_REQUIRED = (
    "bwclient_h.exe",
    "offline-res.json",
    "tools/prepare_native_client.py",
)
NG_CLIENT_EXCLUDED_SUFFIXES = (".pdb",)
# The launcher prepares resources in its own frozen Python runtime. Keep the
# development interpreter, stdlib tests/IDLE and unused platform/debugger tools
# in the independently runnable native development ZIP, outside the launcher.
NG_CLIENT_EXCLUDED_RELATIVE_PATHS = (
    "prepare-runtime",
    "Start-Client.cmd",
    "engine-res/scripts/common/Lib/test",
    "engine-res/scripts/common/Lib/idlelib",
    "engine-res/scripts/client/DLLs/win64",
    "engine-res/scripts/common/pydev",
    # The particle editor's example space owns this flora/terrain closure.
    # The client uses the map's flora or system/data/flora.xml instead.
    "engine-res/helpers/spaces/pe",
    "engine-res/helpers/flora",
    "engine-res/helpers/maps/lev_dirt01.tga",
    "engine-res/helpers/maps/lev_dirt02.tga",
    "engine-res/helpers/maps/lev_grass_01.tga",
    "engine-res/helpers/maps/lev_grassflowers.tga",
    # Direct3D 9 is the supported renderer; retain D3DX9 and its compiler.
    "d3dx10_43.dll",
    "d3dx11_43.dll",
    # Product preparation mounts owned #1513 packages in the launcher process.
    # Authoring/baking and direct development entry points stay in the dev ZIP.
    "tools/prepare_native_static_models.py",
    "tools/prepare_native_vehicle.py",
    "tools/bake_navigation_0922.py",
    "tools/bake_destructibles_0922.py",
    "tools/bw_primitives_geometry.py",
    "tools/space_bin_0922.py",
    "tools/navigation_graph_schema.py",
    "tools/vendor",
    "native/standalone_client/bigworld/res",
    "run-client.cmd",
    "README-FIRST.txt",
)
BUILD_IDENTITY_RELATIVE = "mods/configs/offline_lan_0922/build_identity.json"
BUILD_IDENTITY_ENV = "WOT_OFFLINE_BUILD_IDENTITY"
BUILD_IDENTITY_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,95}$")
_LAUNCHER_VERSION = re.compile(r'^LAUNCHER_VERSION = "([^"]+)"$', re.M)


def repository_root():
    return os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _copy_file(source, target):
    directory = os.path.dirname(target)
    if not os.path.isdir(directory):
        os.makedirs(directory)
    shutil.copy2(source, target)


def _copy_tree(source, target, keep_bytecode=False, suffixes=None,
               excluded_suffixes=(), excluded_relative_paths=()):
    source = os.fspath(source)
    written = []
    for directory, directories, names in os.walk(source):
        relative_directory = os.path.relpath(directory, source)
        def relative_child(name):
            relative = name if relative_directory == os.curdir else os.path.join(relative_directory, name)
            return relative.replace(os.sep, "/")
        directories[:] = sorted(
            name for name in directories
            if name != "__pycache__" and not name.startswith(".") and
            relative_child(name) not in excluded_relative_paths and
            not os.path.islink(os.path.join(directory, name)))
        for name in sorted(names):
            if relative_child(name) in excluded_relative_paths:
                continue
            if name.endswith(".pyc") and not keep_bytecode:
                continue
            if name.startswith(".") or (suffixes is not None and
                                         not name.lower().endswith(suffixes)):
                continue
            if excluded_suffixes and name.lower().endswith(excluded_suffixes):
                continue
            source_path = os.path.join(directory, name)
            if os.path.islink(source_path):
                continue
            target_path = os.path.join(
                target, os.path.relpath(source_path, source))
            _copy_file(source_path, target_path)
            written.append(target_path)
    return written


def launcher_version(source_root=None):
    source_root = source_root or repository_root()
    with open(os.path.join(source_root, "launcher", "wot_launcher.py"),
              "r", encoding="utf-8") as stream:
        match = _LAUNCHER_VERSION.search(stream.read())
    if match is None:
        raise ValueError("LAUNCHER_VERSION is missing from wot_launcher.py")
    return match.group(1)


def stage_launcher_contracts(target_root, source_root=None):
    """Stage only the launcher's imported data tools, never server modules."""
    source_root = source_root or repository_root()
    source = os.path.join(source_root, "src", "res", "scripts", "client",
                          *LAUNCHER_CONTRACTS_RELATIVE.split("/"))
    written = []
    for relative in LAUNCHER_CONTRACTS:
        origin = os.path.join(source, *relative.split("/"))
        if not os.path.isfile(origin):
            raise ValueError("Launcher data helper is missing: %s" % relative)
        target = os.path.join(target_root, *LAUNCHER_CONTRACTS_RELATIVE.split("/"),
                              *relative.split("/"))
        _copy_file(origin, target)
        written.append(target)
    return written


def build_identity(environ=None):
    """Return the package build identity used by diagnostics and LAN probes."""
    environ = os.environ if environ is None else environ
    explicit = str(environ.get(BUILD_IDENTITY_ENV, "") or "").strip()
    if explicit:
        if BUILD_IDENTITY_PATTERN.match(explicit) is None:
            raise ValueError("%s is invalid" % BUILD_IDENTITY_ENV)
        return explicit
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    return "local-%s-%s" % (stamp, uuid.uuid4().hex[:12])


def stage_ng_client(target_root, ng_client, source_root=None, identity=None):
    """Copy one NG client package and record its build identity."""
    if ng_client is None or not os.path.isdir(ng_client):
        raise ValueError("no NG client package; pass --ng-client")
    for relative in NG_CLIENT_REQUIRED:
        if not os.path.isfile(os.path.join(ng_client, *relative.split("/"))):
            raise ValueError("NG client package is incomplete: %s" % relative)
    source_root = source_root or repository_root()
    try:
        with open(os.path.join(source_root, "ng-launcher-source.json"),
                  encoding="utf-8") as stream:
            expected = json.load(stream)["fork_revision"]
        with open(os.path.join(ng_client, "offline-res.json"),
                  encoding="utf-8") as stream:
            actual = json.load(stream)["revision"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ValueError("NG client gameplay revision cannot be verified: %s" % error)
    if not isinstance(expected, str) or re.fullmatch(r"[0-9a-f]{40}", expected) is None:
        raise ValueError("Composed launcher gameplay revision is invalid")
    if actual != expected:
        raise ValueError("NG client gameplay revision %r does not match the launcher/server %s" %
                         (actual, expected))
    room_data = os.path.join(source_root, "native-room-data.json")
    try:
        with open(room_data, encoding="utf-8") as stream:
            room_identity = json.load(stream)
        if room_identity.get("schema") != 1 or room_identity.get("gameplay_revision") != expected:
            raise ValueError("native room data does not match the gameplay revision")
    except (OSError, ValueError, TypeError) as error:
        raise ValueError("Native room data is missing or invalid: %s" % error)
    # Python 2.7 bytecode can be part of the client runtime. Debug symbols
    # are archived beside each build instead of shipping to every player.
    written = _copy_tree(ng_client, target_root, keep_bytecode=True,
                         excluded_suffixes=NG_CLIENT_EXCLUDED_SUFFIXES,
                         excluded_relative_paths=NG_CLIENT_EXCLUDED_RELATIVE_PATHS + (
                             "native-room-data.json", BUILD_IDENTITY_RELATIVE))
    room_target = os.path.join(target_root, "native-room-data.json")
    _copy_file(room_data, room_target)
    written.append(room_target)
    target = os.path.join(target_root, *BUILD_IDENTITY_RELATIVE.split("/"))
    payload = {
        "schema": 1,
        "semanticVersion": launcher_version(source_root),
        "buildIdentity": identity or build_identity(),
    }
    directory = os.path.dirname(target)
    if not os.path.isdir(directory):
        os.makedirs(directory)
    with open(target, "w", encoding="utf-8") as stream:
        json.dump(payload, stream, indent=2, sort_keys=True)
        stream.write("\n")
    if target not in written:
        written.append(target)
    return written


def stage(target_root, source_root=None, include_clients=True,
          ng_client=None, identity=None):
    """Write the complete payload under target_root and return every file."""
    if os.path.isdir(target_root):
        shutil.rmtree(target_root)
    os.makedirs(target_root, exist_ok=True)
    written = []
    if include_clients:
        written.extend(stage_ng_client(
            os.path.join(target_root, NG_CLIENT_DIR), ng_client,
            source_root, identity))
        written.extend(stage_launcher_contracts(
            os.path.join(target_root, LAUNCHER_CONTRACTS_DIR), source_root))
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True,
                        help="directory that receives the payload")
    parser.add_argument("--source", default=None,
                        help="repository root (default: this checkout)")
    parser.add_argument("--ng-client", default=None,
                        help="NG client package directory (bwclient_h.exe)")
    arguments = parser.parse_args(argv)
    try:
        written = stage(arguments.output, arguments.source,
                        ng_client=arguments.ng_client)
    except ValueError as error:
        sys.stderr.write("%s\n" % error)
        return 1
    print("Staged %d payload files in %s" % (len(written), arguments.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
