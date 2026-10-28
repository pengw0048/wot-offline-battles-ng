"""#1513 VM call-frame contract and original lobby Scheduler catch-up regression."""
from pathlib import Path
import ctypes
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
from test_stock_vector4_basic import block
from stock_flash_scheduler_abc import read_scheduler, Probe
spec = importlib.util.spec_from_file_location('flash_scheduler_patch', ROOT/'bigworld/patch_stock_flash_scheduler.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def sdk(test):
    name = 'WOT_SCALEFORM_SDK_ROOT'
    root = Path(os.environ.get(name, '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
    path = root / patcher.SOURCE
    if not path.is_file():
        if name in os.environ: test.fail('Configured Scaleform SDK header missing: ' + str(path))
        test.skipTest('Pinned Scaleform 4.2.22 source SDK required')
    version = (root/'Include/GFxVersion.h').read_text()
    test.assertIn('"4.2.22"', version)
    source = path.read_text(encoding='utf-8-sig')
    # A prepared source is also a valid fixture; reconstruct only this exact seam for the control.
    return source.replace(patcher.AFTER, patcher.BEFORE)


def client(test):
    value = os.environ.get('WOT_0922_CLIENT')
    if not value: test.skipTest('Original #1513 client required')
    root = Path(value)
    for relative in ('WorldOfTanks.exe', 'res/packages/gui.pkg'):
        test.assertTrue((root/relative).is_file(), 'Configured #1513 fixture missing: ' + relative)
    return root


class Gate:
    def __init__(self, test, directory, source, cache):
        compiler = os.environ.get('CXX', 'c++')
        if not shutil.which(compiler):
            if 'CXX' in os.environ: test.fail('Configured C++ compiler not found')
            test.skipTest('C++ compiler required')
        name = 'cache' if cache else 'paged'
        name += '-stock' if patcher.AFTER in source else '-sdk'
        text = (HERE/'stock_flash_scheduler_host.cpp').read_text()
        limit = re.search(r'(?m)^    enum \{MaxCallStackSize = \d+\};', source).group(0)
        text = text.replace('@LIMIT@', limit).replace('@ADD_FRAME@', block(source, 'void AddFrame('))
        cpp, library = directory/(name+'.cpp'), directory/(name+('.dll' if os.name == 'nt' else '.so'))
        cpp.write_text(text)
        flags = ['-DSF_AS3_ENABLE_CALLFRAME_CACHE'] if cache else []
        subprocess.run([compiler, '-std=c++14', '-O0', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                        *flags, str(cpp), '-o', str(library)], check=True)
        self.library = ctypes.CDLL(str(library))
        self.library.reset.argtypes = [ctypes.c_uint, ctypes.c_int]
        self.library.enter.restype = ctypes.c_int
        self.library.depth.restype = ctypes.c_uint
        self.library.error.restype = ctypes.c_int
        self.library.setup_count.restype = ctypes.c_int
        self.library.register_count.restype = ctypes.c_int
        self.reset = self.library.reset
        self.enter = self.library.enter
        self.leave = self.library.leave
        self.depth = self.library.depth
        self.error = self.library.error
        self.setup_count = self.library.setup_count
        self.register_count = self.library.register_count

    def close(self):
        if os.name == 'nt':
            import _ctypes
            _ctypes.FreeLibrary(self.library._handle)
            self.library._handle = 0


class FlashSchedulerTest(unittest.TestCase):
    def test_patch_preserves_complete_addframe_and_is_atomic_idempotent(self):
        source = sdk(self)
        patched = patcher.patch_source(source)
        self.assertEqual(patcher.patch_source(patched), patched)
        self.assertEqual(patched, source.replace(patcher.BEFORE, patcher.AFTER, 1))
        self.assertEqual(block(source, 'void AddFrame('), block(patched, 'void AddFrame('))
        # The base SDK patch also owns VM aliases/AMF declarations. Replay its
        # actual callbacks in memory; no prepared SDK tree is modified here.
        base_spec = importlib.util.spec_from_file_location('scheduler_base_sdk_patch', ROOT/'bigworld/patch_scaleform_sdk.py')
        base = importlib.util.module_from_spec(base_spec)
        base_spec.loader.exec_module(base)
        sdk_root = Path(os.environ.get('WOT_SCALEFORM_SDK_ROOT', '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        def base_vm_edits(text):
            values = [text]
            def edit(path, before, after, label):
                if Path(path) == sdk_root/patcher.SOURCE:
                    values[0] = base.replace_once(values[0], before, after, label)
            base.class_alias_edits(sdk_root, edit)
            base.amf3_edits(sdk_root, edit)
            return values[0]
        self.assertEqual(base_vm_edits(patched), patcher.patch_source(base_vm_edits(source)))
        for changed in (source.replace(patcher.BEFORE, ''), source+patcher.BEFORE,
                        patched+patcher.AFTER, patched.replace('MaxCallStackSize = 1024', 'MaxCallStackSize = 2048')):
            with self.assertRaises(ValueError): patcher.patch_source(changed)
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory)/patcher.SOURCE; path.parent.mkdir(parents=True); path.write_text(changed)
                with self.assertRaises(ValueError): patcher.patch(directory)
                self.assertEqual(path.read_text(), changed)

    def test_complete_sdk_addframe_cache_and_paged_paths(self):
        source = sdk(self)
        with tempfile.TemporaryDirectory() as directory:
            for cache in (False, True):
                for capacity, text in ((128, source), (1024, patcher.patch_source(source))):
                    gate = Gate(self, Path(directory), text, cache)
                    try:
                        for depth in (0, 1, 127, capacity-1, capacity):
                            gate.reset(depth, 0)
                            self.assertEqual(bool(gate.enter()), depth < capacity)
                            self.assertEqual(gate.depth(), depth+int(depth < capacity))
                            self.assertEqual(gate.error(), 1023 if depth == capacity else 0)
                            self.assertEqual(gate.setup_count(), int(depth < capacity))
                            self.assertEqual(gate.register_count(), int(depth < capacity))
                        # Register setup errors must never push a frame, in either storage mode.
                        gate.reset(10, 1)
                        self.assertFalse(gate.enter())
                        self.assertEqual((gate.depth(), gate.setup_count(), gate.register_count()), (10, 1, 1))
                    finally: gate.close()

    def test_original_x86_addframe_overflow_boundary(self):
        root = client(self)
        try:
            from stock_flash_input_mode_oracle import OriginalFlashInputMode, OBJECT, STACK, SENTINEL
            import unicorn
            from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP
        except ImportError: self.skipTest('Optional Unicorn/pefile dependencies required')
        import struct
        original = OriginalFlashInputMode(root/'WorldOfTanks.exe')
        for size in (0, 1, 127, 128, 129, 255, 1023, 1024):
            result = []
            for address, overflow in ((0xda09b5, True), (0xda09e6, False)):
                original.hooks[address] = lambda overflow=overflow: (result.append(overflow), original.u.emu_stop())
                # Install each hook once, then update the dictionary for subsequent runs.
                if size == 0:
                    original.u.hook_add(unicorn.UC_HOOK_CODE, original._hook, begin=address, end=address)
            original.u.mem_write(OBJECT+0x8c, struct.pack('<I', size))
            original.u.mem_write(STACK+0x80000, struct.pack('<I', SENTINEL))
            original.u.reg_write(UC_X86_REG_ECX, OBJECT)
            original.u.reg_write(UC_X86_REG_ESP, STACK+0x80000)
            original.u.emu_start(0xda09a0, SENTINEL+1, count=100)
            self.assertEqual(result, [size == 1024], size)

    def test_original_lobby_scheduler_finite_due_queue_negative_control(self):
        root, source = client(self), sdk(self)
        with zipfile.ZipFile(root/'res/packages/gui.pkg') as package:
            abc = read_scheduler(package.read('gui/flash/lobby.swf'))
        previous = sys.getrecursionlimit()
        sys.setrecursionlimit(20000)  # Only the Python replay host; production remains the original 1024 AS frames.
        try:
            with tempfile.TemporaryDirectory() as directory:
                for cache in (False, True):
                    for patched in (False, True):
                        gate = Gate(self, Path(directory), patcher.patch_source(source) if patched else source, cache)
                        try:
                            gate.reset(0, 0)
                            probe = Probe(abc, gate)
                            if not patched:
                                with self.assertRaisesRegex(OverflowError, '1023'): probe.run()
                                self.assertEqual(probe.max_depth, 128)
                                self.assertEqual(gate.error(), 1023)
                            else:
                                scheduler = probe.run()
                                self.assertEqual(probe.invoked, 160)
                                self.assertEqual(scheduler.values['_taskStack'].values['_tasks'], [])
                                self.assertEqual(probe.timer_calls, [('stop', 40.0)])
                                self.assertGreater(probe.max_depth, 128)
                                self.assertLess(probe.max_depth, 1024)
                                self.assertEqual(gate.error(), 0)
                                gate.reset(0, 0)
                                future = Probe(abc, gate)
                                future.now = 60
                                pending = future.run()
                                self.assertEqual(future.invoked, 8)
                                self.assertEqual(len(pending.values['_taskStack'].values['_tasks']), 8)
                                self.assertEqual(future.timer_calls, [('reset', 20.0), ('start', 20.0)])
                            self.assertEqual(gate.depth(), 0)
                        finally: gate.close()
        finally: sys.setrecursionlimit(previous)


if __name__ == '__main__': unittest.main()
