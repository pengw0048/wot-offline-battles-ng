#!/usr/bin/env python3
"""Compose the NG launcher source tree from the pinned gameplay source.

The launcher, its tests and its build read ``server/``, ``src/`` and
``tools/`` beside ``launcher/``. Those must be the pinned v0.10.1 gameplay
source, not this repository's retired line, and the server must speak the
binary battle wire the independent client uses. The composed tree is:

- every file of the pinned source archive or directory;
- ``launcher/`` replaced by this repository's NG launcher;
- ``server/`` replaced by ``native/standalone_client/wire/stage_server.py``,
  listening on its own port so an NG room never answers an original-mod
  launcher.

    python3 tools/stage_ng_launcher_source.py --output OUT [--fork-source PATH]

Without ``--fork-source`` the pinned revision is downloaded from GitHub.
"""

import argparse
import io
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.request import urlopen


ROOT = Path(__file__).resolve().parent.parent
WIRE = ROOT / 'native/standalone_client/wire'
FORK_REPOSITORY = 'colorfulmeans/wot-0.9.22-offline-battles'
SOURCE_RECORD = 'ng-launcher-source.json'
ORIGINAL_SERVER_PORT = 28782
# Must match launcher/core.py DEFAULT_SERVER_PORT.
NG_SERVER_PORT = 28792
_IGNORE = shutil.ignore_patterns('__pycache__', '*.pyc', '*.pyo')


def pinned_revision():
    """Read the one gameplay pin that the native client build also uses."""
    script = (ROOT / 'tools/build_bigworld_client.ps1').read_text(encoding='utf-8')
    match = re.search(r'^\$OfflineForkRevision = "([0-9a-f]{40})"$', script, re.M)
    if match is None:
        raise ValueError('The pinned gameplay revision is not declared.')
    return match.group(1)


def _extract_archive(data, revision, target):
    prefix = 'wot-0.9.22-offline-battles-' + revision
    with tarfile.open(fileobj=io.BytesIO(data), mode='r:gz') as archive:
        members = archive.getmembers()
        for member in members:
            path = PurePosixPath(member.name)
            if (path.parts[:1] != (prefix,) or '..' in path.parts or
                    '\\' in member.name or not (member.isdir() or member.isfile())):
                raise ValueError('Unexpected pinned source member: ' + member.name)
        for member in members:
            relative = PurePosixPath(*PurePosixPath(member.name).parts[1:])
            if not relative.parts:
                continue
            destination = target.joinpath(*relative.parts)
            if member.isdir():
                destination.mkdir(parents=True, exist_ok=True)
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            with archive.extractfile(member) as source, destination.open('wb') as output:
                shutil.copyfileobj(source, output)


def _copy_fork(source, revision, target):
    if source is None:
        url = 'https://codeload.github.com/%s/tar.gz/%s' % (FORK_REPOSITORY, revision)
        with urlopen(url, timeout=120) as response:
            _extract_archive(response.read(), revision, target)
        return
    source = Path(source).expanduser().resolve()
    if source.is_dir():
        shutil.copytree(source, target, ignore=_IGNORE, dirs_exist_ok=True)
    else:
        _extract_archive(source.read_bytes(), revision, target)


def _stage_server_port(path):
    """Give the NG room its own port beside an original-mod room."""
    text = path.read_text(encoding='utf-8')
    before = 'SERVER_PORT = %d\n' % ORIGINAL_SERVER_PORT
    if text.count(before) != 1:
        raise ValueError('The pinned Windows server port seam changed.')
    path.write_text(text.replace(before, 'SERVER_PORT = %d\n' % NG_SERVER_PORT),
                    encoding='utf-8')


def _ng_revision():
    try:
        return subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=str(ROOT), check=True,
                              capture_output=True, text=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def stage(output, fork_source=None, revision=None):
    output = Path(output).expanduser().resolve()
    if output.exists():
        raise ValueError('Output exists: %s' % output)
    revision = revision or pinned_revision()
    sys.path.insert(0, str(WIRE))
    try:
        import stage_server
    finally:
        sys.path.remove(str(WIRE))
    output.parent.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='.ng-launcher-source-', dir=str(output.parent)))
    try:
        tree = work / 'tree'
        tree.mkdir()
        _copy_fork(fork_source, revision, tree)
        for required in ('launcher/core.py', 'server/lan_battle_server.py',
                         'src/res/scripts/client/gui/mods/offline_lan_0922/__init__.py',
                         'tools/packed_xml.py'):
            if not (tree / required).is_file():
                raise ValueError('The pinned source is missing ' + required)
        shutil.rmtree(tree / 'launcher')
        shutil.copytree(ROOT / 'launcher', tree / 'launcher', ignore=shutil.ignore_patterns(
            '__pycache__', '*.pyc', '*.pyo', 'build', 'dist'))
        pinned_server = work / 'pinned-server'
        (tree / 'server').rename(pinned_server)
        stage_server.stage(str(pinned_server), str(tree / 'server'))
        _stage_server_port(tree / 'server/windows_server.py')
        record = {'fork_repository': FORK_REPOSITORY, 'fork_revision': revision,
                  'ng_revision': _ng_revision()}
        (tree / SOURCE_RECORD).write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        tree.rename(output)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return output


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', required=True)
    parser.add_argument('--fork-source', default=None,
                        help='pinned source directory or GitHub tar.gz archive')
    arguments = parser.parse_args(argv)
    try:
        path = stage(arguments.output, arguments.fork_source)
    except (OSError, ValueError) as error:
        print('NG launcher source staging failed: %s' % error, file=sys.stderr)
        return 1
    print('Staged the NG launcher source tree at %s' % path)
    return 0


if __name__ == '__main__':
    sys.exit(main())
