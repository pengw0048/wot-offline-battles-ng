// Actual SDK BitmapData constructor and BW factory call; image allocation is the
// explicit boundary. The Windows SDK probe separately executes real D3D9 pixels.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "stock_flash_drawable_images.hpp"
using SInt32=int32_t;
using UInt32=uint32_t;
using Scaleform::Render::ThreadCommandQueue;
#define SF_DEBUG_ASSERT(...)
#define SF_DEBUG_ARG(...)
#define BW_GUARD
#define SF_NEW new
int created=0, errors=0;
struct ImageSize { unsigned width,height; ImageSize(unsigned x,unsigned y):width(x),height(y){} };
using TextureManager=Scaleform::Render::TextureManager;
struct DrawableImageContext
{
    ThreadCommandQueue* queue=NULL;
    ThreadCommandQueue* GetQueue(){return queue;}
};
struct DrawableImage
{
    unsigned refs=1;
    void AddRef(){++refs;}
    void Release(){if(--refs==0)delete this;}
    DrawableImage(int format,ImageSize size,bool,UInt32,DrawableImageContext*)
    {assert(format==27 && size.width==3 && size.height==4);++created;}
    DrawableImage(bool,void*,DrawableImageContext*){assert(false);}
};
struct Value
{
    int value;
    explicit Value(int v):value(v){}
    bool Convert2Int32(SInt32& result)const{result=value;return true;}
    bool Convert2Boolean()const{return value!=0;}
    struct Check {void DoNotCheck(){}};
    Check Convert2UInt32(UInt32& result)const{result=static_cast<UInt32>(value);return {};}
};
struct MovieImpl
{
    DrawableImageContext context;
    DrawableImageContext* GetDrawableImageContext(){return &context;}
};
struct MovieRoot
{
    MovieImpl impl;
    MovieImpl* GetMovieImpl(){return &impl;}
};
struct VM
{
    enum {eWrongArgumentCountError=1};
    struct Error {Error(int,VM&){} };
    void ThrowArgumentError(const Error&){++errors;}
};
struct ASVM:VM
{
    MovieRoot root;
    MovieRoot* GetMovieRoot()const{return const_cast<MovieRoot*>(&root);}
};
namespace Render {using Interfaces=Scaleform::Render::Interfaces;}
struct ImageResource {void* GetImage(){return NULL;}};
struct BitmapData
{
    ASVM& vm;
    UInt32 Width=0,Height=0;
    bool Transparent=true;
    ImageResource* pImageResource=NULL;
    Scaleform::Ptr<DrawableImage> pImage;
    explicit BitmapData(ASVM& value):vm(value){}
    ASVM& GetVM(){return vm;}
    void AS3Constructor(unsigned,const Value*);
};
@CONSTRUCTOR@
// This host's image type has the same intrusive API as the SDK's Image.
namespace GFx
{
struct Movie{};
struct Definition
{
    ASVM vm;
    BitmapData bitmap{vm};
    bool fail=false;
    Movie movie;
    Movie* CreateInstance(bool firstFrame,unsigned=0,void* =NULL,ThreadCommandQueue* queue=NULL)
    {
        if(fail)return NULL;
        vm.root.impl.context.queue=queue;
        if(firstFrame)
        {
            Value values[]={Value(3),Value(4)};
            bitmap.AS3Constructor(2,values);
        }
        return &movie;
    }
};
}
struct Manager
{
    BW::ScaleformBW::StockFlashDrawableImageQueue queue;
    Manager():queue([](Scaleform::Render::ThreadCommand*){assert(false);}){}
    static Manager& instance(){static Manager value;return value;}
    ThreadCommandQueue* drawableImageQueue(){return &queue;}
};
int PyExc_Exception=1;
void PyErr_SetString(int,const char*){++errors;}
struct Factory
{
    GFx::Definition* pMovieDef_;
    GFx::Movie* createInstance(bool initFirstFrame)
    {
@FACTORY_PREFIX@
        return movieView;
    }
};
int main()
{
    TextureManager texture;
    Scaleform::Render::Interfaces interfaces;
    interfaces.pTextureManager=&texture;
    Manager::instance().queue.configure(interfaces);
    GFx::Definition definition;
    Factory factory{&definition};
    assert(factory.createInstance(true)!=NULL);
    assert(created==1 && definition.bitmap.pImage.GetPtr()!=NULL);
    definition.fail=true;
    assert(factory.createInstance(true)==NULL);
    assert(errors==1);
    std::puts("PASS actual BW CreateInstance call and SDK BitmapData allocation guard");
}
