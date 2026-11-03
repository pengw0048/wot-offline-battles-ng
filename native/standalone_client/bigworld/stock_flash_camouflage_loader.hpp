#ifndef STOCK_FLASH_CAMOUFLAGE_LOADER_HPP
#define STOCK_FLASH_CAMOUFLAGE_LOADER_HPP

#include "stock_flash_camouflage.hpp"
#include "resmgr/multi_file_system.hpp"
#include "cstdmf/bw_util.hpp"
#include <algorithm>
#include <mutex>
#include <vector>

namespace BW { namespace ScaleformBW {

// The supported installation already owns these exact CPU image libraries.
// Reuse its decode/origin/nearest-scale path instead of changing DDS rounding
// or inventing a replacement shader for the original thumbnail compositor.
class StockCamouflageImageDecoder
{
    typedef unsigned char ILboolean;
    typedef unsigned int ILuint;
    typedef void (__stdcall* Init)();
    typedef ILuint (__stdcall* Generate)();
    typedef void (__stdcall* ImageOperation)(ILuint);
    typedef ILboolean (__stdcall* Option)(unsigned);
    typedef ILboolean (__stdcall* Load)(unsigned, const void*, unsigned);
    typedef ILboolean (__stdcall* Convert)(unsigned, unsigned);
    typedef ILboolean (__stdcall* Scale)(unsigned, unsigned, unsigned);
    typedef ILboolean (__stdcall* Overlay)(unsigned, int, int, int);
    typedef unsigned (__stdcall* Error)();
    struct ImageInfo
    {
        unsigned id;
        unsigned char* data;
        unsigned width, height, depth;
        unsigned char bytesPerPixel;
        unsigned size, format, type, origin;
        unsigned char* palette;
        unsigned paletteType, paletteSize, cubeFlags, next, mipmaps, layers;
    };
#if defined(_M_IX86)
    static_assert(sizeof(ImageInfo) == 68, "The supported ILU image descriptor is x86");
#endif
    typedef void (__stdcall* Describe)(ImageInfo*);
    HMODULE image_ = NULL, utility_ = NULL;
    Init initialize_ = NULL, shutdown_ = NULL;
    Generate generate_ = NULL;
    ImageOperation bind_ = NULL, remove_ = NULL;
    Option disable_ = NULL;
    Load load_ = NULL;
    Convert convert_ = NULL;
    Scale scale_ = NULL;
    Overlay overlay_ = NULL;
    Describe describe_ = NULL;
    Error error_ = NULL;
    bool attempted_ = false, ready_ = false;
    unsigned reported_ = 0;

    bool failed(unsigned bit, const char* stage, const std::string& path)
    {
        if (!(reported_ & bit))
        {
            reported_ |= bit;
            ERROR_MSG("Scaleform: camouflage decode failed stage=%s resource=%s error=0x%x\n",
                stage, path.c_str(), error_ ? error_() : 0);
        }
        return false;
    }

    template<class T> bool symbol(HMODULE module, const char* name, T& value)
    {
        value = reinterpret_cast<T>(GetProcAddress(module, name));
        if (!value) ERROR_MSG("Scaleform: original image library lacks %s\n", name);
        return value != NULL;
    }
    HMODULE library(const wchar_t* name)
    {
        BW::wstring path(L"../");
        path += name;
        if (BWResource::resolveToAbsolutePath(path) != IFileSystem::FT_FILE)
        {
            ERROR_MSG("Scaleform: original customization image library is unavailable\n");
            return NULL;
        }
        // The resource manager uses '/', but LoadLibraryEx requires native
        // separators, including for its altered dependency search directory.
        std::replace(path.begin(), path.end(), L'/', L'\\');
        HMODULE result = LoadLibraryExW(path.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!result) ERROR_MSG("Scaleform: cannot load original image library name=%ls error=%lu\n",
            name, static_cast<unsigned long>(GetLastError()));
        return result;
    }
    bool initialize()
    {
        if (attempted_) return ready_;
        attempted_ = true;
        image_ = library(L"ResIL.dll");
        utility_ = image_ ? library(L"ILU.dll") : NULL;
        if (!image_ || !utility_) return false;
        Init utilityInit = NULL;
        if (!symbol(image_, "ilInit", initialize_) || !symbol(image_, "ilShutDown", shutdown_) ||
            !symbol(image_, "_ilGenImage@0", generate_) || !symbol(image_, "ilBindImage", bind_) ||
            !symbol(image_, "_ilDeleteImage@4", remove_) || !symbol(image_, "_ilDisable@4", disable_) ||
            !symbol(image_, "_ilOverlayImage@16", overlay_) || !symbol(image_, "ilLoadL", load_) ||
            !symbol(image_, "ilConvertImage", convert_) || !symbol(utility_, "iluInit", utilityInit) ||
            !symbol(utility_, "iluScale", scale_) || !symbol(utility_, "_iluGetImageInfo@4", describe_) ||
            !symbol(image_, "ilGetError", error_))
            return false;
        initialize_();
        utilityInit();
        ready_ = true;
        return true;
    }
public:
    std::mutex mutex;
    ~StockCamouflageImageDecoder()
    {
        if (ready_) shutdown_();
        if (utility_) FreeLibrary(utility_);
        if (image_) FreeLibrary(image_);
    }
    bool decode(const std::string& path, unsigned width, unsigned height, std::vector<unsigned>& output)
    {
        if (!initialize()) return failed(1, "initialize", path);
        BinaryPtr data = BWResource::instance().rootSection()->readBinary(path.c_str());
        if (!data || data->len() <= 0) return failed(2, "readBinary", path);
        const unsigned id = generate_();
        if (!id) return failed(4, "generate", path);
        struct ImageLease
        {
            ImageOperation remove;
            unsigned id;
            void reset(unsigned next) { if (id) remove(id); id = next; }
            unsigned release() { const unsigned value = id; id = 0; return value; }
            ~ImageLease() { reset(0); }
        } image{remove_, id};
        bind_(id);
        // Original aadda0 calls 0x9a0bb0 (ilLoadL), 0x9a1180 (BGRA/byte),
        // then 0x9a0ff0 calls the bundled ILU scaler (default filter 0x2601).
        if (!load_(0, data->data(), static_cast<unsigned>(data->len()))) return failed(32, "load", path);
        if (!convert_(0x80e1, 0x1401)) return failed(64, "convert", path);
        ImageInfo source = {};
        describe_(&source);
        if (!source.width || !source.height || !source.depth || !source.data ||
            source.bytesPerPixel != 4 || source.width > 0xffffffffu / 4 / source.height ||
            source.size < source.width * source.height * 4)
            return failed(8, "describe-source", path);
        if (source.mipmaps || source.depth > 1 || source.layers || source.next || source.cubeFlags)
        {
            // Original 0x9a0ff0 copies the base surface into a flat image before
            // scaling, separating its attached mip/surface allocations from
            // the resized image. Preserve the native buffer's row order.
            const unsigned flat = generate_();
            if (!flat) return failed(16, "flatten-generate", path);
            ImageLease flattened{remove_, flat};
            if (!disable_(0x636)) return failed(512, "flatten-disable-blend", path);
            bind_(flat);
            if (!scale_(source.width, source.height, 1)) return failed(1024, "flatten-size", path);
            if (!convert_(0x80e1, 0x1401)) return failed(2048, "flatten-convert", path);
            if (!overlay_(image.id, 0, 0, 0)) return failed(4096, "flatten-overlay", path);
            image.reset(flattened.release());
        }
        if (!scale_(width, height, 1)) return failed(128, "scale", path);
        ImageInfo info = {};
        describe_(&info);
        const unsigned bytes = width * height * 4;
        if (info.width != width || info.height != height || info.depth != 1 ||
            info.bytesPerPixel != 4 || info.size < bytes || !info.data)
        {
            if (!(reported_ & 256)) ERROR_MSG("Scaleform: camouflage image descriptor width=%u height=%u depth=%u bpp=%u size=%u format=0x%x type=0x%x origin=0x%x data=%p\n",
                info.width, info.height, info.depth, unsigned(info.bytesPerPixel), info.size,
                info.format, info.type, info.origin, info.data);
            return failed(256, "describe", path);
        }
        output.resize(static_cast<size_t>(width) * height);
        memcpy(output.data(), info.data, bytes);
        return true;
    }
};

inline Render::Image* loadStockFlashCamouflage(const char* url)
{
    wot::flash_camouflage::Parameters params;
    if (!wot::flash_camouflage::parse(url, params))
    {
        ERROR_MSG("Scaleform: invalid customization camouflage image URI\n");
        return NULL;
    }
    static StockCamouflageImageDecoder decoder;
    std::vector<unsigned> mask, background;
    {
        // ResIL owns a process-wide current image. Each command has its own
        // image ID, and no pointer into that library survives this lock.
        std::lock_guard<std::mutex> lock(decoder.mutex);
        if (!decoder.decode(params.texture, params.width, params.height, mask) ||
            !decoder.decode(params.background, params.width, params.height, background))
        {
            ERROR_MSG("Scaleform: cannot decode customization camouflage image resources\n");
            return NULL;
        }
    }
    Render::RawImage* created = Render::RawImage::Create(Render::Image_R8G8B8A8,
        1, Render::ImageSize(params.width, params.height), 0, ::Scaleform::Memory::pGlobalHeap);
    if (!created) return NULL;
    Ptr<Render::RawImage> image = *created;
    Render::ImageData data;
    if (!image || !image->GetImageData(&data)) return NULL;
    Render::ImagePlane plane;
    data.GetPlane(0, &plane);
    const unsigned rowBytes = params.width * 4;
    if (!plane.pData || plane.Width != params.width || plane.Height != params.height ||
        plane.Pitch < rowBytes || plane.DataSize < rowBytes ||
        params.height - 1 > (plane.DataSize - rowBytes) / plane.Pitch) return NULL;
    for (unsigned y = 0; y < params.height; ++y)
        for (unsigned x = 0; x < params.width; ++x)
        {
            const size_t index = static_cast<size_t>(y) * params.width + x;
            const std::uint32_t pixel = wot::flash_camouflage::pixel(mask[index], background[index], params);
            // pixel() includes the original format-1 SetPixel channel map.
            // Image_R8G8B8A8 stores the resulting R,G,B,A bytes explicitly.
            UByte* output = plane.pData + y * plane.Pitch + x * 4;
            output[0] = UByte(pixel);
            output[1] = UByte(pixel >> 8);
            output[2] = UByte(pixel >> 16);
            output[3] = 255;
        }
    image->AddRef();
    return image.GetPtr();
}

}} // namespace BW::ScaleformBW
#endif
