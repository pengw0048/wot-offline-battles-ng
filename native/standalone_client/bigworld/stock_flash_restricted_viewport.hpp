#pragma once
#include <windows.h>
#include <memory>
#include "moo/device_callback.hpp"

BW_BEGIN_NAMESPACE
namespace Moo
{
// #1513 99d4e0: NVIDIA Mosaic's landscape centre display is the optional
// restricted UI area. This is independent of DRR and of the GUI clip stack.
class StockFlashRestrictedViewport : public DeviceCallback
{
public:
    struct Region { int left, top, width, height; bool restricted; };
    StockFlashRestrictedViewport() : library_(NULL), query_(NULL), initialised_(false),
        attempted_(false), dirty_(true), windowed_(true), region_{0,0,0,0,false} {}
    ~StockFlashRestrictedViewport()
    {
        if (initialised_)
        {
            auto unload = reinterpret_cast<Initialise>(query_(0xd22bdd7e));
            if (unload) unload();
        }
        if (library_) FreeLibrary(library_);
    }
    void createUnmanagedObjects() override { dirty_ = true; }
    void deleteUnmanagedObjects() override { dirty_ = true; region_.restricted = false; }
    bool recreateForD3DExDevice() const override { return true; }
    Region region(unsigned width, unsigned height, bool windowed)
    {
        if (!dirty_ && width == width_ && height == height_ && windowed == windowed_)
            return region_;
        dirty_ = false; width_ = width; height_ = height; windowed_ = windowed;
        region_ = {0, 0, int(width), int(height), false};
        if (windowed || !width || !height || !initialise()) return region_;
        auto primary = reinterpret_cast<Primary>(query_(0x1e9d8a31));
        auto viewports = reinterpret_cast<Viewports>(query_(0xdc6dc8d3));
        unsigned display = 0;
        if (!primary || !viewports || primary(&display) != 0) return region_;
        Rect rectangles[64] = {};
        unsigned char bezel = 0;
        if (viewports(display, width, height, rectangles, &bezel) != 0) return region_;
        unsigned count = 0;
        while (count < 64 && rectangles[count].top != rectangles[count].bottom) ++count;
        if (count <= 1 || !(count & 1)) return region_;
        for (unsigned i = 1; i < count; ++i)
            if (rectangles[i].top != rectangles[0].top ||
                rectangles[i].bottom != rectangles[0].bottom) return region_;
        const Rect& centre = rectangles[count / 2];
        const int centreWidth = centre.right - centre.left + 1;
        const int centreHeight = centre.bottom - centre.top + 1;
        if (centreWidth > centreHeight)
            region_ = {centre.left, centre.top, centreWidth, centreHeight, true};
        return region_;
    }
private:
    struct Rect { int left, top, right, bottom; };
    using Query = void* (__cdecl *)(unsigned);
    using Initialise = int (__cdecl *)();
    using Primary = int (__cdecl *)(unsigned*);
    using Viewports = int (__cdecl *)(unsigned, unsigned, unsigned, Rect*, unsigned char*);
    bool initialise()
    {
        if (attempted_) return initialised_;
        attempted_ = true;
        library_ = LoadLibraryW(L"nvapi.dll");
        if (!library_) return false;
        query_ = reinterpret_cast<Query>(GetProcAddress(library_, "nvapi_QueryInterface"));
        if (!query_) return false;
        auto init = reinterpret_cast<Initialise>(query_(0x0150e828));
        initialised_ = init && init() == 0;
        return initialised_;
    }
    StockFlashRestrictedViewport(const StockFlashRestrictedViewport&) = delete;
    StockFlashRestrictedViewport& operator=(const StockFlashRestrictedViewport&) = delete;
    HMODULE library_;
    Query query_;
    bool initialised_, attempted_, dirty_, windowed_;
    unsigned width_ = 0, height_ = 0;
    Region region_;
};
}
BW_END_NAMESPACE
