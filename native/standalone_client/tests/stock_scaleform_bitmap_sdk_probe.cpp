// Real SDK BitmapData lifetime regression using the client's HeapPT allocator.
#ifdef _MSC_VER
#pragma warning(push)
// SDK headers carry warnings unrelated to the probe assertions.
#pragma warning(disable: 4244 4100 4189 4456 4458 4005)
#endif
#include <GFx/GFx_Loader.h>
#include <GFx/GFx_AS3Support.h>
#include <GFx/GFx_PlayerImpl.h>
#include <GFx/AMP/Amp_Server.h>
#include <GFx/AS3/AS3_MovieRoot.h>
#include <GFx/AS3/Obj/Display/AS3_Obj_Display_BitmapData.h>
#include <GFx/AS3/Obj/Display/AS3_Obj_Display_Bitmap.h>
#include <GFx/AS3/Obj/Display/AS3_Obj_Display_Graphics.h>
#include <GFx/AS3/AS3_AvmBitmap.h>
#include <GFx/GFx_MovieDef.h>
#include <GFx/GFx_ImageResource.h>
#include <GFx/GFx_ImageCreator.h>
#include <GFx/GFx_DrawingContext.h>
#include <Render/Render_TreeShape.h>
#include <Render/Render_DrawableImage.h>
#include <Kernel/HeapPT/HeapPT_SysAllocWinAPI.h>
#include <AS3/AS3_Global.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace Scaleform;
using namespace Scaleform::GFx;

// FWS v9, minimal RECT/frame header, AS3 FileAttributes, ShowFrame and End.
// The immutable buffer outlives every MemoryFile lease; no disk fixture is needed.
class ProbeFileOpener : public FileOpener
{
public:
    File* OpenFile(const char* path, int, int) override
    {
        static const UByte swf[] = {
            0x46, 0x57, 0x53, 0x09, 0x18, 0x00, 0x00, 0x00,
            0x08, 0x00, 0x00, 0x0c, 0x01, 0x00, 0x44, 0x11,
            0x08, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00
        };
        static const UByte png[] = {
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
            0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
            0x0b, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0x0f, 0x04, 0x00,
            0x09, 0xfb, 0x03, 0xfd, 0xfb, 0x5e, 0x6b, 0x2b, 0x00, 0x00, 0x00, 0x00,
            0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
        };
        if (std::strncmp(path, "lifetime-", 9) == 0) return new MemoryFile(String(path), png, sizeof(png));
        if (std::strcmp(path, "bitmap-empty.swf") != 0) return NULL;
        return new MemoryFile(String(path), swf, sizeof(swf));
    }
    SInt64 GetFileModifyTime(const char*) override { return 0; }
};

static void check(bool condition, const char* label)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(2); }
}


// Ownership tests retain existing PNG delegates and perform no drawing. Fail
// if a scenario unexpectedly asks this queue to execute a rendering command.
struct NoRenderQueue : Render::ThreadCommandQueue
{
    void PushThreadCommand(Render::ThreadCommand*) override
    { check(false, "unexpected rendering command in lifetime probe"); }
};

using BitmapData = AS3::Instances::fl_display::BitmapData;

struct Probe
{
    Loader& loader;
    AS3::ASVM& vm;
    NoRenderQueue& queue;
    AS3::Value create(const char* name)
    {
        AS3::Value value;
        AS3::Value args[] = { AS3::Value(SInt32(1)), AS3::Value(SInt32(1)) };
        unsigned argc = std::strcmp(name, "flash.display.BitmapData") == 0 ? 2 : 0;
        check(vm.CreateObject(name, vm.GetFrameAppDomain(), value, argc, args), name);
        check(!vm.IsException(), "create exception");
        return value;
    }
    void set(const AS3::Value& object, const char* key, const AS3::Value& value)
    {
        const AS3::Multiname name(vm.GetPublicNamespace(),
                                  AS3::Value(vm.GetStringManager().CreateString(key)));
        check(object.GetObject()->SetProperty(name, value), "set property");
        check(!vm.IsException(), "set exception");
    }
    void attach(BitmapData& bitmap, const char* path)
    {
        Ptr<MovieDef> definition = *loader.CreateMovie(path,
            Loader::LoadAll | Loader::LoadImageFiles | Loader::LoadWaitCompletion);
        check(definition.GetPtr() != NULL, "PNG fixture load");
        MovieDefImpl* imageDef = static_cast<MovieDefImpl*>(definition.GetPtr());
        // The temporary resource lease must die before the external MovieDef.
        ResourceHandle handle;
        check(imageDef->GetDataDef()->GetResourceHandle(&handle,
              ResourceId(CharacterDef::CharId_ImageMovieDef_ImageResource)), "PNG resource handle");
        Resource* resource = handle.GetResource(&imageDef->GetResourceBinding());
        check(resource && resource->GetResourceType() == Resource::RT_Image, "PNG resource");
        bitmap.CreateLibraryObject(static_cast<ImageResource*>(resource), imageDef);
        check(bitmap.GetImageResource() == resource, "attached image resource");
    }
    void run(const char* name)
    {
        std::printf("BEGIN BitmapData %s\n", name); std::fflush(stdout);
        AS3::Value value = create("flash.display.BitmapData");
        BitmapData& bitmap = *static_cast<BitmapData*>(value.GetObject());
        attach(bitmap, "lifetime-a.png");
        if (std::strncmp(name, "fill", 4) == 0)
        {
            // Invoke the real AS3 native producer. No new owner API or layout is
            // referenced here, so the identical probe builds against before/after.
            AS3::Value graphicsValue = create("flash.display.Graphics");
            AS3::Instances::fl_display::Graphics& graphics =
                *static_cast<AS3::Instances::fl_display::Graphics*>(graphicsValue.GetObject());
            Ptr<DrawingContext> drawing = *vm.GetMovieImpl()->CreateDrawingContext();
            graphics.pDrawing = drawing;
            graphics.beginBitmapFill(&bitmap);
            drawing->MoveTo(0, 0);
            drawing->LineTo(20, 0);
            drawing->LineTo(20, 20);
            drawing->LineTo(0, 20);
            drawing->LineTo(0, 0);
            drawing->EndFill();
            drawing->UpdateRenderNode();
            check(drawing->GetTreeNode()->GetSize() == 1, "runtime bitmap render shape");
            Ptr<Render::ShapeMeshProvider> snapshot =
                static_cast<Render::TreeShape*>(drawing->GetTreeNode()->GetAt(0))->GetShape();
            if (std::strcmp(name, "fill-rebind") == 0)
            {
                attach(bitmap, "lifetime-b.png");
                graphics.beginBitmapFill(&bitmap);
            }
            value.SetUndefined();
            vm.GetGC().ForceCollect(NULL);
            std::puts("Source BitmapData released; drawing still holds PNG"); std::fflush(stdout);
            if (std::strcmp(name, "fill-shutdown") == 0)
            {
                AS3::Value cycle = create("Object");
                set(cycle, "self", cycle);
                set(cycle, "graphics", graphicsValue);
                graphicsValue.SetUndefined();
                return;
            }
            graphicsValue.SetUndefined();
            vm.GetGC().ForceCollect(NULL);
            drawing->Clear();
            drawing.Clear();
            Render::FillData fill;
            snapshot->GetFillData(&fill, 0, 0, 0);
            check(fill.Type == Render::Fill_Image && fill.pImage, "snapshot bitmap fill");
            check(fill.pImage->GetSize() == Render::ImageSize(1, 1), "PNG survives source/graphics/drawing retirement");
            UByte pixel[4] = {0, 0, 0, 0};
            Render::ImageData decoded(Render::Image_R8G8B8A8, 1, 1, 4, pixel);
            check(fill.pImage->Decode(&decoded), "retained snapshot PNG decode");
            check(pixel[0] == 255 && pixel[1] == 255 && pixel[2] == 255 && pixel[3] == 255,
                  "retained snapshot PNG pixel");
            snapshot.Clear();
            std::puts("Drawing and retained snapshot released"); std::fflush(stdout);
        }
        else if (std::strcmp(name, "bitmap-rebind") == 0)
        {
            AS3::Value bitmapValue = create("flash.display.Bitmap");
            AS3::Instances::fl_display::Bitmap& display =
                *static_cast<AS3::Instances::fl_display::Bitmap*>(bitmapValue.GetObject());
            display.bitmapDataSet(&bitmap);
            check(display.GetAvmBitmap() != NULL, "runtime Bitmap display object");
            Ptr<Render::ShapeMeshProvider> snapshot = static_cast<Render::TreeShape*>(
                display.GetAvmBitmap()->GetRenderNode())->GetShape();
            attach(bitmap, "lifetime-b.png");
            display.bitmapDataSet(&bitmap);
            bitmapValue.SetUndefined();
            value.SetUndefined();
            vm.GetGC().ForceCollect(NULL);
            Render::FillData fill;
            snapshot->GetFillData(&fill, 0, 0, 0);
            check(fill.Type == Render::Fill_Image && fill.pImage, "rebound Bitmap snapshot fill");
            UByte pixel[4] = {0, 0, 0, 0};
            Render::ImageData decoded(Render::Image_R8G8B8A8, 1, 1, 4, pixel);
            check(fill.pImage->Decode(&decoded) && pixel[0] == 255 && pixel[3] == 255,
                  "rebound Bitmap snapshot PNG decode");
            snapshot.Clear();
        }
        else if (std::strcmp(name, "dispose") == 0)
        {
            AS3::Value unused;
            bitmap.dispose(unused);
            bitmap.dispose(unused);
        }
        else if (std::strcmp(name, "rebind") == 0)
        {
            attach(bitmap, "lifetime-a.png");
            attach(bitmap, "lifetime-b.png");
        }
        else if (std::strcmp(name, "clone") == 0)
        {
            Render::DrawableImageContext* context =
                vm.GetMovieRoot()->GetMovieImpl()->GetDrawableImageContext();
            context->SetQueue(&queue);
            AS3::SPtr<BitmapData> first, second;
            bitmap.clone(first);
            check(first.GetPtr() && !vm.IsException(), "first clone");
            bitmap.clone(second);
            check(second.GetPtr() && !vm.IsException(), "second clone");
            value.SetUndefined();
            vm.GetGC().ForceCollect(NULL);
            first = NULL;
            vm.GetGC().ForceCollect(NULL);
            // The remaining clone still delegates to the source PNG here.
            second = NULL;
            vm.GetGC().ForceCollect(NULL);
            context->SetQueue(NULL);
        }
        else if (std::strcmp(name, "shutdown") == 0)
        {
            // Keep BitmapData in an unreachable AS3 cycle for MovieRoot shutdown.
            AS3::Value cycle = create("Object");
            set(cycle, "self", cycle);
            set(cycle, "bitmap", value);
            value.SetUndefined();
            return;
        }
        value.SetUndefined();
        vm.GetGC().ForceCollect(NULL);
        std::printf("PASS BitmapData %s\n", name); std::fflush(stdout);
    }
};

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    const char* cases[] = {"direct", "dispose", "rebind", "clone", "shutdown",
                           "fill", "fill-rebind", "fill-shutdown", "bitmap-rebind"};
    check(argc <= 2, "expected at most one case name");
    bool selected = argc == 1;
    for (const char* name : cases)
        if (argc == 2 && std::strcmp(argv[1], name) == 0) selected = true;
    check(selected, "unknown case name");
    for (unsigned lifetime = 0; lifetime != 3; ++lifetime)
    {
        {
            SysAllocWinAPI allocator;
            Scaleform::GFx::System system(&allocator);
            Ptr<AMP::ServerState> state = *new AMP::ServerState;
            state->StateFlags = AMP::Amp_Disabled;
            AmpServer::GetInstance().UpdateState(state);
            state.Clear();
            Loader loader;
            loader.SetFileOpener(Ptr<FileOpenerBase>(*new ProbeFileOpener));
            loader.SetAS3Support(Ptr<ASSupport>(*new AS3Support));
            loader.SetImageCreator(Ptr<ImageCreator>(*new ImageCreator));
            loader.SetImageFileHandlerRegistry(Ptr<GFx::ImageFileHandlerRegistry>(
                *new GFx::ImageFileHandlerRegistry(GFx::ImageFileHandlerRegistry::AddDefaultHandlers)));
            Ptr<MovieDef> definition = *loader.CreateMovie("bitmap-empty.swf",
                Loader::LoadAll | Loader::LoadWaitCompletion);
            check(definition.GetPtr() != NULL, "AS3 fixture load");
            Ptr<Movie> movie = *definition->CreateInstance(true);
            check(movie.GetPtr() != NULL, "AS3 instance");
            AS3::ASVM* vm = static_cast<AS3::MovieRoot*>(movie->pASMovieRoot.GetPtr())->GetAVM();
            check(vm != NULL, "AS3 VM");
            NoRenderQueue queue;
            Probe probe = {loader, *vm, queue};
            for (const char* name : cases)
                if (argc == 1 || std::strcmp(argv[1], name) == 0)
                    probe.run(name);
        }
        check(Memory::GetGlobalHeap() == NULL, "global heap shutdown");
        check(!Scaleform::System::HasMemoryLeaks, "SDK heap leak check");
        std::printf("PASS BitmapData SDK lifetime %u\n", lifetime); std::fflush(stdout);
    }
    std::puts("PASS real SDK BitmapData ownership and full SDK reinitialization");
}
