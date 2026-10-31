#!/usr/bin/env python3
"""Copy the LAN server and NG client package the launcher carries."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
import time
import uuid

SERVER_DIR = "servers"
NG_CLIENT_DIR = "ng-client"

PAYLOAD_FILES = {
    "0.9.22": (
        "server/battle_wire_codec.py",
        "server/battle_wire_replica.py",
        "server/battle_wire_schema.py",
        "server/lan_battle_server.py",
        "server/offline_rewards.py",
        "server/retired_vehicles.py",
        "server/server_bot_ai.py",
        "server/vehicle_overlay_store.py",
        "server/windows_server.py",
    ),
}

PAYLOAD_TREES = {
    "0.9.22": ("src/res/scripts/client/gui/mods/offline_lan_0922",),
}

# Files every NG client package must carry before the launcher can run it.
NG_CLIENT_REQUIRED = (
    "bwclient_h.exe",
    "offline-res.json",
    "tools/prepare_native_client.py",
)
NG_CLIENT_EXCLUDED_SUFFIXES = (".pdb",)
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
               excluded_suffixes=()):
    written = []
    for directory, directories, names in os.walk(source):
        directories[:] = sorted(
            name for name in directories
            if name != "__pycache__" and not name.startswith(".") and
            not os.path.islink(os.path.join(directory, name)))
        for name in sorted(names):
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


def stage_servers(target_root, source_root=None):
    source_root = source_root or repository_root()
    written = []
    for port_version, relative_paths in PAYLOAD_FILES.items():
        for relative_path in relative_paths:
            source = os.path.join(source_root, *relative_path.split("/"))
            target = os.path.join(target_root, port_version,
                                  *relative_path.split("/"))
            _copy_file(source, target)
            written.append(target)
    for port_version, relative_dirs in PAYLOAD_TREES.items():
        for relative_dir in relative_dirs:
            source = os.path.join(source_root, *relative_dir.split("/"))
            target = os.path.join(target_root, port_version,
                                  *relative_dir.split("/"))
            written.extend(_copy_tree(source, target, suffixes=(".py",)))
    return written


def launcher_version(source_root=None):
    source_root = source_root or repository_root()
    with open(os.path.join(source_root, "launcher", "wot_launcher.py"),
              "r", encoding="utf-8") as stream:
        match = _LAUNCHER_VERSION.search(stream.read())
    if match is None:
        raise ValueError("LAUNCHER_VERSION is missing from wot_launcher.py")
    return match.group(1)


def build_identity(environ=None):
    """Return one opaque diagnostic identity, never a compatibility gate."""
    environ = os.environ if environ is None else environ
    explicit = str(environ.get(BUILD_IDENTITY_ENV, "") or "").strip()
    if explicit:
        if BUILD_IDENTITY_PATTERN.match(explicit) is None:
            raise ValueError("%s is invalid" % BUILD_IDENTITY_ENV)
        return explicit
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    return "local-%s-%s" % (stamp, uuid.uuid4().hex[:12])


def stage_ng_client(target_root, ng_client, source_root=None, identity=None):
    """Copy one NG client package and record its diagnostic identity."""
    if ng_client is None or not os.path.isdir(ng_client):
        raise ValueError("no NG client package; pass --ng-client")
    for relative in NG_CLIENT_REQUIRED:
        if not os.path.isfile(os.path.join(ng_client, *relative.split("/"))):
            raise ValueError("NG client package is incomplete: %s" % relative)
    # Python 2.7 bytecode can be part of the client runtime. Debug symbols
    # are archived beside each build instead of shipping to every player.
    written = _copy_tree(ng_client, target_root, keep_bytecode=True,
                         excluded_suffixes=NG_CLIENT_EXCLUDED_SUFFIXES)
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
    written = stage_servers(os.path.join(target_root, SERVER_DIR), source_root)
    if include_clients:
        written.extend(stage_ng_client(
            os.path.join(target_root, NG_CLIENT_DIR), ng_client,
            source_root, identity))
    return written


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True,
                        help="directory that receives the payload")
    parser.add_argument("--source", default=None,
                        help="repository root (default: this checkout)")
    parser.add_argument("--ng-client", default=None,
                        help="NG client package directory (bwclient_h.exe)")
    parser.add_argument("--servers-only", action="store_true",
                        help="stage the LAN servers without the NG client")
    arguments = parser.parse_args(argv)
    try:
        written = stage(arguments.output, arguments.source,
                        include_clients=not arguments.servers_only,
                        ng_client=arguments.ng_client)
    except ValueError as error:
        sys.stderr.write("%s\n" % error)
        return 1
    print("Staged %d payload files in %s" % (len(written), arguments.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
