"""Actual tree-provider lifecycle and the original #1513 no-SRT contract."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_stock_cursor_camera import ROOT, compile_host, fresh_engine, load_patch, requirements, function
from test_stock_tree_hiding import lifecycle_source


BODY_MAIN=r'''
int main()
{
    MatrixProvider first,second;first.label=31;second.label=32;
    first.world=at(1,2,3);second.world=at(4,5,6);
    MatrixProviderPtr externalFirst(&first),externalSecond(&second);
    unsigned firstSamples=0,secondSamples=0;
    first.onMatrix=[&]{++firstSamples;};second.onMatrix=[&]{++secondSamples;};
    setStockTreeCollisionBody(&first);assert(first.references==2);
    tickStockTreeSceneRendering(.1f);assert(firstSamples==0);
    renderingOwner=std::make_shared<TreeRenderingOwner>();auto initial=renderingOwner;
    for(unsigned index=0;index!=3;++index){
        tickStockTreeSceneRendering(.1f);
        assert(!std::memcmp(first.world.m,initial->collisionBodyMatrix.data(),sizeof(Matrix)));
    }
    assert(firstSamples==3&&initial->time>0);
    setSpeedTreeCollisionBody(MatrixProviderPtr());tickStockTreeSceneRendering(.1f);
    assert(first.references==1&&initial->collisionBodyMatrix[15]==0);
    assert(!std::memcmp(first.world.m,initial->collisionBodyMatrix.data(),15*sizeof(float)));
    renderingOwner->active=false;setStockTreeCollisionBody(&first);
    tickStockTreeSceneRendering(.1f);assert(firstSamples==3);renderingOwner->active=true;
    // The executing provider must remain alive after replacing itself.
    first.dynamic=true;
    first.onMatrix=[&]{++firstSamples;setStockTreeCollisionBody(&second);assert(first.references>0&&!first.destroyed);};
    externalFirst=MatrixProviderPtr();tickStockTreeSceneRendering(.1f);
    assert(first.destroyed&&secondSamples==0&&initial->collisionBodyMatrix[15]==0);
    tickStockTreeSceneRendering(.1f);
    assert(!std::memcmp(second.world.m,initial->collisionBodyMatrix.data(),sizeof(Matrix)));
    // A -> None -> A still invalidates the in-flight sample.
    second.onMatrix=[&]{++secondSamples;setStockTreeCollisionBody(nullptr);setStockTreeCollisionBody(&second);};
    initial->collisionBodyMatrix[0]=-1;tickStockTreeSceneRendering(.1f);
    assert(initial->collisionBodyMatrix[0]==-1);
    // A callback may replace the renderer; neither owner's cache/clock may
    // receive the sample belonging to the old renderer identity.
    auto replacement=std::make_shared<TreeRenderingOwner>();
    second.onMatrix=[&]{++secondSamples;renderingOwner=replacement;};
    const float oldTime=initial->time;tickStockTreeSceneRendering(.1f);
    assert(initial->time==oldTime&&replacement->time==0&&replacement->collisionBodyMatrix[0]==0);
    second.onMatrix=[&]{++secondSamples;};tickStockTreeSceneRendering(.1f);
    assert(!std::memcmp(second.world.m,replacement->collisionBodyMatrix.data(),sizeof(Matrix)));
    // Closing refuses re-publication by an old provider's finalizer.
    MatrixProvider finalizer,attempted;finalizer.dynamic=true;
    finalizer.onDestroy=[&]{setStockTreeCollisionBody(&attempted);};
    setStockTreeCollisionBody(&finalizer);closeStockTreeCameraTarget();
    assert(finalizer.destroyed&&attempted.references==0&&!treeCollisionBody().target);
    setStockTreeCollisionBody(&attempted);assert(attempted.references==0);
    openStockTreeCameraTarget();setStockTreeCollisionBody(&attempted);assert(attempted.references==1);
    // Fini remains harmless after a partial start or repeated cleanup.
    finaliseStockTreeSceneRendering();
    assert(attempted.references==0&&!treeCollisionBody().target&&!renderingOwner);
    finaliseStockTreeSceneRendering();setStockTreeCollisionBody(&attempted);assert(attempted.references==0);
    openStockTreeCameraTarget();setStockTreeCollisionBody(&attempted);assert(attempted.references==1);
    // Fini from inside matrix() invalidates the sample before publication.
    renderingOwner=std::make_shared<TreeRenderingOwner>();auto closing=renderingOwner;
    attempted.onMatrix=[&]{finaliseStockTreeSceneRendering();};
    tickStockTreeSceneRendering(.1f);assert(!renderingOwner&&closing->collisionBodyMatrix[0]==0);
    assert(!closing->active&&closing->time==0&&!treeCollisionBody().target);
    closeStockTreeCameraTarget();
}
'''


class StockTreeCollisionBodyTests(unittest.TestCase):
    def test_production_active_owner_sampling_and_reentrant_teardown(self):
        requirements(self,oracle=False)
        scene=(ROOT/'bigworld/stock_tree_scene.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='tree-body-lifecycle-')as folder:
            root=Path(folder);fresh_engine(root);load_patch().patch(root)
            source=lifecycle_source(root).split('int main()')[0]
            source='#include <cassert>\n#include <mutex>\n'+source
            source=source.replace('struct EffectVisualContext {',
                'struct EffectVisualContext { enum{AUTO_CONSTANT_TYPE_PER_FRAME}; void unregisterAutoConstant(int,const char*){};')
            source=source.replace('bool active = true, gpuReady = true;',
                'bool active = true, gpuReady = true; void shutdown(){active=false;}')
            source=source.replace('std::shared_ptr<TreeRenderingOwner> renderingOwner;',
                'std::shared_ptr<TreeRenderingOwner> renderingOwner; std::mutex renderingOwnerMutex;')
            source+='\n'+function(scene,'void setSpeedTreeCollisionBody( MatrixProviderPtr target )')
            source+='\n'+function(scene,'void finaliseStockTreeSceneRendering()')+BODY_MAIN
            binary=compile_host(root,source,'tree_body_lifecycle')
            subprocess.run([str(binary)],check=True)

    def test_original_retention_and_both_pipeline_sampling_with_zero_srt(self):
        client=os.environ.get('WOT_0922_CLIENT')
        if not client:self.skipTest('WOT_0922_CLIENT required for original instructions')
        try:from stock_tree_collision_body_oracle import probe
        except ImportError:self.skipTest('pefile and Unicorn required for original instructions')
        result=probe(Path(client)/'WorldOfTanks.exe')
        self.assertEqual(result['tree_count'],0)
        self.assertEqual(result['samples_with_nullable_renderer'],0)
        self.assertEqual(result['samples_with_empty_renderer'],6)
        self.assertTrue(result['retention_without_renderer']and result['released_after_clear']and result['clear_only_m44'])


if __name__=='__main__':unittest.main()
