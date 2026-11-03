"""Execute SDK Context shutdown and the generated BW render-owner teardown.

The SDK owns lock/wait ordering. Host boundaries supply a deterministic render
queue and native owner destructors, without claiming a GPU or crash diagnosis.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_stock_flash_drawable_images import (HERE, build, engine, load_patch, native_scene_helpers, patcher, sdk,
                                               thread_patcher, threaded_executor)
from test_stock_vector4_basic import block


HOST = r'''
#define BW_GUARD
#define CONSUMER_CLIENT
#define INFO_MSG(...)
#define SF_DEBUG_WARNING(...)
struct Context;
struct Event {
    bool ready=false;
    void SetEvent(){ready=true;}
    void Wait(){assert(!sdkContextLock);StockRenderThread::drain();assert(ready);}
};
struct Lock {struct Locker {
    bool* held;explicit Locker(bool* value):held(value){assert(!*held);*held=true;}
    ~Locker(){*held=false;}
};};
struct ContextCaptureNotify {ContextCaptureNotify* pNext=nullptr;void OnShutdown(bool){}};
struct EmptyNotifyList {
    ContextCaptureNotify* GetFirst(){return nullptr;}
    bool IsNull(ContextCaptureNotify* value){return value==nullptr;}
};
struct RenderNotify {
    Render::ThreadCommandQueue* pRTCommandQueue;
    Context* context;
    struct ServiceCommand:Render::ThreadCommand {
        RenderNotify* owner;explicit ServiceCommand(RenderNotify* value):owner(value){}
        void Execute()override;
    } ServiceCommandInstance;
    RenderNotify(Render::ThreadCommandQueue* queue,Context* owner):
        pRTCommandQueue(queue),context(owner),ServiceCommandInstance(this){}
    // @SDK_PUSH@
};
struct Context {
    EmptyNotifyList CaptureNotifyList;
    bool DIChangesRequired=true,ShutdownRequested=false,MultiThreadedUse=true,handlesCleared=false;
    RenderNotify* pRenderer=nullptr;
    Event* pShutdownEvent=nullptr;
    struct CaptureLock {Context* pContext;};
    CaptureLock capture{this};CaptureLock* pCaptureLock=&capture;
    bool* getLock(){return &sdkContextLock;}
    void handleFinalizingSnaphot(){}
    void clearRTHandleList(){handlesCleared=true;}
    void shutdownRendering_NoLock(){assert(Scaleform::currentThread==2);pRenderer=nullptr;}
    void Shutdown(bool waitFlag);
};
// @SDK_SHUTDOWN@
void RenderNotify::ServiceCommand::Execute(){
    assert(Scaleform::currentThread==2&&!sdkContextLock);
    owner->context->pRenderer=nullptr;
    if(owner->context->pShutdownEvent){owner->context->pShutdownEvent->SetEvent();owner->context->pShutdownEvent=nullptr;}
}
std::vector<int> destroyed;
struct Owner {
    unsigned refs=1;int kind;Context* context;
    Owner(int value,Context* ctx):kind(value),context(ctx){}
    void AddRef(){++refs;}void Release(){if(!--refs)delete this;}
    ~Owner(){
        if(kind==1){assert(Scaleform::currentThread==1);context->Shutdown(true);}
        else if(kind==2||kind==3){assert(Scaleform::currentThread==2);assert(context->ShutdownRequested&&context->handlesCleared&&!context->pRenderer);}
        destroyed.push_back(kind);
    }
};
struct Loader {
    void SetVideo(void*){assert(Scaleform::currentThread==1);}
    void SetAudio(void*){assert(Scaleform::currentThread==1);}
    ~Loader(){assert(Scaleform::currentThread==1);destroyed.push_back(4);}
};
struct Manager {
    struct Overlay {void reset(){assert(Scaleform::currentThread==2);}} overlayTarget_;
    BW::ScaleformBW::StockFlashDrawableImageQueue drawableImageQueue_;
    Loader* pLoader_=new Loader;
    Ptr<Owner> pDrawTextManager_,pRenderer_,pRenderHAL_,pLogger_;
    void* pTexMgr_=nullptr;
    Context context;
    RenderNotify notify;
    Manager():drawableImageQueue_(&executeStockFlashDrawableImageCommand),notify(&drawableImageQueue_,&context){
        Render::Interfaces interfaces;interfaces.RenderThreadID=2;drawableImageQueue_.configure(interfaces);
        context.pRenderer=&notify;
        pDrawTextManager_=*new Owner(1,&context);pRenderer_=*new Owner(2,&context);
        pRenderHAL_=*new Owner(3,&context);pLogger_=*new Owner(5,&context);
    }
    bool doFini();
};
// @FINI@
int main(){
    Scaleform::currentThread=1;Manager manager;
    assert(manager.doFini());assert(!sdkContextLock&&StockRenderThread::jobs.empty());
    assert((destroyed==std::vector<int>{1,4,2,3,5}));
    Render::Interfaces interfaces;manager.drawableImageQueue_.GetRenderInterfaces(&interfaces);
    assert(interfaces.RenderThreadID==0);
    std::puts("PASS actual SDK shutdown lock/wait and render-thread renderer/HAL retirement");
}
'''


class ScaleformCommandShutdownTests(unittest.TestCase):
    def test_actual_sdk_shutdown_releases_lock_before_render_service(self):
        root = sdk(self)
        context = (root / 'Src/Render/Render_Context.cpp').read_text(encoding='utf-8-sig')
        context_header = (root / 'Src/Render/Render_Context.h').read_text(encoding='utf-8-sig')
        originals = engine(self)
        glyph = load_patch('patch_stock_glyph_cache')
        for relative, unused_edits in glyph.FILES:
            originals[relative] = glyph.patch_source(relative, originals[relative])
        generated = patcher.patch_sources(originals)[patcher.SOURCE]
        old_drain, old_drain_body = thread_patcher.EDITS[patcher.SOURCE][4]
        before = block(generated.replace(old_drain, old_drain_body, 1), 'bool Manager::doFini()')
        # Run the complete production manager transformer, including previous
        # viewport/render-job upgrades, from actual pinned SDK/BW source.
        final = thread_patcher.patch_render_thread_source(patcher.SOURCE, generated)
        after = block(final, 'bool Manager::doFini()')
        self.assertEqual(thread_patcher.patch_render_thread_source(patcher.SOURCE, final), final)
        legacy = final.replace(thread_patcher.command_scene_ownership(thread_patcher.EDITS[patcher.SOURCE][2][1]),
                               thread_patcher.LEGACY_COMMAND_EXECUTOR, 1).replace(after, before, 1)
        self.assertEqual(thread_patcher.patch_render_thread_source(patcher.SOURCE, legacy), final)
        prefix = (HERE / 'stock_flash_drawable_images_host.cpp').read_text()
        prefix = prefix[:prefix.index('bool queueReleased=')]
        prefix = prefix.replace('// @SCENE_HELPERS@', native_scene_helpers()).replace('@EXECUTOR@', threaded_executor())
        source = prefix + HOST.replace('// @SDK_PUSH@', block(context_header, 'void PushRTServiceQueuesCommand()')).replace(
            '// @SDK_SHUTDOWN@', block(context, 'void Context::Shutdown(bool waitFlag)'))
        with tempfile.TemporaryDirectory(prefix='scaleform-shutdown-') as temporary:
            binary = build(self, temporary, 'shutdown', source.replace('// @FINI@', after))
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS actual SDK shutdown', result.stdout)
            # The old generated destructor drains, but releases the renderer on
            # the caller. Run that actual old body against the same SDK test.
            binary = build(self, temporary, 'old-caller-retirement', source.replace('// @FINI@', before))
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0,
                                'Old caller-thread renderer retirement unexpectedly passed')


if __name__ == '__main__':
    unittest.main()
