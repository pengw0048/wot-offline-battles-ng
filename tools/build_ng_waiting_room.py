"""Rebuild the NG room overlay with the pinned fork's unmodified SWF builder."""
import argparse
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OVERLAY = ROOT / 'native/standalone_client/bigworld/offline_entry'


def build(fork_source, client, java, ffdec):
    fork_source = Path(fork_source).resolve()
    sys.path.insert(0, str(fork_source / 'tools'))
    try:
        spec = importlib.util.spec_from_file_location(
            'pinned_room_builder', fork_source / 'tools/build_waiting_room_swf.py')
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory(prefix='ng-room-build-') as directory:
            root = Path(directory)
            actionscript = root / 'tools/actionscript'
            actionscript.mkdir(parents=True)
            shutil.copyfile(OVERLAY / 'OfflineWaitingRoom.as', actionscript / 'OfflineWaitingRoom.as')
            shutil.copyfile(fork_source / 'tools/actionscript/OfflineMapPicker.as',
                            actionscript / 'OfflineMapPicker.as')
            builder.ROOT = root
            output = root / 'output/offlineWaitingRoom.swf'
            builder.build(client, java, ffdec, output)
            # The pinned builder also rebuilds its unchanged map picker.
            # Only the room belongs to this overlay.
            for name in ('offlineWaitingRoom.swf', 'offlineWaitingRoom.swf.json'):
                shutil.copyfile(output.parent / name, OVERLAY / name)
    finally:
        sys.path.pop(0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ('fork-source', 'client', 'java', 'ffdec'):
        parser.add_argument('--' + argument, required=True)
    arguments = parser.parse_args()
    build(arguments.fork_source, arguments.client, arguments.java, arguments.ffdec)
