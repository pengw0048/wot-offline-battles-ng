// Compile the production ComplexFill, GFx lease and SDK Graphics/DrawingContext
// bitmap handoff bodies. Refcount/heap storage and geometry are explicit boundaries.
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#define SF_UNUSED5(a,b,c,d,e) (void)a;(void)b;(void)c;(void)d;(void)e
#define SF_HEAP_NEW(heap) new
#define SF_NEW new
#define Stat_Default_Mem 0
static std::vector<std::string> events;
struct RefCountImpl {int refs=1;virtual ~RefCountImpl(){}void AddRef(){refs++;}void Release(){if(!--refs)delete this;}};
template<class T,int>struct RefCountBase:RefCountImpl{};
template<class T>class Ptr {
    T*p_=nullptr;
public:
    Ptr()=default;Ptr(T*p):p_(p){if(p_)p_->AddRef();}Ptr(T&p):p_(&p){}
    Ptr(const Ptr&o):Ptr(o.p_){}~Ptr(){if(p_)p_->Release();}
    Ptr&operator=(const Ptr&o){return operator=(o.p_);}Ptr&operator=(T*p){if(p)p->AddRef();T*old=p_;p_=p;if(old)old->Release();return *this;}
    Ptr&operator=(T&p){T*old=p_;p_=&p;if(old)old->Release();return *this;}
    T*operator->()const{return p_;}T*GetPtr()const{return p_;}operator T*()const{return p_;}void Clear(){*this=(T*)nullptr;}
};
struct Heap {bool live=true;std::string name;explicit Heap(std::string n):name(std::move(n)){} };
struct Matrix2F {int state=0;Matrix2F()=default;Matrix2F(const Matrix2F&)=default;Matrix2F&operator=(const Matrix2F&)=default;void AppendScaling(int v){state=v;}Matrix2F GetInverse()const{return *this;}};
using Matrix=Matrix2F;
namespace Render {
using Matrix2F=::Matrix2F;
struct ImageBase:RefCountImpl {enum{Type_ImageBase,Type_Image};virtual int GetImageType()const{return Type_Image;}};
struct Image:ImageBase {
    std::shared_ptr<Heap>heap;
    explicit Image(std::shared_ptr<Heap>h):heap(std::move(h)){}
    ~Image(){assert(heap->live&&"heap retired before image");events.push_back("image:"+heap->name);}
    void read()const{assert(heap->live&&"heap retired before image");}
};
struct ImageSource:ImageBase{};
struct GradientData:RefCountImpl{};
struct ImageFillMode {unsigned Fill=0;};
enum{Wrap_Repeat=1,Wrap_Clamp=2,Sample_Point=4,Sample_Linear=8};
@COMPLEX_FILL@
struct FillStyleType {unsigned Color=0;Ptr<ComplexFill>pFill;};
}
struct MovieDefImpl:RefCountImpl {
    std::shared_ptr<Heap>heap;Ptr<Render::Image>image;
    explicit MovieDefImpl(const char*name):heap(std::make_shared<Heap>(name)),image(*new Render::Image(heap)){}
    ~MovieDefImpl(){image.Clear();events.push_back("owner:"+heap->name);heap->live=false;}
};
struct ImageResource:RefCountImpl {
    Ptr<Render::Image>image;explicit ImageResource(Render::Image*p):image(p){}
    Render::ImageBase*GetImage(){return image;}
};
struct MemoryHeap{};
struct ImageCreateInfo {enum{Create_SourceImage};ImageCreateInfo(int,MemoryHeap*){}};
struct ImageCreator:RefCountImpl {Render::Image*CreateImage(ImageCreateInfo,Render::ImageSource*){assert(false);return nullptr;}};
namespace Memory {static MemoryHeap*GetHeapByAddress(void*){return nullptr;}}
static void LogDebugMessage(int,const char*){assert(false);}
static const int Log_Warning=0;
enum FillType {Fill_TiledImage,Fill_TiledSmoothImage,Fill_ClippedImage,Fill_ClippedSmoothImage};
struct Shapes {
    std::vector<Render::FillStyleType>fills;
    void GetFillStyle(unsigned index,Render::FillStyleType*out){*out=fills[index-1];}
    void UpdateFillStyle(unsigned index,const Render::FillStyleType&f){fills[index-1]=f;}
};
@RESOURCE_OWNER@
struct DrawingContext {
    using FillStyle=Render::FillStyleType;using ComplexFill=Render::ComplexFill;
    Ptr<ImageCreator>ImgCreator;::Shapes storage;::Shapes*Shapes=&storage;
    unsigned SetNewFill(){storage.fills.push_back(FillStyle());return storage.fills.size();}
    void*GetHeap(){return nullptr;}void BeginFill(){}void AcquirePath(bool){}
    void Clear(){storage.fills.clear();}
    @DRAW_DECLARATION@
};
namespace Instances {namespace fl_geom {struct Matrix {Matrix2F GetMatrixF(){return {};}};}
namespace fl_display {
struct BitmapData {
    Ptr<MovieDefImpl>pDefImpl;Ptr<ImageResource>resource;
    explicit BitmapData(const char*name):pDefImpl(*new MovieDefImpl(name)),resource(*new ImageResource(pDefImpl->image)){}
    ImageResource*GetImageResource(){return resource;}
    @GET_OWNER@
    ~BitmapData(){resource.Clear();pDefImpl.Clear();}
};
struct Graphics {using Value=int;DrawingContext*pDrawing;
    void beginBitmapFill(const Value&result,BitmapData*bitmap,fl_geom::Matrix*matrix,bool repeat,bool smooth);
};
@GRAPHICS_HANDOFF@
}}
@DRAW_HANDOFF@
using BitmapData=Instances::fl_display::BitmapData;
using Graphics=Instances::fl_display::Graphics;
static Ptr<Render::ComplexFill> produce(DrawingContext&drawing,const char*name){
    BitmapData bitmap(name);Graphics graphics{&drawing};graphics.beginBitmapFill(0,&bitmap,nullptr,true,false);
    return drawing.storage.fills.back().pFill;
}
static void assignment(Render::ComplexFill&a,const Render::ComplexFill&b){a=b;}
int main(int argc,char**argv){
    assert(argc==2);std::string mode(argv[1]);
    if(mode=="assignment"){
        Render::ComplexFill first,second;
        {DrawingContext a,b;auto af=produce(a,"a"),bf=produce(b,"b");first=*af.GetPtr();second=*bf.GetPtr();}
        // Only first's image and lease now keep resource A alive. Replacing the
        // lease before releasing that image is a real order violation.
        assignment(first,second);
        assignment(first,first);
        first.pImage->read();second.pImage->read();
        assignment(first,Render::ComplexFill());
        second.pImage->read();
    }else{
        DrawingContext drawing;
        Ptr<Render::ComplexFill> snapshot=produce(drawing,"a");
        snapshot->pImage->read();
        drawing.Clear();
        snapshot->pImage->read();
        Render::ComplexFill copy(*snapshot.GetPtr());
        snapshot.Clear();
        copy.pImage->read();
        assert(copy.FillMode.Fill==(Render::Wrap_Repeat|Render::Sample_Point));
        assert(copy.ImageMatrix.state==20);
        copy.pImage.Clear();
#ifdef STOCK_OWNER
        assert(events.empty()); // The last copy still owns the resource; no hidden AS3 root is needed.
        copy.pImageOwner.Clear();
#endif
    }
    assert(events.size()==(mode=="assignment"?4u:2u));
    for(size_t i=0;i<events.size();i+=2){assert(events[i].find("image:")==0);assert(events[i+1].find("owner:")==0);}
}
