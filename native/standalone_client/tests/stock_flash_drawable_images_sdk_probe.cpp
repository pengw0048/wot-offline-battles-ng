// Uses only pre-existing SDK APIs, so the same probe can verify the old no-queue path.
#define main stockBitmapOwnershipProbeMain
#include "stock_scaleform_bitmap_sdk_probe.cpp"
#undef main
#include <Render/D3D9/D3D9_HAL.h>
#include <Render/Renderer2D.h>
#include <GFx/AS3/Obj/Geom/AS3_Obj_Geom_Rectangle.h>
#include <GFx/AS3/Obj/Geom/AS3_Obj_Geom_Point.h>
#include "stock_flash_drawable_images.hpp"

static IDirect3DDevice9* probeDevice = NULL;
static unsigned executedCommands = 0;
static void executeImageCommand(Render::ThreadCommand* command)
{
    check(SUCCEEDED(probeDevice->BeginScene()), "image command BeginScene");
    ++executedCommands;
    command->Execute();
    check(SUCCEEDED(probeDevice->EndScene()), "image command EndScene");
}

static AS3::Value object(AS3::ASVM& vm, const char* name, unsigned argc = 0, const AS3::Value* args = NULL)
{
    AS3::Value value;
    check(vm.CreateObject(name, vm.GetFrameAppDomain(), value, argc, args), name);
    check(!vm.IsException(), "object creation exception");
    return value;
}

static AS3::Value call(AS3::ASVM& vm, AS3::Class* klass, const char* name,
                       unsigned argc, const AS3::Value* args)
{
    check(klass != NULL, "TextFieldEx class");
    AS3::Value method, result;
    const AS3::Multiname member(vm.GetPublicNamespace(), AS3::Value(vm.GetStringManager().CreateString(name)));
    check(klass->GetProperty(member, method), name);
    check(!vm.IsException() && method.IsCallable(), "registered TextFieldEx method");
    vm.Execute(method, AS3::Value(klass), result, argc, args);
    check(!vm.IsException(), "TextFieldEx method exception");
    return result;
}

static void pixels(Loader& loader, AS3::ASVM& vm, bool before)
{
    AS3::Value size[] = {AS3::Value(SInt32(2)), AS3::Value(SInt32(2)),
                         AS3::Value(true), AS3::Value(UInt32(0x10203040))};
    AS3::Value targetValue = object(vm, "flash.display.BitmapData", 4, size);
    BitmapData& target = *static_cast<BitmapData*>(targetValue.GetObject());
    if (before) check(target.GetImageResource() != NULL, "old no-queue BitmapData has no image");
    check(target.GetImageResource() != NULL, "new BitmapData owns real DrawableImage");
    check(target.getPixel32(0, 0) == 0x10203040, "new BitmapData clear pixels");
    AS3::Value sourceValue = object(vm, "flash.display.BitmapData", 4, size);
    BitmapData& source = *static_cast<BitmapData*>(sourceValue.GetObject());
    NoRenderQueue unused;
    Probe probe = {loader, vm, unused};
    probe.attach(source, "lifetime-atlas.png");
    AS3::Value rectArgs[] = {AS3::Value(SInt32(0)), AS3::Value(SInt32(0)),
                            AS3::Value(SInt32(1)), AS3::Value(SInt32(1))};
    AS3::Value pointArgs[] = {AS3::Value(SInt32(1)), AS3::Value(SInt32(1))};
    AS3::Value rectangle = object(vm, "flash.geom.Rectangle", 4, rectArgs);
    AS3::Value point = object(vm, "flash.geom.Point", 2, pointArgs);
    // AtlasManager.getNewBitmapData's actual native producer and consumers.
    target.copyPixels(&source, static_cast<AS3::Instances::fl_geom::Rectangle*>(rectangle.GetObject()),
                      static_cast<AS3::Instances::fl_geom::Point*>(point.GetObject()));
    check(!vm.IsException(), "atlas copyPixels exception");
    check(target.getPixel32(1, 1) == 0xffffffff, "atlas copied white PNG pixel");
    check(target.getPixel32(0, 0) == 0x10203040, "atlas copy preserves other pixels");
    target.dispose();
    check(!vm.IsException(), "BitmapData.dispose");
    std::puts("PASS real SDK new BitmapData / PNG atlas copyPixels / pixels / dispose");
}

static void autosize(AS3::ASVM& vm)
{
    vm.ExtensionsEnabled = true;
    AS3::Value text = object(vm, "flash.text.TextField");
    AS3::Class* klass = vm.GetClass("scaleform.gfx.TextFieldEx", vm.GetFrameAppDomain());
    static const char* alignments[] = {"top", "bottom", "center", "none"};
    for (unsigned index = 0; index < sizeof(alignments)/sizeof(alignments[0]); ++index)
    {
        const char* align = alignments[index];
        AS3::Value args[] = {text, AS3::Value(vm.GetStringManager().CreateString(align))};
        call(vm, klass, "setVerticalAutoSize", 2, args);
        AS3::Value result = call(vm, klass, "getVerticalAutoSize", 1, args);
        ASString string = vm.GetStringManager().CreateEmptyString();
        check(result.Convert2String(string), "autosize string result");
        check(std::strcmp(string.ToCStr(), align) == 0, "actual TextFieldEx autosize round trip");
    }
    std::puts("PASS real SDK TextFieldEx autosize registration and DocView owner");
}

int main(int argc, char** argv)
{
    const bool before = argc == 2 && std::strcmp(argv[1], "before") == 0;
    const bool textOnly = argc == 2 && std::strcmp(argv[1], "text") == 0;
    check(argc == 1 || before || textOnly, "unknown probe mode");
    HWND window = CreateWindowExW(0, L"STATIC", L"DrawableImage probe", WS_OVERLAPPED,
                                  0, 0, 64, 64, NULL, NULL, GetModuleHandleW(NULL), NULL);
    check(window != NULL, "hidden D3D window");
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    check(d3d != NULL, "Direct3DCreate9");
    D3DPRESENT_PARAMETERS presentation = {};
    presentation.Windowed = TRUE;
    presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentation.BackBufferWidth = presentation.BackBufferHeight = 64;
    presentation.BackBufferFormat = D3DFMT_UNKNOWN;
    presentation.hDeviceWindow = window;
    check(SUCCEEDED(d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &presentation, &probeDevice)), "D3D9 device");
    for (unsigned lifecycle = 0; lifecycle != 3; ++lifecycle)
    {
        {
            SysAllocWinAPI allocator;
            Scaleform::GFx::System system(&allocator);
            Ptr<AMP::ServerState> state = *new AMP::ServerState;
            state->StateFlags = AMP::Amp_Disabled;
            AmpServer::GetInstance().UpdateState(state);
            state.Clear();
            BW::ScaleformBW::StockFlashDrawableImageQueue queue(&executeImageCommand);
            Ptr<Render::D3D9::HAL> hal = *new Render::D3D9::HAL(&queue);
            check(hal->InitHAL(Render::D3D9::HALInitParams(probeDevice, presentation,
                                  Render::D3D9::HALConfig_NoSceneCalls)), "actual SDK HAL init");
            Ptr<Render::Renderer2D> renderer = *new Render::Renderer2D(hal);
            queue.configure(Render::Interfaces(hal->GetTextureManager(), hal, renderer, hal->GetRenderThreadId()));
            {
                Loader loader;
                loader.SetFileOpener(Ptr<FileOpenerBase>(*new ProbeFileOpener));
                loader.SetAS3Support(Ptr<ASSupport>(*new AS3Support));
                loader.SetImageCreator(Ptr<ImageCreator>(*new ImageCreator(hal->GetTextureManager())));
                loader.SetImageFileHandlerRegistry(Ptr<GFx::ImageFileHandlerRegistry>(
                    *new GFx::ImageFileHandlerRegistry(GFx::ImageFileHandlerRegistry::AddDefaultHandlers)));
                Ptr<MovieDef> definition = *loader.CreateMovie("bitmap-empty.swf", Loader::LoadAll | Loader::LoadWaitCompletion);
                check(definition.GetPtr() != NULL, "AS3 fixture load");
                for (unsigned round = 0; round != 2; ++round)
                {
                    Ptr<Movie> movie = *definition->CreateInstance(true, 0, NULL, before ? NULL : &queue);
                    check(movie.GetPtr() != NULL, "AS3 instance");
                    AS3::ASVM* vm = static_cast<AS3::MovieRoot*>(movie->pASMovieRoot.GetPtr())->GetAVM();
                    check(vm != NULL, "AS3 VM");
                    if (textOnly) autosize(*vm); else pixels(loader, *vm, before);
                    vm->GetGC().ForceCollect(NULL);
                    movie.Clear();
                    std::printf("PASS Movie close and next round %u\n", round);
                }
            }
            renderer.Clear();
            hal.Clear();
            queue.configure(Render::Interfaces());
        }
        check(Memory::GetGlobalHeap() == NULL, "global heap shutdown");
        check(!Scaleform::System::HasMemoryLeaks, "SDK heap leak check");
        std::printf("PASS DrawableImage SDK lifecycle %u\n", lifecycle);
    }
    if (!textOnly) check(executedCommands != 0, "actual SDK rendering commands executed");
    probeDevice->Release(); d3d->Release(); DestroyWindow(window);
    std::puts("PASS real SDK DrawableImage render queue, atlas pixels and reinitialization");
    return 0;
}
