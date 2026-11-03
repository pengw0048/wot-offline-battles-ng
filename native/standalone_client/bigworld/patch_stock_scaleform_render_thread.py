#!/usr/bin/env python3
"""Run Scaleform display jobs on the D3D9 render thread; AS3 advance and capture stay on the caller."""
import argparse
from pathlib import Path


EDITS = {
    'lib/scaleform/manager.cpp': (
        (r'''
#include "sys_alloc.hpp"

#if ENABLE_RESOURCE_COUNTERS
''',
         r'''
#include "sys_alloc.hpp"
#include "stock_scaleform_render_thread.hpp"

#if ENABLE_RESOURCE_COUNTERS
'''),
        (r'''// DrawableImage may flush synchronously while AS3 advances outside a frame.
// HALConfig_NoSceneCalls leaves the actual D3D scene to Moo's nested owner.
static void executeStockFlashDrawableImageCommand(Render::ThreadCommand* command)
{
    IDirect3DDevice9* device = Moo::rc().device();
''',
         r'''// DrawableImage may flush synchronously while AS3 advances outside a frame.
// HALConfig_NoSceneCalls leaves the actual D3D scene to Moo's nested owner.
static void executeStockFlashDrawableImageCommandNow(Render::ThreadCommand* command)
{
    IDirect3DDevice9* device = Moo::rc().device();
'''),
        (r'''    if (state) state->Release();
    if (device) Moo::rc().invalidateStateCache();
}

''',
         r'''    if (state) state->Release();
    if (device) Moo::rc().invalidateStateCache();
}
// Context shutdown and DrawableImage destruction can enqueue while holding
// SDK locks. Keep a lease and return; the HAL owner executes after admitted
// display jobs, once the producer has released its lock.
static void executeStockFlashDrawableImageCommand(Render::ThreadCommand* command)
{
    struct CommandJob
    {
        Ptr<Render::ThreadCommand> command;
        void operator()() { executeStockFlashDrawableImageCommandNow(command.GetPtr()); }
    };
    CommandJob* job = new CommandJob();
    job->command = command;
    StockRenderThread::postCommand(job);
}

'''),
        (r'''		// Configure renderer in "Dependent mode", honoring externally
		// configured device settings.
		if (!pRenderHAL_->InitHAL(Render::D3D9::HALInitParams(Moo::rc().device(),
			Moo::rc().presentParameters(), Render::D3D9::HALConfig_NoSceneCalls)))
		{
			ERROR_MSG( "%s: %s\n", "BW::Manager::doInit: ", "Couldn't initialize RenderHAL object");
''',
         r'''		// Configure renderer in "Dependent mode", honoring externally
		// configured device settings.
		const DWORD renderThread = StockRenderThread::id();
		if (!pRenderHAL_->InitHAL(Render::D3D9::HALInitParams(Moo::rc().device(),
			Moo::rc().presentParameters(), Render::D3D9::HALConfig_NoSceneCalls,
			renderThread ? (Scaleform::ThreadId)(size_t)renderThread : Scaleform::ThreadId())))
		{
			ERROR_MSG( "%s: %s\n", "BW::Manager::doInit: ", "Couldn't initialize RenderHAL object");
'''),
        (r'''	{
		BW_GUARD;

#ifndef CONSUMER_CLIENT
''',
         r'''	{
		BW_GUARD;
		// Finish every posted HAL frame before the renderer and HAL go away.
		{
			auto drain = []() {};
			StockRenderThread::runSync(drain);
		}

#ifndef CONSUMER_CLIENT
'''),
        (r'''		fullScreenViewport( gvFullScreen );

		if (glyphCacheParamsPending_)
		{
			pRenderer_->GetGlyphCacheConfig()->SetParams( glyphCacheParams_ );
			glyphCacheParamsPending_ = false;
		}

		pRenderer_->BeginFrame();

		for (BW::list<PyMovieView*>::iterator it = movies_.begin(); it != movies_.end(); ++it)
		{
			GFx::Movie *pMovieView = (*it)->pMovieView();
			GFx::MovieDisplayHandle &movieDisplayHandle = (*it)->movieDisplayHandle();

			pMovieView->Capture();
			pMovieView->SetViewport(gvFullScreen);
			if (movieDisplayHandle.NextCapture(pRenderer_->GetContextNotify()))
				pRenderer_->Display(movieDisplayHandle);
		}

		pRenderer_->EndFrame();

#if ENABLE_WATCHERS
		Render::HAL::Stats stats;
		pRenderHAL_->GetStats(&stats, false);
		lastNumDrawCalls_ = stats.Primitives;
		lastNumTriangles_ = stats.Triangles;
#endif

#if ENABLE_RESOURCE_COUNTERS
''',
         r'''		fullScreenViewport( gvFullScreen );

		// Capture stays here; the HAL frame runs on the render thread in queue order.
		struct GlobalMoviesJob
		{
			Ptr<Render::Renderer2D> renderer;
			Ptr<Render::D3D9::HAL> hal;
			BW::vector<GFx::MovieDisplayHandle> handles;
			bool glyphParams;
			Render::GlyphCacheParams params;
			Manager* owner;
			void operator()()
			{
				if (glyphParams) renderer->GetGlyphCacheConfig()->SetParams( params );
				renderer->BeginFrame();
				for (size_t i = 0; i < handles.size(); ++i)
					if (handles[i].NextCapture(renderer->GetContextNotify()))
						renderer->Display(handles[i]);
				renderer->EndFrame();
#if ENABLE_WATCHERS
				Render::HAL::Stats stats;
				hal->GetStats(&stats, false);
				owner->lastNumDrawCalls_ = stats.Primitives;
				owner->lastNumTriangles_ = stats.Triangles;
#endif
			}
		};
		GlobalMoviesJob* job = new GlobalMoviesJob();
		job->renderer = pRenderer_;
		job->hal = pRenderHAL_;
		job->glyphParams = glyphCacheParamsPending_;
		job->params = glyphCacheParams_;
		job->owner = this;
		glyphCacheParamsPending_ = false;
		for (BW::list<PyMovieView*>::iterator it = movies_.begin(); it != movies_.end(); ++it)
		{
			GFx::Movie *pMovieView = (*it)->pMovieView();
			pMovieView->Capture();
			pMovieView->SetViewport(gvFullScreen);
			job->handles.push_back((*it)->movieDisplayHandle());
		}
		StockRenderThread::postJob(job);

#if ENABLE_RESOURCE_COUNTERS
'''),
        (r'''
		if (pRenderHAL_)
			pRenderHAL_->PrepareForReset();
	}

''',
         r'''
		if (pRenderHAL_)
		{
			Render::D3D9::HAL* hal = pRenderHAL_.GetPtr();
			auto job = [hal]() { hal->PrepareForReset(); };
			StockRenderThread::runSync(job);
		}
	}

'''),
        (r'''
		if (pRenderHAL_)
			pRenderHAL_->RestoreAfterReset();
	}

''',
         r'''
		if (pRenderHAL_)
		{
			Render::D3D9::HAL* hal = pRenderHAL_.GetPtr();
			auto job = [hal]() { hal->RestoreAfterReset(); };
			StockRenderThread::runSync(job);
		}
	}

'''),
    ),
    'lib/scaleform/flash_gui_component.cpp': (
        (r'''#include "moo/viewport_setter.hpp"
#include "moo/scissors_setter.hpp"

DECLARE_DEBUG_COMPONENT2( "2DComponents", 0 )
''',
         r'''#include "moo/viewport_setter.hpp"
#include "moo/scissors_setter.hpp"
#include "stock_scaleform_render_thread.hpp"

DECLARE_DEBUG_COMPONENT2( "2DComponents", 0 )
'''),
        (r'''
	{
		PROFILER_SCOPED( GUI_FlashBeginFrame );
		pRenderer->BeginFrame();
	}

	{
		PROFILER_SCOPED( GUI_FlashCapture );
		pMovieView_->pMovieView()->Capture();
	}
	pMovieView_->pMovieView()->SetViewport( gv );
	GFx::MovieDisplayHandle &movieDisplayHandle = pMovieView_->movieDisplayHandle();
	bool stockNextCapture;
	{
		PROFILER_SCOPED( GUI_FlashNextCapture );
		stockNextCapture = movieDisplayHandle.NextCapture(pRenderer->GetContextNotify());
	}
	if (stockNextCapture)
	{
		PROFILER_SCOPED( GUI_FlashDisplay );
		pRenderer->Display(movieDisplayHandle);
	}

	{
		PROFILER_SCOPED( GUI_FlashEndFrame );
		pRenderer->EndFrame();
	}

''',
         r'''
	{
		PROFILER_SCOPED( GUI_FlashCapture );
		pMovieView_->pMovieView()->Capture();
	}
	pMovieView_->pMovieView()->SetViewport( gv );

	// The HAL frame runs on the render thread in queue order; Capture stays here.
	struct DisplayJob
	{
		Ptr<Render::Renderer2D> renderer;
		GFx::MovieDisplayHandle handle;
		void operator()()
		{
			renderer->BeginFrame();
			if (handle.NextCapture(renderer->GetContextNotify()))
				renderer->Display(handle);
			renderer->EndFrame();
		}
	};
	DisplayJob* job = new DisplayJob();
	job->renderer = pRenderer;
	job->handle = pMovieView_->movieDisplayHandle();
	{
		PROFILER_SCOPED( GUI_FlashDisplay );
		StockRenderThread::postJob(job);
	}

'''),
    ),
    'lib/scaleform/flash_text_gui_component.cpp': (
        (r'''#include "moo/effect_material.hpp"
#include "moo/geometrics.hpp"

DECLARE_DEBUG_COMPONENT2( "2DComponents", 0 )
''',
         r'''#include "moo/effect_material.hpp"
#include "moo/geometrics.hpp"
#include "stock_scaleform_render_thread.hpp"

DECLARE_DEBUG_COMPONENT2( "2DComponents", 0 )
'''),
        ('\tRender::TreeRootDisplayHandle &displayHandle = *const_cast<Render::TreeRootDisplayHandle*>(&Manager::instance().pTextManager()->GetDisplayHandle());',
         '\tPtr<GFx::DrawTextManager> textManager = Manager::instance().pTextManager();\n'
         '\tif (!textManager) return;'),
        (r'''

void FlashTextGUIComponent::drawSelf( bool reallyDraw, bool overlay )
{
''',
         r'''

namespace
{
	// Text display runs on the render thread with the rest of the HAL work.
	struct TextDisplayJob
	{
		Ptr<Render::Renderer2D> renderer;
		Render::TreeRootDisplayHandle handle;
		void operator()()
		{
			if (handle.NextCapture(renderer->GetContextNotify()))
				renderer->Display(handle);
		}
	};
	void postTextDisplay(Ptr<Render::Renderer2D>& renderer, Render::TreeRootDisplayHandle& handle)
	{
		TextDisplayJob* job = new TextDisplayJob();
		job->renderer = renderer;
		job->handle = handle;
		StockRenderThread::postJob(job);
	}
}

void FlashTextGUIComponent::drawSelf( bool reallyDraw, bool overlay )
{
'''),
        (r'''
		Manager::instance().pTextManager()->SetViewport(gv);
		if (displayHandle.NextCapture(pRenderer->GetContextNotify()))
			pRenderer->Display(displayHandle);
	}
	else
''',
         r'''
		Manager::instance().pTextManager()->SetViewport(gv);
		postTextDisplay(pRenderer, displayHandle);
	}
	else
'''),
        (r'''
		// draw text
		if (displayHandle.NextCapture(pRenderer->GetContextNotify()))
			pRenderer->Display(displayHandle);

		// We are done with the text
''',
         r'''
		// draw text
		postTextDisplay(pRenderer, displayHandle);

		// We are done with the text
'''),
        ('\t\t// begin text rendering\n\t\tManager::instance().pTextManager()->SetViewport(gv);',
         '\t\t// begin text rendering\n\t\ttextManager->SetViewport(gv);'),
    ),
    'lib/scaleform/py_movie_view.cpp': (
        (r'''#include "moo/render_context.hpp"
#include "ime.hpp"

BW_BEGIN_NAMESPACE
''',
         r'''#include "moo/render_context.hpp"
#include "ime.hpp"
#include "stock_scaleform_render_thread.hpp"

BW_BEGIN_NAMESPACE
'''),
        (r'''	{
		BW_GUARD;

#ifndef CONSUMER_CLIENT_BUILD
''',
         r'''	{
		BW_GUARD;
		// Queued display jobs may still reference this movie's context.
		StockRenderThread::drain();

#ifndef CONSUMER_CLIENT_BUILD
'''),
    ),
}


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError("Unexpected public engine source for " + label)
    return text.replace(old, new, 1)


HAL_HEADER = 'Src/Render/D3D9/D3D9_HAL.h'
HAL_CONTRACT = '''                  UInt32 halConfigFlags = 0,
                  ThreadId renderThreadId = ThreadId())'''

LEGACY_COMMAND_EXECUTOR = r'''    if (state) state->Release();
    if (device) Moo::rc().invalidateStateCache();
}
// The caller may hold Scaleform locks that render-thread work needs, so the
// command runs here once every queued HAL job has finished.
static void executeStockFlashDrawableImageCommand(Render::ThreadCommand* command)
{
    StockRenderThread::drain();
    executeStockFlashDrawableImageCommandNow(command);
}

'''


def render_thread_shutdown(text):
    # SDK producers can request Context retirement while releasing DrawText
    # and Loader on their original caller. Their commands must finish before
    # renderer/HAL destruction, which itself belongs to the actual HAL thread.
    before = '''\t\tpRenderer_ = NULL;
\t\tpRenderHAL_ = NULL;
\t\tdrawableImageQueue_.configure(Render::Interfaces());
\t\tpLogger_ = NULL;
\t\tpTexMgr_ = NULL;'''
    after = '''\t\t// Retiring caller-side contexts may admit more native commands.
\t\t// Establish their consumer fence before admitting final owner release.
\t\tStockRenderThread::drainCommands();
\t\t{
\t\t\tauto releaseRenderOwners = [this]() {
\t\t\t\tpRenderer_ = NULL;
\t\t\t\tpRenderHAL_ = NULL;
\t\t\t\tpTexMgr_ = NULL;
\t\t\t};
\t\t\tStockRenderThread::runSync(releaseRenderOwners);
\t\t}
\t\tdrawableImageQueue_.configure(Render::Interfaces());
\t\tpLogger_ = NULL;'''
    return replace_once(text, before, after, 'Scaleform render owner shutdown')


def command_scene_ownership(text):
    # The main thread keeps recording Moo state while native commands execute.
    # Its scene nesting counter, resource preload and profiling are not RT state.
    text = text.replace(
        '    const HRESULT scene = device ? Moo::rc().beginScene() : S_OK;',
        '    const bool nativeOwner = StockRenderThread::id() != 0;\n'
        '    bool openedScene = false;\n'
        '    const HRESULT scene = nativeOwner ?\n'
        '        ::wot::d3d9rt::begin_command_scene(device, openedScene) :\n'
        '        (device ? Moo::rc().beginScene() : S_OK);')
    text = text.replace(
        '    if (device && FAILED(Moo::rc().endScene()))',
        '    const HRESULT ended = nativeOwner ?\n'
        '        ::wot::d3d9rt::end_command_scene(device, openedScene) :\n'
        '        (device ? Moo::rc().endScene() : S_OK);\n'
        '    if (FAILED(ended))')
    return text.replace('    if (device) Moo::rc().invalidateStateCache();',
                        '    if (device && !nativeOwner) Moo::rc().invalidateStateCache();')


def viewport_before_capture(text):
    # #1513 aa84df/aa84f4 publishes the viewport before aa8504 captures it.
    # Capture freezes root viewport/matrix data; writing afterwards only
    # updates the next snapshot, including the first draw after a clip change.
    return text.replace(
        '\t\t\tpMovieView->Capture();\n\t\t\tpMovieView->SetViewport(gvFullScreen);',
        '\t\t\tpMovieView->SetViewport(gvFullScreen);\n\t\t\tpMovieView->Capture();').replace(
        '\t{\n\t\tPROFILER_SCOPED( GUI_FlashCapture );\n'
        '\t\tpMovieView_->pMovieView()->Capture();\n\t}\n'
        '\tpMovieView_->pMovieView()->SetViewport( gv );',
        '\tpMovieView_->pMovieView()->SetViewport( gv );\n'
        '\t{\n\t\tPROFILER_SCOPED( GUI_FlashCapture );\n'
        '\t\tpMovieView_->pMovieView()->Capture();\n\t}')


def render_job_ownership(text):
    text = text.replace('renderer->BeginFrame();', 'if (!renderer->BeginFrame()) return;')
    text = text.replace(
        '\t\tRender::TreeRootDisplayHandle handle;',
        '\t\tPtr<GFx::DrawTextManager> owner;\n\t\tRender::TreeRootDisplayHandle handle;')
    text = text.replace(
        '\t\t\tif (handle.NextCapture(renderer->GetContextNotify()))\n'
        '\t\t\t\trenderer->Display(handle);\n\t\t}',
        '\t\t\tif (!renderer->BeginFrame()) return;\n'
        '\t\t\tif (handle.NextCapture(renderer->GetContextNotify()))\n'
        '\t\t\t\trenderer->Display(handle);\n'
        '\t\t\trenderer->EndFrame();\n\t\t}')
    if 'struct TextDisplayJob' in text:
        text = text.replace(
            '\t\tPtr<GFx::DrawTextManager> owner;\n',
            '\t\tPtr<GFx::DrawTextManager> owner;\n\t\tbool overlay;\n')
        text = text.replace(
            '\t\t\tif (!renderer->BeginFrame()) return;\n',
            '\t\t\tif (!renderer->BeginFrame()) return;\n'
            '\t\t\tif (!overlay)\n\t\t\t{\n'
            '\t\t\t\tif (!renderer->BeginScene()) { renderer->EndFrame(); return; }\n'
            '\t\t\t\t// BeginDisplay keeps an explicitly opened scene. Restore the\n'
            '\t\t\t\t// original world-text depth mask after HAL scene defaults.\n'
            '\t\t\t\tIDirect3DDevice9* device = Moo::rc().device();\n'
            '\t\t\t\tdevice->SetRenderState(D3DRS_STENCILENABLE, TRUE);\n'
            '\t\t\t\tdevice->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);\n'
            '\t\t\t\tdevice->SetRenderState(D3DRS_STENCILREF, 2);\n'
            '\t\t\t\tdevice->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);\n'
            '\t\t\t\tdevice->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);\n'
            '\t\t\t}\n')
        text = text.replace('\t\t\trenderer->EndFrame();\n',
                            '\t\t\tif (!overlay) renderer->EndScene();\n\t\t\trenderer->EndFrame();\n')
    text = text.replace(
        'void postTextDisplay(Ptr<Render::Renderer2D>& renderer, Render::TreeRootDisplayHandle& handle)',
        'void drawTextDisplay(Ptr<Render::Renderer2D>& renderer, const Ptr<GFx::DrawTextManager>& textManager, bool overlay)')
    text = text.replace(
        '\t\tjob->handle = handle;\n\t\tStockRenderThread::postJob(job);',
        '\t\tjob->owner = textManager;\n'
        '\t\tjob->overlay = overlay;\n'
        '\t\ttextManager->Capture();\n'
        '\t\tjob->handle = textManager->GetDisplayHandle();\n'
        '\t\tStockRenderThread::postJob(job);\n'
        '\t\t// All components share this context; consume this snapshot before\n'
        '\t\t// the next component changes its viewport or text transforms.\n'
        '\t\tStockRenderThread::drain();')
    text = text.replace('postTextDisplay(pRenderer, displayHandle);', 'drawTextDisplay(pRenderer, textManager, overlay);')
    text = text.replace('Manager::instance().pTextManager()->SetViewport(gv);', 'textManager->SetViewport(gv);')
    return text.replace(
        'auto job = [hal]() { hal->RestoreAfterReset(); };',
        'auto job = [hal]() {\n'
        '\t\t\t\tif (!hal->RestoreAfterReset())\n'
        '\t\t\t\t\tERROR_MSG("Scaleform: HAL restore after device reset failed\\n");\n'
        '\t\t\t};')


def overlay_surface_ownership(text):
    if 'struct DisplayJob' in text:
        text = text.replace('\t\tGFx::MovieDisplayHandle handle;',
                            '\t\tGFx::MovieDisplayHandle handle;\n\t\tbool overlay;')
        text = text.replace('\tjob->renderer = pRenderer;',
                            '\tjob->renderer = pRenderer;\n\tjob->overlay = overlay;')
    if any(marker in text for marker in ('struct GlobalMoviesJob', 'struct DisplayJob', 'struct TextDisplayJob')):
        overlay = 'true' if 'struct GlobalMoviesJob' in text else 'overlay'
        indent = '\t\t\t\t' if 'struct GlobalMoviesJob' in text else '\t\t\t'
        frame = indent + 'if (!renderer->BeginFrame()) return;'
        scope = (indent + 'StockFlashOverlayTarget::Scope target(Manager::instance().overlayTarget(),\n' +
                 indent + '\tstatic_cast<Render::D3D9::HAL*>(renderer->GetHAL()), Moo::rc().device(), ' + overlay + ');\n' +
                 indent + 'if (!target) return;\n')
        text = text.replace(frame, scope + frame)
    text = text.replace('auto drain = []() {};', 'auto drain = [this]() { overlayTarget_.reset(); };')
    return text.replace('auto job = [hal]() { hal->PrepareForReset(); };',
                        'auto job = [hal, this]() { overlayTarget_.reset(); hal->PrepareForReset(); };')


def patch_overlay_lifecycle(root):
    path = Path(root) / 'lib/scaleform/manager.hpp'
    text = path.read_text()
    for before, after in (
            ('#include "loader.hpp"', '#include "loader.hpp"\n#include "stock_flash_overlay_target.hpp"'),
            ('\t\tPtr<Render::Renderer2D> & pRenderer() { return pRenderer_; }',
             '\t\tStockFlashOverlayTarget& overlayTarget() { return overlayTarget_; }\n'
             '\t\tPtr<Render::Renderer2D> & pRenderer() { return pRenderer_; }'),
            ('\t\tRender::D3D9::TextureManager *pTexMgr_;',
             '\t\tRender::D3D9::TextureManager *pTexMgr_;\n\t\tStockFlashOverlayTarget overlayTarget_;')):
        text = replace_once(text, before, after, 'Scaleform overlay surface lifecycle')
    path.write_text(text)


def patch_render_thread_source(relative, text):
    edits = EDITS[relative]
    if relative == 'lib/scaleform/manager.cpp' and LEGACY_COMMAND_EXECUTOR in text:
        text = replace_once(text, LEGACY_COMMAND_EXECUTOR, edits[2][1],
                            'Scaleform asynchronous command upgrade')
    for old, new in edits:
        viewport_ordered = viewport_before_capture(new)
        render_owned = render_job_ownership(viewport_ordered)
        ordered = overlay_surface_ownership(render_owned)
        if relative == 'lib/scaleform/manager.cpp':
            ordered = command_scene_ownership(ordered)
        # Upgrade the preceding render-thread adapter in cached SDK trees.
        for previous in (new, viewport_ordered, render_owned):
            if ordered != previous and previous in text:
                text = replace_once(text, previous, ordered, relative)
        text = replace_once(text, old, ordered, relative)
    if relative == 'lib/scaleform/manager.cpp':
        text = render_thread_shutdown(text)
        text = command_scene_ownership(text)
    return text


def patch(root, scaleform_root):
    # Display jobs rely on the D3D9 HAL accepting the render thread at init.
    if HAL_CONTRACT not in (Path(scaleform_root) / HAL_HEADER).read_text():
        raise RuntimeError("Scaleform D3D9 HAL lacks the render thread init parameter")
    patch_reset_lifecycle(root)
    for relative in EDITS:
        path = Path(root) / relative
        path.write_text(patch_render_thread_source(relative, path.read_text()))
    patch_overlay_lifecycle(root)


def patch_reset_lifecycle(root):
    path = Path(root) / 'lib/scaleform/manager.hpp'
    old = '\t\tvoid createUnmanagedObjects();'
    new = (old + '\n'
           '\t\t// ResetEx preserves textures, but replaces HAL backbuffer/render-buffer state.\n'
           '\t\tbool recreateForD3DExDevice() const override { return true; }')
    path.write_text(replace_once(path.read_text(), old, new, 'Scaleform ResetEx lifecycle'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    parser.add_argument("scaleform_root", type=Path)
    args = parser.parse_args()
    patch(args.engine_root, args.scaleform_root)


if __name__ == "__main__":
    main()
