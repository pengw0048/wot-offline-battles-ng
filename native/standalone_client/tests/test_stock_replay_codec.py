"""Prove the Python-2 producer/real restricted writer boundary end to end."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]


class StockReplayCodecTests(unittest.TestCase):
    def test_real_python2_process_recorder_and_unchanged_restricted_readers(self):
        source = os.environ.get('WOT_FORK_0101_SOURCE')
        if not source:
            self.skipTest('Pinned gameplay source and original wotmod are required')
        source = Path(source)
        package = source / 'src/res/scripts/client/gui/mods/offline_lan_0922'
        spec = importlib.util.spec_from_file_location(
            'replay_bindings', ROOT / 'bigworld/stock_replay_bindings.py')
        adapter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(adapter)
        py27 = subprocess.check_output(['pyenv', 'which', 'python2.7'], text=True,
            env=dict(os.environ, PYENV_VERSION='2.7.18')).strip()
        with tempfile.TemporaryDirectory(prefix='wot-replay-codec-') as temporary:
            root = Path(temporary)
            target = root / 'client/gui/mods/offline_lan_0922'
            target.mkdir(parents=True)
            for parent in [target, *list(target.parents)[:3]]:
                (parent / '__init__.py').write_text('')
            original = (package / 'replay_process.py').read_text()
            changed = adapter.process(original)
            self.assertEqual(changed.replace(
                'import uuid\n\nfrom gui.mods.offline_lan_0922 import stock_replay_codec as _replay_codec\n',
                'import uuid\n').replace('_replay_codec.dumps(value, 2)', '_pickle.dumps(value, 2)'), original)
            for text in (original.replace('        data = _pickle.dumps(value, 2)\n', ''),
                         original + '        data = _pickle.dumps(value, 2)\n'):
                with self.assertRaises(ValueError):
                    adapter.process(text)
            (target / 'replay_process.py').write_text(changed)
            (target / '_original_replay_process.py').write_text(original)
            for name in ['offline_replay.py', 'replay_reader.py']:
                shutil.copyfile(package / name, target / name)
            for name in ['stock_snapshot_static.py', 'stock_replay_codec.py']:
                shutil.copyfile(ROOT / 'bigworld' / name, target / name)
            archive = source / 'dist/org.colorfulmeans.offline_lan_0922_0.10.1.wotmod'
            with zipfile.ZipFile(archive) as outer:
                with zipfile.ZipFile(io.BytesIO(outer.read('res/offline_replay/recorder-runtime.zip'))) as runtime:
                    writer = runtime.read('replay_writer_process.py')
            script = root / 'replay_writer_process.py'
            script.write_bytes(writer)
            run = subprocess.run([py27, '-B', str(ROOT / 'tests/stock_replay_codec_fixture.py'),
                str(root / 'client'), str(script), sys.executable, str(root),
                str(ROOT.parents[1] / 'tests')], capture_output=True, text=True, timeout=25,
                env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn('fixed_writer_saved=True bots=29 records=7 static_rewalks=0', run.stdout)
            scope = {'__name__': 'actual_restricted_writer'}
            exec(compile(writer, str(script), 'exec'), scope)
            value = scope['primitive_load']((root / 'plain.pickle').read_bytes())
            self.assertEqual(value['data']['players'][0]['outfits'], [{'value': [1, 2, 3]}])
            with self.assertRaisesRegex(ValueError, 'IPC global objects are forbidden'):
                scope['primitive_load']((root / 'forbidden.pickle').read_bytes())
            # The real primitive bounds/cycle/non-finite rules remain intact.
            import pickle
            cycle = []; cycle.append(cycle)
            for value in [dict(data=cycle), dict(data=float('nan'))]:
                with self.assertRaises(ValueError):
                    scope['primitive_load'](pickle.dumps(value, protocol=2))


if __name__ == '__main__':
    unittest.main()
