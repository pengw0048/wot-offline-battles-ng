"""Exact #1513 scene-bank selection, original events and native callback order."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import struct
import sys
import tarfile
import tempfile
import unittest
import zipfile

from native_test_compile_flags import native_test_compile_flags
from test_vehicle_compound_nodes import block
from audit_wwise_bank import sound_id

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audio_space_patch', ROOT / 'bigworld/patch_stock_audio_space.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def facade_source(test):
    name = 'WOT_PUBLIC_ENGINE_ARCHIVE'
    archive = Path(os.environ.get(name, '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if name in os.environ:
            test.fail('Configured public SDK archive is missing')
        test.skipTest('Pinned public SDK archive required')
    with tarfile.open(archive) as tar:
        member = next(m for m in tar if m.name.endswith('/programming/bigworld/' + patcher.PATH))
        return tar.extractfile(member).read().decode('utf-8-sig')


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value:
        test.skipTest('Exact #1513 client required')
    path = Path(value)
    test.assertTrue((path / 'WorldOfTanks.exe').is_file(), 'Invalid explicit WOT_0922_CLIENT')
    return path


def events(data):
    found = set()
    offset = 0
    while offset < len(data):
        tag, size = struct.unpack_from('<4sI', data, offset)
        offset += 8
        assert offset + size <= len(data)
        if tag == b'HIRC':
            count = struct.unpack_from('<I', data, offset)[0]
            at = offset + 4
            for unused in range(count):
                kind, length = struct.unpack_from('<BI', data, at)
                at += 5
                assert length >= 4 and at + length <= offset + size
                if kind == 4:
                    found.add(struct.unpack_from('<I', data, at)[0])
                at += length
            assert at == offset + size
        offset += size
    assert offset == len(data)
    return found


class AudioSpaceTests(unittest.TestCase):
    def test_patch_scope_idempotence_and_failure(self):
        source = facade_source(self)
        with tempfile.TemporaryDirectory(prefix='audio-space-patch-') as temporary:
            root = Path(temporary)
            path = root / patcher.PATH
            path.parent.mkdir(parents=True)
            path.write_text(source)
            patcher.patch(root)
            patched = path.read_text()
            self.assertEqual(patched.replace(patcher.AFTER, patcher.BEFORE).replace('\n' + patcher.INCLUDE, ''), source)
            patcher.patch(root)
            self.assertEqual(path.read_text(), patched)
            path.write_text(source.replace('lastCameraSpace_ = pSpace;', 'changedSpace_ = pSpace;'))
            before = path.read_bytes()
            with self.assertRaises(ValueError):
                patcher.patch(root)
            self.assertEqual(path.read_bytes(), before)

    def test_actual_bank_diff_and_facade_notification_order(self):
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        original = facade_source(self)
        audio = (ROOT / 'bigworld/stock_audio.cpp').read_text()
        host = (ROOT / 'tests/stock_audio_space_host.cpp').read_text()
        host = host.replace('@BANK_DIFF@', block(audio, 'bool selectStockAudioBankSet('))
        host = host.replace('@SPACE_SELECTOR@', block(audio, 'void setStockAudioSpaceBankSet('))
        with tempfile.TemporaryDirectory(prefix='audio-space-native-') as temporary:
            root = Path(temporary)
            path = root / patcher.PATH
            path.parent.mkdir(parents=True)
            path.write_text(original)
            patcher.patch(root)
            after = block(path.read_text(), 'void FacadeApp::tick(')
            command = [compiler, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                       *native_test_compile_flags(compiler)]
            cpp, binary = root / 'host.cpp', root / 'host'
            cpp.write_text(host.replace('@FACADE_TICK@', after))
            subprocess.run(command + [str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
            # Without the camera-space producer the original callback reaches
            # Python while only login banks exist, reproducing missing events.
            cpp.write_text(host.replace('@FACADE_TICK@', block(original, 'void FacadeApp::tick(')))
            subprocess.run(command + [str(cpp), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)

    def test_exact_bank_event_owners(self):
        path = client(self)
        with zipfile.ZipFile(path / 'res/packages/audioww.pkg') as archive:
            names = {Path(n).name.lower(): n for n in archive.namelist()}
            checks = (('wpn.bnk', 'wpn_small_PC'),
                      ('gun_reload.bnk', 'gun_rld_20_45mm'),
                      ('eng_gaz_aa.bnk', 'eng_gaz_aa_pc'))
            login_events = events(archive.read(names['hangar_v2.bnk']))
            for bank, event in checks:
                event_id = sound_id(event)
                self.assertIn(event_id, events(archive.read(names[bank])))
                self.assertNotIn(event_id, login_events)

    def test_exact_native_bank_group_selection(self):
        path = client(self)
        for dependency in ('pefile', 'unicorn'):
            if importlib.util.find_spec(dependency) is None:
                self.skipTest('Optional x86 oracle dependency unavailable: ' + dependency)
        import unicorn
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX
        from stock_flash_input_mode_oracle import OriginalFlashInputMode, MARKS
        oracle = OriginalFlashInputMode(path / 'WorldOfTanks.exe')
        selected = []
        def boundary():
            value = oracle.u.reg_read(UC_X86_REG_EBX)
            selected.append(value)
            oracle._return(value)
        oracle.hooks[0x1255deb] = boundary
        oracle.u.hook_add(unicorn.UC_HOOK_CODE, oracle._hook, begin=0x1255deb, end=0x1255deb)
        for login in (0, 1):
            for gameplay_present in (0, 1):
                oracle.u.reg_write(UC_X86_REG_EBP, MARKS + 0x2000)
                oracle.u.mem_write(MARKS + 0x200c, struct.pack('<I', login))
                oracle.u.reg_write(UC_X86_REG_EAX, gameplay_present)
                result = oracle._call(0x1255dcf, 0)
                self.assertEqual(result, 0x144 if login else 0x15c if gameplay_present else 0x150)
        self.assertEqual(selected, [0x150, 0x15c, 0x144, 0x144])


if __name__ == '__main__':
    unittest.main()
