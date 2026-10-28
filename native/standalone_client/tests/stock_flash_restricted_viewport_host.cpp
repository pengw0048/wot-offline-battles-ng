#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
int mode=1,loads=0,unloads=0,initialises=0,releases=0,queries=0,callbacks=0;
int regionLeft=1280,regionTop=0,regionWidth=1280,regionHeight=720;
void* LoadLibraryW(const wchar_t*) {++loads;return mode==0?nullptr:(void*)1;}
void FreeLibrary(void*) {++releases;}
int init() {++initialises;return mode==3?-1:0;}
int unload() {++unloads;return 0;}
int primary(unsigned* display) {*display=42;return mode==4?-1:0;}
int viewports(unsigned display,unsigned,unsigned,void* output,unsigned char* bezel)
{
    assert(display==42);++queries;*bezel=0;
    if(mode==5)return -1;
    struct Rect {int left,top,right,bottom;};
    auto* rect=static_cast<Rect*>(output);
    int count=mode==6?1:mode==7?2:3;
    for(int i=0;i<count;++i)
        rect[i]={regionLeft+(i-1)*regionWidth,regionTop,
            regionLeft+(i-1)*regionWidth+regionWidth-1,regionTop+regionHeight-1};
    if(mode==8)rect[2].top+=1;
    if(mode==9)rect[1].right=rect[1].left+300;
    return 0;
}
void* query(unsigned id)
{
    if(id==0x0150e828)return (void*)&init;
    if(id==0xd22bdd7e)return (void*)&unload;
    if(id==0x1e9d8a31)return (void*)&primary;
    assert(id==0xdc6dc8d3);return (void*)&viewports;
}
void* GetProcAddress(void*,const char* name)
{assert(!std::strcmp(name,"nvapi_QueryInterface"));return mode==2?nullptr:(void*)&query;}
namespace Moo {
struct DeviceCallback {
    DeviceCallback(){++callbacks;}virtual ~DeviceCallback(){--callbacks;}
    virtual void createUnmanagedObjects(){} virtual void deleteUnmanagedObjects(){}
    virtual bool recreateForD3DExDevice()const{return false;}
};
}
#include "stock_flash_restricted_viewport.hpp"
using SInt=int;
struct Vector2 {float x,y;void set(float a,float b){x=a;y=b;}};
struct Vector3 {float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c){}};
struct Vector4 {float x,y,z,w;};
struct Matrix {Vector3 applyPoint(const Vector3& v)const{return v;}};
namespace GFx {
struct Viewport {
    int BufferWidth,BufferHeight,Left,Top,Width,Height;
    Viewport()=default;
    Viewport(int bw,int bh,int l,int t,int w,int h):BufferWidth(bw),BufferHeight(bh),
        Left(l),Top(t),Width(w),Height(h){}
};
}
struct GRectF {
    float values[4];void SetRect(float a,float b,float c,float d){values[0]=a;values[1]=b;values[2]=c;values[3]=d;}
    float X1()const{return values[0];}float Y1()const{return values[1];}
    float X2()const{return values[2];}float Y2()const{return values[3];}
};
struct SimpleGUI {
    static SimpleGUI& instance(){static SimpleGUI o;return o;}
    Vector4 clipRegion()const{return clip;}Vector4 clip{-1,1,1,-1};
};
namespace Moo {
struct RC {
    struct Desc {unsigned Width=3840,Height=720;} desc;
    StockFlashRestrictedViewport owner;bool window=false;Matrix matrix;
    const Desc& backBufferDesc()const{return desc;}bool windowed()const{return window;}
    const Matrix& world()const{return matrix;}
    StockFlashRestrictedViewport& stockFlashRestrictedViewport(){return owner;}
};
RC& rc(){static RC r;return r;}
}
@VIEWPORT_METHODS@
int main(int argc,char**)
{
    if(argc>1)
    {
        int restricted,active;Vector2 corners[4];
        while(std::cin>>restricted>>active>>regionLeft>>regionTop>>regionWidth>>regionHeight)
        {
            auto& rc=Moo::rc();rc.window=!active;rc.owner.createUnmanagedObjects();
            auto& clip=SimpleGUI::instance().clip;
            std::cin>>clip.x>>clip.y>>clip.z>>clip.w;
            for(auto& c:corners)std::cin>>c.x>>c.y;
            GFx::Viewport viewport;GRectF rect;
            runtimeGUIViewport(corners,viewport,rect,restricted!=0);
            std::cout<<viewport.BufferWidth<<' '<<viewport.BufferHeight<<' '<<viewport.Left<<' '
                <<viewport.Top<<' '<<viewport.Width<<' '<<viewport.Height;
            std::cout.precision(9);for(float v:rect.values)std::cout<<' '<<v;std::cout<<'\n';
        }
        return 0;
    }
    for(mode=0;mode<=9;++mode)
    {
        loads=unloads=initialises=releases=queries=0;
        {
            Moo::StockFlashRestrictedViewport owner;
            auto value=owner.region(3840,720,false);
            assert(value.restricted==(mode==1));
            if(mode==1)
            {
                assert(value.left==1280&&value.top==0&&value.width==1280&&value.height==720);
                const int before=queries;owner.region(3840,720,false);assert(queries==before);
                owner.deleteUnmanagedObjects();owner.createUnmanagedObjects();
                assert(owner.recreateForD3DExDevice());owner.region(3840,720,false);assert(queries==before+1);
            }
            else assert(value.left==0&&value.top==0&&value.width==3840&&value.height==720);
            value=owner.region(3840,720,true);assert(!value.restricted&&value.width==3840);
        }
        assert(callbacks==0);
        assert(releases==(mode==0?0:1));
        assert(unloads==(mode==0||mode==2||mode==3?0:1));
    }
    mode=1;loads=0;
    {Moo::StockFlashRestrictedViewport owner;owner.region(3840,720,true);assert(loads==0);}
}
