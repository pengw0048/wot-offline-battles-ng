"""Exact #1513 edge phase/deferral evidence and interleaved SDK patching."""
import importlib.util
import os
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StockEdgeDetectionTests(unittest.TestCase):
    def test_sdk_phase_hooks_are_idempotent_and_failure_is_atomic(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public SDK archive required')
        spec = importlib.util.spec_from_file_location('edge_patch', ROOT / 'bigworld/patch_stock_edge_detection.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        names = ('client/world_app.cpp', 'lib/duplo/chunk_attachment.cpp',
                 'lib/compiled_space/py_attachment_entity_embodiment.cpp')
        with tempfile.TemporaryDirectory(prefix='edge-phase-patch-') as temp:
            folder = Path(temp)
            with tarfile.open(archive) as tar:
                members = tar.getmembers()
                for name in names:
                    path = folder / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    member = next(m for m in members if m.name.endswith('/' + name))
                    path.write_bytes(tar.extractfile(member).read())
            patch.patch(folder)
            for name in names:
                path = folder / name
                # Another patcher inserted its own include between ours and pch.
                path.write_text(path.read_text().replace('#include "pch.hpp"\n',
                                                        '#include "pch.hpp"\n#include "another_owner.hpp"\n'))
                if name.startswith('client/'):
                    path.write_text(path.read_text().replace('    rp->endOpaqueDraw();',
                                                            '    anotherOpaqueOwner();\n    rp->endOpaqueDraw();'))
                else:
                    text = path.read_text()
                    start = text.index('    if (stockDeferEdgeAttachment(')
                    path.write_text(text[:start] + '    anotherAttachmentOwner();\n' + text[start:])
            before = {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in folder.rglob('*') if p.is_file()}
            patch.patch(folder)
            self.assertEqual(before, {p: (p.read_bytes(), p.stat().st_mtime_ns) for p in before})
            world = (folder / names[0]).read_text()
            self.assertLess(world.index('prepareStockEdgeDetection()'), world.index('rp->endOpaqueDraw();'))
            # Last seam failure may not partially rewrite the earlier files.
            final = folder / names[-1]
            final.write_text(final.read_text().replace('void PyAttachmentEntityEmbodiment::doDraw(', 'void changed('))
            before = {p: p.read_bytes() for p in before}
            with self.assertRaises(ValueError):
                patch.patch(folder)
            self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_original_phase_calls_and_mode_deferral(self):
        client = os.environ.get('WOT_0922_CLIENT')
        if not client:
            self.skipTest('WOT_0922_CLIENT required')
        try:
            from stock_ballistics_oracle import Original, DATA, STACK, SENTINEL
            from unicorn import UC_HOOK_CODE
            from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EDI
        except ImportError:
            self.skipTest('Original PE execution dependencies required')
        original = Original(Path(client) / 'WorldOfTanks.exe')
        # The actual z-mask/normal-colour preparation is in World; Lens only
        # transfers it. 630e2b is a shimmer flush, not the mask preparation.
        for address, target in ((0x637193, 0xaf46e0), (0x630e77, 0xaf45f0), (0xaf4a3e, 0xaf39b0)):
            code = bytes(original.u.mem_read(address, 5))
            self.assertEqual(code[0], 0xe8)
            self.assertEqual(address + 5 + struct.unpack('<i', code[1:])[0], target)
        frame, drawer, embodiment, tables, backend, vtable = [DATA + n for n in (0x100, 0x400, 0x500, 0x600, 0x800, 0x900)]
        dispatch = SENTINEL + 0x100
        calls = []
        def record(uc, address, size, unused):
            calls.append((original.arg(1), original.arg(2)))
            original.ret(0, 8)
        original.u.hook_add(UC_HOOK_CODE, record, begin=dispatch, end=dispatch)
        original.put(frame - 0x4c, '<I', drawer)
        original.put(frame - 0x48, '<I', embodiment)
        original.put(drawer + 0x54, '<I', tables)
        original.put(0x1d5966e, '<B', 0)
        original.put(tables, '<I', tables + 0x100)
        original.put(tables + 0x100 + 12, '<I', backend)
        original.put(embodiment, '<II', 1513, 1)
        original.put(backend, '<I', vtable)
        original.put(vtable, '<I', dispatch)
        for mode in (0, 1, 2):
            calls[:] = []
            original.put(frame + 0x10, '<I', mode)
            original.u.reg_write(UC_X86_REG_EBP, frame)
            original.u.reg_write(UC_X86_REG_EDI, embodiment)
            original.u.reg_write(UC_X86_REG_ESP, STACK + 0xf0000)
            original.u.emu_start(0xaf41e6, 0xaf4212, count=1000)
            self.assertEqual(calls, [] if mode == 0 else [(1513, 1)])


if __name__ == '__main__':
    unittest.main()
