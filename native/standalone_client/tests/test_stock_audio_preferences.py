"""Speaker writes preserve unregistered graphics preferences at early startup."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('WOT_BIGWORLD_ROOT',
    '/tmp/wot-bigworld-engine-14.4.1/programming/bigworld'))


def function(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


class AudioPreferenceTests(unittest.TestCase):
    def test_production_helper_and_sdk_resource_lifecycle(self):
        compiler = os.environ.get('CXX', 'clang++')
        if not shutil.which(compiler) or not (SDK / 'lib/resmgr/dataresource.cpp').is_file():
            self.skipTest('Requires a C++ compiler and the pinned public BigWorld source')
        patch_path = ROOT / 'bigworld/patch_stock_graphics_settings.py'
        spec = importlib.util.spec_from_file_location('audio_pref_graphics_patch', patch_path)
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tempfile.TemporaryDirectory(prefix='speaker-preferences-') as directory:
            folder = Path(directory)
            for name in ('lib/moo/graphics_settings.cpp', 'lib/moo/graphics_settings.hpp',
                         'lib/moo/graphics_settings_picker.hpp', 'lib/romp/py_graphics_setting.cpp'):
                destination = folder / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(SDK / name, destination)
            patch.patch(folder)
            writer = function((folder / 'lib/moo/graphics_settings.cpp').read_text(),
                              'void GraphicsSetting::write(')
            resource = (SDK / 'lib/resmgr/dataresource.cpp').read_text()
            resource_methods = '\n'.join(function(resource, marker) for marker in (
                'DataResource::DataResource() :', 'DataSectionPtr DataResource::getRootSection()',
                'DataHandle::Error DataResource::load(', 'DataHandle::Error DataResource::save('))
            production = (ROOT / 'bigworld/stock_audio.cpp').read_text()
            helper = function(production, 'void saveSpeakerPreference(')
            owner = function((ROOT / 'bigworld/stock_preferences_path.cpp').read_text(),
                             'BW::string DeviceApp::preferencesFilePath()')
            legacy = '''void legacySaveSpeakerPreference(const char* key, unsigned value) {
    if (!s_scriptsPreferences) return;
    s_scriptsPreferences->writeUInt(key, value);
    if (!App::instance().savePreferences())
        ERROR_MSG("StockAudio: failed to save authored speaker preference\\n");
}'''
            host = (ROOT / 'tests/stock_audio_preferences_host.cpp').read_text()
            for marker, value in (('@PATH_OWNER@', owner), ('@DATA_RESOURCE@', resource_methods),
                                  ('@GRAPHICS_WRITER@', writer), ('@LEGACY_HELPER@', legacy),
                                  ('@PRODUCTION_HELPER@', helper)):
                self.assertEqual(host.count(marker), 1)
                host = host.replace(marker, value)
            source, binary = folder / 'probe.cpp', folder / 'probe'
            source.write_text(host)
            command = [compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG']
            if sys.platform != 'win32':
                command += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                            '-fno-omit-frame-pointer']
            build = subprocess.run(command + [str(source), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            normal = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(normal.returncode, 0, normal.stdout + normal.stderr)
            old = subprocess.run([str(binary), 'legacy'], capture_output=True, text=True)
            self.assertEqual(old.returncode, 1, old.stdout + old.stderr)
            self.assertIn('unregistered graphics settings were lost', old.stderr)

    def test_complete_original_savers_and_failure_boundaries(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('Requires the exact Chinese HD #1513 executable')
        from stock_audio_preferences_oracle import OriginalSpeakerPreferences
        original = OriginalSpeakerPreferences(Path(client) / 'WorldOfTanks.exe')
        for field in ('detectedSpeakerConfig', 'speakerConfig'):
            key = ('scriptsPreferences/soundPrefs/' + field).encode()
            for value in (0, 2, 5, 7, 0xffffffff):
                for live_present in (False, True):
                    for load_error, save_error, null_root in ((0, 0, False), (1, 0, False),
                                                              (0, 2, False), (0, 0, True)):
                        with self.subTest(field=field, value=value, live=live_present,
                                          load=load_error, save=save_error, null_root=null_root):
                            trace, live, disk = original.save(key, value, live=live_present,
                                load_error=load_error, save_error=save_error, null_root=null_root)
                            expected = [('live-write', key, value)] if live_present else []
                            path = b'C:/player settings/preferences.xml'
                            expected += [('resolve', path), ('load', path, load_error)]
                            if not load_error and not null_root:
                                expected += [('disk-write', key, value), ('save', path, save_error)]
                            self.assertEqual(trace, expected)
                            self.assertEqual(disk[b'graphicsPreferences/entry/FAR_PLANE'], 3)
                            self.assertEqual(disk[b'graphicsPreferences/entry/OBJECT_LOD'], 3)
                            self.assertEqual(disk[b'devicePreferences/windowedWidth'], 1014)
                            self.assertEqual(disk[b'scriptsPreferences/custom'], b'preserve')
                            if not load_error and not null_root and not save_error:
                                self.assertEqual(disk[key], value)
                            else:
                                self.assertNotIn(key, disk)
                            if live_present:
                                self.assertEqual(live[key], value)
                            else:
                                self.assertIsNone(live)


if __name__ == '__main__':
    unittest.main()
