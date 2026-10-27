"""Exercise the real pinned lifecycle seams without modifying an engine checkout."""
from pathlib import Path
import importlib.util
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('patch_stock_space_trees', ROOT / 'bigworld/patch_stock_space_trees.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class StockSpaceTreesPatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='stock-space-trees-patch-')
        self.root = Path(self.temporary.name)
        archive_path = Path('/tmp/wot-bigworld-engine-14.4.1.tar.gz')
        if not archive_path.exists():
            self.skipTest('Pinned public-engine archive is required for real lifecycle seams')
        self.paths = ('lib/chunk/chunk_manager.cpp', 'lib/compiled_space/static_scene_provider.cpp',
                      'lib/moo/deferred_pipeline.cpp', 'lib/moo/forward_pipeline.cpp')
        with tarfile.open(archive_path) as archive:
            names = archive.getnames()
            for relative in self.paths:
                matches = [name for name in names if name.endswith('/programming/bigworld/'+relative)]
                self.assertEqual(len(matches), 1)
                target = self.root / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(archive.extractfile(matches[0]).read())

    def tearDown(self):
        self.temporary.cleanup()

    def snapshot(self):
        return {str(path.relative_to(self.root)): path.read_bytes() for path in self.root.rglob('*') if path.is_file()}

    def test_actual_chunk_lifecycle_and_scene_operations_are_idempotent(self):
        PATCH.patch(self.root)
        first = self.snapshot()
        PATCH.patch(self.root)
        self.assertEqual(self.snapshot(), first)
        chunk = (self.root / self.paths[0]).read_text()
        self.assertEqual(chunk.count('initialiseStockTreeSceneRendering()'), 1)
        self.assertEqual(chunk.count('tickStockTreeSceneRendering( dTime )'), 1)
        self.assertEqual(chunk.count('finaliseStockTreeSceneRendering()'), 1)
        for call in ('init();', 'fini();', 'tick( dTime );'):
            self.assertNotIn('speedtree::SpeedTreeRenderer::'+call, chunk)
        scene = (self.root / self.paths[1]).read_text()
        self.assertEqual(scene.count('StockTreeSceneHandler::registerHandlers( scene )'), 1)
        self.assertEqual((self.root / 'lib/stock_tree_scene.hpp').read_bytes(),
                         (ROOT / 'bigworld/stock_tree_scene.hpp').read_bytes())
        # The original maps have no BWSS/VLOS payload. Registration must not
        # install the unrelated public default mapping or enable an SDK flag.
        self.assertNotIn('lib/compiled_space/compiled_space_mapping.cpp', first)
        self.assertNotIn('#define SPEEDTREE_SUPPORT', chunk)
        # 009c4ed0/009c69c0: both pipelines drain the faded trees in their
        # semitransparent stage; only the deferred one has the optimizer path.
        deferred = (self.root / self.paths[2]).read_text()
        stage = deferred[deferred.index('void SpeedTreeOptimizer::draw()'):]
        self.assertLess(stage.index('drawStockTreeSemitransparentStage( !g_drawSemiTransparentTrees, '
                                    'g_useSpeedTreeOptimizer,\n        g_blurSemiTransparentTrees );'),
                        stage.index('#if 0'))
        forward = (self.root / self.paths[3]).read_text()
        self.assertIn('void ForwardPipeline::endSemitransparentDraw()\n{\n    BW_GUARD;\n'
                      '    // #1513 009c69c0 always draws the faded old-ST4 trees.\n'
                      '    drawStockTreeSemitransparent( false );\n}', forward)
        for source in (deferred, forward):
            self.assertEqual(source.count('#include "stock_tree_scene.hpp"'), 1)

    def test_failure_in_later_file_publishes_nothing(self):
        for index, seam in ((1, 'StaticSceneSpeedTreeHandler::registerHandlers( scene );'),
                            (3, 'void ForwardPipeline::endSemitransparentDraw()')):
            path = self.root / self.paths[index]
            original = path.read_text()
            path.write_text(original.replace(seam, 'unrecognisedStockTreeSeam();'))
            before = self.snapshot()
            with self.assertRaises(ValueError):
                PATCH.patch(self.root)
            self.assertEqual(self.snapshot(), before)
            path.write_text(original)


if __name__ == '__main__':
    unittest.main()
