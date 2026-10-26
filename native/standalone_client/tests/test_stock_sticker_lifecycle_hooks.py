"""The original projected stickers retain one native script/draw owner."""
import importlib.util
import os
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StickerLifecycleHooksTests(unittest.TestCase):
    def test_original_draw_and_teardown_seams_are_transactional(self):
        modules = []
        for name in ('stock_damage_sticker_registration', 'stock_vehicle_stickers'):
            spec = importlib.util.spec_from_file_location(name, ROOT / 'bigworld' / ('patch_' + name + '.py'))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            modules.append(module)
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('The pinned public engine archive is required')
        with tempfile.TemporaryDirectory(prefix='stock-sticker-hooks-') as directory, tarfile.open(archive) as source:
            root = Path(directory)
            (root / 'client').mkdir()
            (root / 'lib').mkdir()
            for name in ('world_app.cpp', 'script_app.cpp', 'script_bigworld.cpp'):
                member = next(m for m in source.getmembers() if m.name.endswith('/programming/bigworld/client/' + name))
                (root / 'client' / name).write_bytes(source.extractfile(member).read())
            def snapshot():
                return {p.relative_to(root): p.read_bytes() for p in root.rglob('*') if p.is_file()}
            for module in modules:
                module.patch(root)
            before = snapshot()
            for module in reversed(modules):
                module.patch(root)
            self.assertEqual(before, snapshot())
            world = (root / 'client/world_app.cpp').read_text()
            self.assertLess(world.index('rp->endOpaqueDraw();'), world.index('drawStockVehicleStickers()'))
            self.assertLess(world.index('ForwardDecalsManager::getInstance()->draw();'), world.index('drawStockVehicleStickers()'))
            self.assertLess(world.index('drawStockVehicleStickers()'), world.index('rp->applyLighting();'))
            teardown = (root / 'client/script_app.cpp').read_text()
            self.assertLess(teardown.index('finiStockVehicleStickers();'), teardown.index('BigWorldClientScript::fini();'))
            tokens = (root / 'client/script_bigworld.cpp').read_text()
            for name in ('StockVehicleStickers_token', 'StockDamageStickerRegistration_token'):
                self.assertEqual(tokens.count(name + ' |'), 1)
            path = root / 'client/script_bigworld.cpp'
            path.write_text(tokens.replace('\tStockVehicleStickers_token |\n', '').replace('\tMath_token |', '\tChanged_token |'))
            before = snapshot()
            with self.assertRaises(ValueError):
                modules[1].patch(root)
            self.assertEqual(before, snapshot())


if __name__ == '__main__':
    unittest.main()
