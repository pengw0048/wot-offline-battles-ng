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
// The caller may hold Scaleform locks that render-thread work needs, so the
// command runs here once every queued HAL job has finished.
static void executeStockFlashDrawableImageCommand(Render::ThreadCommand* command)
{
    StockRenderThread::drain();
    executeStockFlashDrawableImageCommandNow(command);
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


def patch(root, scaleform_root):
    # Display jobs rely on the D3D9 HAL accepting the render thread at init.
    if HAL_CONTRACT not in (Path(scaleform_root) / HAL_HEADER).read_text():
        raise RuntimeError("Scaleform D3D9 HAL lacks the render thread init parameter")
    for relative, edits in EDITS.items():
        path = Path(root) / relative
        text = path.read_text()
        for old, new in edits:
            text = replace_once(text, old, new, relative)
        path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    parser.add_argument("scaleform_root", type=Path)
    args = parser.parse_args()
    patch(args.engine_root, args.scaleform_root)


if __name__ == "__main__":
    main()
