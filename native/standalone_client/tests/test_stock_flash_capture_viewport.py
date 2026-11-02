"""Publish viewport changes before freezing Flash's copy-on-write render snapshot."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from stock_battle_gui_support import client_executable
from test_stock_cursor_camera import function
from test_stock_scaleform_scene import REVISION

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('flash_viewport_capture_patch', ROOT / 'bigworld/patch_stock_scaleform_render_thread.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class FlashCaptureViewportTests(unittest.TestCase):
    def test_exact_1513_component_publishes_viewport_before_capture(self):
        exe = client_executable(self)
        # FlashGUIComponent::drawSelf's viewport producer is aaf080. The two
        # Movie SetViewport calls precede Movie Capture (whose bool is true).
        self.assertEqual(exe.code(0xaa84df, 3), bytes.fromhex('ff5030'))
        self.assertEqual(exe.code(0xaa84f4, 3), bytes.fromhex('ff5030'))
        self.assertEqual(exe.code(0xaa84fd, 2), bytes.fromhex('6a01'))
        self.assertEqual(exe.code(0xaa8504, 3), bytes.fromhex('ff5064'))

    def test_fresh_cached_and_repeated_patch_use_the_same_order(self):
        with tempfile.TemporaryDirectory(prefix='flash-capture-patch-') as temp:
            root = Path(temp)
            sdk = root / 'sdk'
            (sdk / PATCH.HAL_HEADER).parent.mkdir(parents=True)
            (sdk / PATCH.HAL_HEADER).write_text(PATCH.HAL_CONTRACT)
            header = root / 'lib/scaleform/manager.hpp'
            header.parent.mkdir(parents=True)
            header.write_text('#include "loader.hpp"\n\t\tvoid createUnmanagedObjects();\n'
                              '\t\tPtr<Render::Renderer2D> & pRenderer() { return pRenderer_; }\n'
                              '\t\tRender::D3D9::TextureManager *pTexMgr_;')
            for relative, edits in PATCH.EDITS.items():
                (root / relative).write_text('\n'.join(old for old, unused in edits))
            PATCH.patch(root, sdk)
            expected = {name: (root / name).read_text() for name in PATCH.EDITS}
            PATCH.patch(root, sdk)
            self.assertEqual(expected, {name: (root / name).read_text() for name in expected})
            # Recreate precisely the preceding adapter, then upgrade in place.
            for relative, edits in PATCH.EDITS.items():
                text = expected[relative]
                for unused, previous in edits:
                    ordered = PATCH.overlay_surface_ownership(PATCH.render_job_ownership(PATCH.viewport_before_capture(previous)))
                    if ordered != previous:
                        self.assertEqual(text.count(ordered), 1)
                        text = text.replace(ordered, previous)
                (root / relative).write_text(text)
            PATCH.patch(root, sdk)
            self.assertEqual(expected, {name: (root / name).read_text() for name in expected})

    def test_global_and_component_sequences_freeze_the_current_viewport(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        sdk = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
            '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        def source(name):
            try:
                return subprocess.run(['git', '-C', str(sdk), 'show', REVISION + ':' + name],
                                      check=True, capture_output=True, text=True).stdout
            except (OSError, subprocess.CalledProcessError):
                self.skipTest('Pinned Scaleform SDK checkout required')
        # Keep the actual SDK copy-on-write implementation. Snapshot/page
        # capture services below are explicit boundaries, rather than a GPU fake.
        writable = function(source('Src/Render/Render_Context.cpp'), 'EntryData* Entry::getWritableData(')
        viewport = function(source('Src/Render/Render_TreeNode.cpp'), 'void TreeRoot::SetViewport(')
        global_previous = next(new for old, new in PATCH.EDITS['lib/scaleform/manager.cpp']
                               if 'GlobalMoviesJob' in new)
        component_previous = next(new for old, new in PATCH.EDITS['lib/scaleform/flash_gui_component.cpp']
                                  if 'struct DisplayJob' in new)
        with tempfile.TemporaryDirectory(prefix='flash-capture-snapshot-') as temp:
            root = Path(temp)
            for label, order in (('production', PATCH.viewport_before_capture), ('old-owner', lambda text: text)):
                global_calls = '\n'.join(re.findall(r'pMovieView->(?:Capture\(\)|SetViewport\(gvFullScreen\));',
                                                     order(global_previous)))
                component_calls = '\n'.join(re.findall(r'pMovieView_->pMovieView\(\)->(?:Capture\(\)|SetViewport\( gv \));',
                                                        order(component_previous)))
                self.assertEqual(len(global_calls.splitlines()), 2)
                self.assertEqual(len(component_calls.splitlines()), 2)
                generated = FIXTURE.replace('@WRITABLE@', writable).replace('@VIEWPORT@', viewport)
                generated = generated.replace('@GLOBAL@', global_calls).replace('@COMPONENT@', component_calls)
                path = root / (label + '.cpp')
                path.write_text(generated)
                binary = root / label
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                for owner in ('global', 'component'):
                    result = subprocess.run([str(binary), owner], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0 if label == 'production' else 41,
                                     (label, owner, result.returncode, result.stdout, result.stderr))

    def test_text_capture_frame_and_shared_owner_are_one_consumed_transaction(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        sdk = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT',
            '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        def source(name):
            try:
                return subprocess.run(['git', '-C', str(sdk), 'show', REVISION + ':' + name],
                                      check=True, capture_output=True, text=True).stdout
            except (OSError, subprocess.CalledProcessError):
                self.skipTest('Pinned Scaleform SDK checkout required')
        draw = source('Src/GFx/GFx_DrawText.cpp')
        actual = '\n'.join(function(draw, signature) for signature in (
            'void DrawTextManager::Capture(',
            'const Render::TreeRootDisplayHandle& DrawTextManager::GetDisplayHandle() const'))
        base = FIXTURE[:FIXTURE.index('struct Movie{')]
        base = base.replace('@WRITABLE@', function(source('Src/Render/Render_Context.cpp'), 'EntryData* Entry::getWritableData('))
        base = base.replace('@VIEWPORT@', function(source('Src/Render/Render_TreeNode.cpp'), 'void TreeRoot::SetViewport('))
        base = base.replace('Viewport VP{0,0,0,0};', 'int transform=0;Viewport VP{0,0,0,0};')
        base = base.replace('bool operator!=', 'bool GetClippedRect(int*)const{return width>0&&height>0;}bool operator!=')
        begin_display = function(source('Src/Render/Render_HAL.cpp'), 'void HAL::beginDisplay(')
        begin_display = begin_display.replace('void HAL::beginDisplay(', 'void DisplayScene::beginDisplay(')
        begin_display = begin_display.replace('vpin.Width', 'vpin.width').replace('vpin.Height', 'vpin.height')
        begin_mask = function(source('Src/Render/D3D9/D3D9_HAL.h'), 'virtual void    beginMaskDisplay()')
        begin_mask = begin_mask.replace('virtual void    beginMaskDisplay()', 'void DisplayScene::beginMaskDisplay()')
        text_output = next(new for old, new in PATCH.EDITS['lib/scaleform/flash_text_gui_component.cpp']
                           if 'struct TextDisplayJob' in new)
        text_output = PATCH.render_job_ownership(PATCH.viewport_before_capture(text_output))
        text_jobs = function(text_output, 'struct TextDisplayJob') + ';\n' + function(text_output, 'void drawTextDisplay(')
        global_output = next(new for old, new in PATCH.EDITS['lib/scaleform/manager.cpp'] if 'struct GlobalMoviesJob' in new)
        global_job = function(PATCH.render_job_ownership(global_output), 'struct GlobalMoviesJob') + ';'
        component_output = next(new for old, new in PATCH.EDITS['lib/scaleform/flash_gui_component.cpp'] if 'struct DisplayJob' in new)
        component_job = function(PATCH.render_job_ownership(component_output), 'struct DisplayJob') + ';'
        variants = {
            'production': (text_jobs, 0),
            'no-capture': (text_jobs.replace('textManager->Capture();', ''), 41),
            'no-frame': (text_jobs.replace('if (!renderer->BeginFrame()) return;', '').replace('renderer->EndFrame();', ''), 42),
            'unconsumed-owner': (text_jobs.replace('StockRenderThread::drain();', ''), 43),
            'lost-world-mask': (text_jobs.replace('device->SetRenderState(D3DRS_STENCILENABLE, TRUE);', ''), 44),
        }
        with tempfile.TemporaryDirectory(prefix='flash-text-snapshot-') as temp:
            root = Path(temp)
            for label, (jobs, expected) in variants.items():
                generated = base + TEXT_FIXTURE.replace('@SDK@', actual).replace('@TEXT@', jobs)
                generated = generated.replace('@GLOBAL@', global_job).replace('@COMPONENT@', component_job)
                generated = generated.replace('@BEGIN_DISPLAY@', begin_display).replace('@BEGIN_MASK@', begin_mask)
                path = root / (label + '.cpp')
                path.write_text(generated)
                binary = root / label
                subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected,
                                 (label, result.returncode, result.stdout, result.stderr))


FIXTURE = r'''
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#define SF_DEBUG_ASSERT(value, message) assert(value)
unsigned GetCurrentThreadId(){return 1;}
struct Viewport{int x,y,width,height;bool operator!=(const Viewport&v)const{return x!=v.x||y!=v.y||width!=v.width||height!=v.height;}};
struct EntryChange{unsigned ChangeBits=0;};
struct EntryData{virtual ~EntryData()=default;virtual EntryData*ConstructCopy(void*)=0;};
std::vector<std::unique_ptr<EntryData>> allocations;
struct Snapshot{EntryChange change;void*GetChangeHeap(){return nullptr;}
EntryChange*AddChangeItem(void*,unsigned bits){change.ChangeBits=bits;return &change;}};
struct Context{unsigned CaptureThreadId=1;};
struct Entry{EntryData*live=nullptr;EntryChange*pChange=nullptr;Snapshot snapshot;Context context;
Context*getContext(){return &context;}EntryData*getWritableData(unsigned);};
struct EntryRef{Entry*entry;explicit EntryRef(Entry*e):entry(e){}EntryData*&GetDataRef(){return entry->live;}
Snapshot*GetSnapshot(){return &entry->snapshot;}};
enum{Change_Viewport=1};
struct TreeRoot:Entry{
 struct NodeData:EntryData{Viewport VP{0,0,0,0};EntryData*ConstructCopy(void*)override{
 allocations.emplace_back(new NodeData(*this));return allocations.back().get();}};
 TreeRoot(){allocations.emplace_back(new NodeData);live=allocations.back().get();}
 const NodeData*GetReadOnlyData(){return static_cast<NodeData*>(live);}
 NodeData*GetWritableData(unsigned bits){return static_cast<NodeData*>(getWritableData(bits));}
 void SetViewport(const Viewport&);
};
@WRITABLE@
@VIEWPORT@
struct Movie{TreeRoot root;const TreeRoot::NodeData*captured=nullptr;
 void SetViewport(const Viewport&v){root.SetViewport(v);}
 void Capture(){captured=root.GetReadOnlyData();root.pChange=nullptr;}
};
struct Component{Movie*movie;Movie*pMovieView(){return movie;}};
void global(Movie*pMovieView,const Viewport&gvFullScreen){@GLOBAL@}
void component(Component*pMovieView_,const Viewport&gv){@COMPONENT@}
int main(int argc,char**argv){
 assert(argc==2);Movie movie;
 Viewport first{0,0,1920,1080},second{300,600,220,80};
 movie.SetViewport(first);movie.Capture();auto*frozen=movie.captured;
 movie.SetViewport(second);
 assert(!(frozen->VP!=first)&&!(movie.root.GetReadOnlyData()->VP!=second));
 assert(frozen!=movie.root.GetReadOnlyData()); // Actual SDK copy-on-write.
 // A newly displayed tooltip and successive clip/viewport moves must be in
 // the snapshot admitted for that draw, even if live state changes afterwards.
 Movie fresh;Component tooltip{&fresh};
 for(const auto&vp:{first,second,Viewport{20,40,640,480},first}){
  if(std::string(argv[1])=="global")global(&fresh,vp);else component(&tooltip,vp);
  if(fresh.captured->VP!=vp)return 41;
  auto*admitted=fresh.captured;fresh.SetViewport({1,2,3,4});
  assert(!(admitted->VP!=vp));
 }
}
'''


TEXT_FIXTURE = r'''
#include <functional>
#include <map>
#define SF_ASSERT(value) assert(value)
template<class T>using Ptr=std::shared_ptr<T>;
enum{D3DRS_STENCILENABLE=52,D3DRS_STENCILFUNC=56,D3DRS_STENCILREF=57,
 D3DRS_STENCILPASS=55,D3DRS_STENCILZFAIL=54,D3DCMP_EQUAL=3,D3DSTENCILOP_KEEP=1};
#define TRUE true
struct IDirect3DDevice9{std::map<int,int>states;void SetRenderState(int name,int value){states[name]=value;}};
IDirect3DDevice9 device;
namespace Moo{struct Context{IDirect3DDevice9*device(){return &::device;}};Context&rc(){static Context v;return v;}}
enum{HS_InFrame=1,HS_InScene=2,HS_SceneInDisplay=4,HS_InDisplay=8,HS_ViewValid=16,HS_DrawingMask=32,Event_Display=0};
struct Color{int GetAlpha(){return 0;}};
template<class T>struct Rect{Rect(int,int){}};
struct BeginDisplayData{Color BackgroundColor;Viewport VP;};
struct DisplayScene{
 unsigned HALState=0,MaskStackTop=0;bool StencilChecked=false,StencilAvailable=false,
 MultiBitStencil=false,DepthBufferAvailable=false,fail=false;
 struct Events{void Begin(const char*){}}events;Events&GetEvent(int){return events;}
 struct DataList{void Free(BeginDisplayData*){}}BeginDisplayDataList;
 struct Profile{void SetDrawMode(int){}}Profiler;
 struct Matrix{Viewport SetOrientation(const Viewport&v){return v;}}matrix;Matrix*Matrices=&matrix;
 Viewport VP{};int ViewRect=0,CurrentBlendState=0;
 bool checkState(unsigned bits,const char*){return (HALState&bits)==bits;}
 bool BeginScene(){if(fail||!(HALState&HS_InFrame))return false;HALState|=HS_InScene;device.SetRenderState(D3DRS_STENCILENABLE,false);return true;}
 void applyBlendMode(int){}void updateViewport(){}void clearSolidRectangle(Rect<int>,Color){}
 void beginDisplay(BeginDisplayData*);void beginMaskDisplay();
};
@BEGIN_DISPLAY@
@BEGIN_MASK@
struct TextContext{
 TreeRoot root;const TreeRoot::NodeData*captured=nullptr;unsigned captures=0,next=0;
 bool HasChanges(){return root.pChange!=nullptr;}
 void Capture(){++captures;captured=root.GetReadOnlyData();root.pChange=nullptr;}
};
namespace Render{
struct TreeRootDisplayHandle{TextContext*context=nullptr;
 bool NextCapture(void*){++context->next;return context->captured!=nullptr;}};
struct GlyphCacheParams{};namespace D3D9{struct HAL{};}
struct Renderer2D{
 struct Receipt{Viewport vp;int transform;bool worldMasked;};std::vector<Receipt>draws;
 DisplayScene scene;bool fail=false,failScene=false,inFrame=false,invalid=false;unsigned begins=0,ends=0;
 bool BeginFrame(){++begins;if(fail)return false;assert(!inFrame);inFrame=true;scene.HALState=HS_InFrame;return true;}
 void EndFrame(){assert(inFrame);++ends;inFrame=false;}
 bool BeginScene(){scene.fail=failScene;return scene.BeginScene();}
 void EndScene(){assert(scene.HALState&HS_InScene);scene.HALState&=~HS_InScene;}
 void*GetContextNotify(){return nullptr;}
 void Display(const TreeRootDisplayHandle&handle){if(!inFrame){invalid=true;return;}
 auto*data=handle.context->captured;assert(data);BeginDisplayData display{Color{},data->VP};scene.beginDisplay(&display);
 bool mask=device.states[D3DRS_STENCILENABLE]&&device.states[D3DRS_STENCILFUNC]==D3DCMP_EQUAL&&device.states[D3DRS_STENCILREF]==2;
 draws.push_back({data->VP,data->transform,mask});
 if(scene.HALState&HS_SceneInDisplay)scene.HALState&=~(HS_InScene|HS_SceneInDisplay);
 scene.HALState&=~HS_InDisplay;}
 struct Config{void SetParams(const GlyphCacheParams&){}}config;
 Config*GetGlyphCacheConfig(){return &config;}
};}
namespace GFx{
using MovieDisplayHandle=Render::TreeRootDisplayHandle;
struct DrawTextManagerImpl{TextContext RenderContext;Render::TreeRootDisplayHandle DispHandle{&RenderContext};};
struct DrawTextManager{
 DrawTextManagerImpl data;DrawTextManagerImpl*pImpl=&data;
 void Capture(bool onChangeOnly=true);const Render::TreeRootDisplayHandle&GetDisplayHandle()const;
 void SetViewport(const Viewport&vp){data.RenderContext.root.SetViewport(vp);}
 void SetTransform(int value){auto*d=data.RenderContext.root.GetWritableData(2);d->transform=value;}
};
@SDK@
}
namespace BW{template<class T>using vector=std::vector<T>;}
struct Manager{};
struct StockRenderThread{
 static std::vector<std::function<void()>>queue;
 template<class T>static void postJob(T*job){queue.emplace_back([job]{(*job)();delete job;});}
 static void drain(){while(!queue.empty()){auto pending=std::move(queue);queue.clear();for(auto&job:pending)job();}}
};
std::vector<std::function<void()>>StockRenderThread::queue;
@GLOBAL@
@COMPONENT@
@TEXT@
int main(){
 auto owner=std::make_shared<GFx::DrawTextManager>();auto renderer=std::make_shared<Render::Renderer2D>();
 Viewport a{10,20,80,40},b{30,60,120,50};
 owner->SetViewport(a);owner->SetTransform(11);drawTextDisplay(renderer,owner,true);
 owner->SetViewport(b);owner->SetTransform(22);drawTextDisplay(renderer,owner,true);
 StockRenderThread::drain();
 if(renderer->invalid)return 42;
 if(renderer->draws.size()!=2)return 41;
 if(renderer->draws[0].vp!=a||renderer->draws[0].transform!=11||
    renderer->draws[1].vp!=b||renderer->draws[1].transform!=22)return 43;
 assert(owner->data.RenderContext.captures==2&&renderer->begins==2&&renderer->ends==2);
 assert(!renderer->draws[0].worldMasked&&!renderer->draws[1].worldMasked);
 drawTextDisplay(renderer,owner,false);
 if(renderer->draws.size()!=3||!renderer->draws.back().worldMasked)return 44;
 renderer->fail=true;unsigned next=owner->data.RenderContext.next;
 GlobalMoviesJob global;global.renderer=renderer;global.glyphParams=false;
 global.handles.push_back(owner->GetDisplayHandle());global();
 DisplayJob component;component.renderer=renderer;component.handle=owner->GetDisplayHandle();component();
 drawTextDisplay(renderer,owner,true);
 assert(owner->data.RenderContext.next==next&&renderer->ends==3&&renderer->draws.size()==3);
 renderer->fail=false;renderer->failScene=true;drawTextDisplay(renderer,owner,false);
 assert(owner->data.RenderContext.next==next&&renderer->ends==4&&renderer->draws.size()==3);
}
'''


if __name__ == '__main__':
    unittest.main()
