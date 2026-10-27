"""Install the actual old-ST4 scene renderer's lifecycle, scene operations and
semitransparent pipeline stage.

The #1513 compiled mapping owns the visibility mask and invokes the handler
directly for SpTr3. This patch does not pretend BWSS/VLOS sections exist.
"""
import argparse
from pathlib import Path


def replace_once(source, before, after):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized pinned tree-renderer seam: ' + repr(before))
    return source.replace(before, after)


def patch(root):
    root = Path(root)
    chunk_path = root / 'lib/chunk/chunk_manager.cpp'
    chunk = chunk_path.read_text(encoding='utf-8-sig')
    chunk = replace_once(chunk, '#include "chunk_manager.hpp"',
                         '#include "chunk_manager.hpp"\n#include "stock_tree_scene.hpp"')
    chunk = replace_once(chunk, '''#if SPEEDTREE_SUPPORT
    speedtree::SpeedTreeRenderer::init();
#endif ''', '''    // The old-ST4 owner consumes the original SPT/ctree resources directly.
    if (!initialiseStockTreeSceneRendering()) return false;''')
    chunk = replace_once(chunk, '''#if SPEEDTREE_SUPPORT
    speedtree::SpeedTreeRenderer::fini();
#endif''', '''    finaliseStockTreeSceneRendering();''')
    chunk = replace_once(chunk, '''#if SPEEDTREE_SUPPORT
    // tick the speedtrees
    speedtree::SpeedTreeRenderer::tick( dTime );
#endif''', '''    tickStockTreeSceneRendering( dTime );''')
    scene_path = root / 'lib/compiled_space/static_scene_provider.cpp'
    scene = scene_path.read_text(encoding='utf-8-sig')
    scene = replace_once(scene, '#include "static_scene_provider.hpp"',
                         '#include "static_scene_provider.hpp"\n#include "stock_tree_scene.hpp"')
    scene = replace_once(scene, '''#if SPEEDTREE_SUPPORT
\tStaticSceneSpeedTreeHandler::registerHandlers( scene );
#endif''', '''\tStockTreeSceneHandler::registerHandlers( scene );''')
    deferred_path = root / 'lib/moo/deferred_pipeline.cpp'
    deferred = deferred_path.read_text(encoding='utf-8-sig')
    deferred = replace_once(deferred, '#include "speedtree/speedtree_renderer.hpp"',
                            '#include "speedtree/speedtree_renderer.hpp"\n#include "stock_tree_scene.hpp"')
    deferred = replace_once(deferred, '''void SpeedTreeOptimizer::draw()
{
    BW_GUARD;

#if 0''', '''void SpeedTreeOptimizer::draw()
{
    BW_GUARD;

    // #1513 009c4ed0: direct, or the near-camera quarter-resolution path.
    drawStockTreeSemitransparentStage( !g_drawSemiTransparentTrees, g_useSpeedTreeOptimizer,
        g_blurSemiTransparentTrees );

#if 0''')
    forward_path = root / 'lib/moo/forward_pipeline.cpp'
    forward = forward_path.read_text(encoding='utf-8-sig')
    forward = replace_once(forward, '#include "speedtree/speedtree_renderer.hpp"',
                           '#include "speedtree/speedtree_renderer.hpp"\n#include "stock_tree_scene.hpp"')
    forward = replace_once(forward, '''void ForwardPipeline::endSemitransparentDraw()
{
    BW_GUARD;
}''', '''void ForwardPipeline::endSemitransparentDraw()
{
    BW_GUARD;
    // #1513 009c69c0 always draws the faded old-ST4 trees.
    drawStockTreeSemitransparent( false );
}''')
    # Stage every edit before publishing any file, including the shared header.
    header = Path(__file__).with_name('stock_tree_scene.hpp').read_bytes()
    chunk_path.write_text(chunk, encoding='utf-8')
    scene_path.write_text(scene, encoding='utf-8')
    deferred_path.write_text(deferred, encoding='utf-8')
    forward_path.write_text(forward, encoding='utf-8')
    (root / 'lib/stock_tree_scene.hpp').write_bytes(header)
    print('Original old-ST4 renderer lifecycle, scene operations and faded-tree stage installed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
