#include <cassert>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
using uint32=std::uint32_t;
using uint16=std::uint16_t;
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
#define SUCCEEDED(value) ((value)>=0)
const unsigned D3DPOOL_MANAGED=1,D3DUSAGE_WRITEONLY=8;
struct D3DXFLOAT16 {std::uint16_t bits;};
// Portable stand-in for the external D3DX conversion only. Python's independent
// IEEE-half encoder and the original x86 producer oracle check every output byte.
static D3DXFLOAT16* D3DXFloat32To16Array(D3DXFLOAT16* out,const float* in,unsigned count){
    for(unsigned i=0;i<count;++i){
        std::uint32_t bits;std::memcpy(&bits,in+i,4);
        unsigned sign=bits>>16&0x8000,mag=bits&0x7fffffff;
        int exponent=int(mag>>23)-127;unsigned value=0;
        if(exponent>15)value=0x7c00;
        else if(exponent>=-14)value=(mag+0xfff+((mag>>13)&1)-0x38000000)>>13;
        else if(exponent>=-25){unsigned shift=unsigned(-exponent-1),mant=(mag&0x7fffff)|0x800000;value=(mant+((1u<<(shift-1))-1)+((mant>>shift)&1))>>shift;}
        out[i].bits=std::uint16_t(sign|value);
    }
    return out;
}
namespace BW {
template<class T>using vector=std::vector<T>;
struct Vector2 {float x,y;void set(float a,float b){x=a;y=b;}};
namespace Moo {
static bool devicePresent=true,createFails=false,lockFails=false;
struct Context{void* device(){return devicePresent?this:nullptr;}};
static Context& rc(){static Context value;return value;}
struct Buffer {std::vector<unsigned char> bytes;bool preloaded=false;};
struct Bind {unsigned stream,stride;std::shared_ptr<Buffer> buffer;};
static std::vector<Bind> binds;
struct VertexBuffer {
    std::shared_ptr<Buffer> buffer;
    void release(){buffer.reset();}
    int create(unsigned bytes,unsigned,unsigned,unsigned,const char* = ""){
        if(createFails)return -1;
        buffer=std::make_shared<Buffer>();buffer->bytes.resize(bytes);return 0;
    }
    bool valid()const{return bool(buffer);}
    void set(unsigned stream,unsigned offset,unsigned stride)const{assert(offset==0);binds.push_back({stream,stride,buffer});}
    void addToPreloadList(){assert(buffer);buffer->preloaded=true;}
};
template<class T>struct VertexLock {
    T* data;
    explicit VertexLock(VertexBuffer& value):data(lockFails?nullptr:reinterpret_cast<T*>(value.buffer->bytes.data())){}
    operator void*()const{return data;}
    T& operator[](unsigned index){return data[index];}
};
}
namespace Terrain {
static float s_lodGeomorphingFix=.007f;
struct AliasedHeightMap {float value;float height(uint32,uint32)const{return value;}};
class GridVertexBuffer {
public:
    Moo::VertexBuffer pVertexBuffer_;
    uint16 resolutionX_=0,resolutionZ_=0;
    bool init(uint16,uint16);
    Moo::VertexBuffer pBuffer()const{return pVertexBuffer_;}
    static GridVertexBuffer* get(uint16 x,uint16 z){
        static std::map<unsigned,std::unique_ptr<GridVertexBuffer>> values;
        unsigned key=unsigned(x)<<16|z;auto& value=values[key];
        if(!value){value=std::make_unique<GridVertexBuffer>();if(!value->init(x,z)){value.reset();return nullptr;}}
        return value.get();
    }
};
class TerrainVertexBuffer {
public:
    Moo::VertexBuffer pVertexBuffer_;
    GridVertexBuffer* pGridBuffer_=nullptr;
    static void generate(const AliasedHeightMap*,const AliasedHeightMap*,uint32,uint32,BW::vector<Vector2>&);
    bool init(const Vector2*,uint32,uint32,uint32);
    bool set();
};
}
}
@HELPER@
namespace BW {namespace Terrain {
@GRID@
@GENERATE@
@HEIGHT@
@SET@
}}
static void bytes(const char* name,const BW::Moo::VertexBuffer& buffer){
    assert(buffer.valid()&&buffer.buffer->preloaded);std::cout<<name<<' ';
    for(unsigned char byte:buffer.buffer->bytes)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(byte);
    std::cout<<std::dec<<'\n';
}
int main(int argc,char** argv){
    using namespace BW;using namespace BW::Terrain;
    if(argc==1){
        Vector2 heights[4]={{-3.5f,.007f},{1,2},{3,4},{5,6}};
        TerrainVertexBuffer value;Moo::devicePresent=false;assert(!value.init(heights,2,2,0));assert(!value.set());
        Moo::devicePresent=true;Moo::createFails=true;assert(!value.init(heights,2,2,0));assert(!value.set());
        Moo::createFails=false;Moo::lockFails=true;assert(!value.init(heights,2,2,0));assert(!value.set());
        Moo::lockFails=false;assert(value.init(heights,2,2,0));assert(value.set());
        std::cout<<"failed upload is not drawn; later upload succeeds\n";return 0;
    }
    if(argc==2){
        assert(std::string(argv[1])=="generated");
        AliasedHeightMap base{-3.5f},coarse{0.f};BW::vector<Vector2> generated;
        TerrainVertexBuffer::generate(&base,&coarse,5,5,generated);
        TerrainVertexBuffer value;assert(value.init(generated.data(),5,5,0));bytes("HEIGHT",value.pVertexBuffer_);return 0;
    }
    assert(argc==3);unsigned x=unsigned(std::stoul(argv[1])),z=unsigned(std::stoul(argv[2]));
    std::vector<Vector2> heights(x*z);
    for(unsigned i=0;i<heights.size();++i)heights[i]={-100.f+float(i)*1.25f,70.f-float(i)*.5f};
    TerrainVertexBuffer value;assert(value.init(heights.data(),x,z,0));assert(value.set());assert(Moo::binds.size()==2);
    bytes("HEIGHT",value.pVertexBuffer_);bytes("GRID",value.pGridBuffer_->pBuffer());
    for(const auto& bind:Moo::binds)std::cout<<"BIND "<<bind.stream<<' '<<bind.stride<<' '<<(bind.buffer==value.pVertexBuffer_.buffer?"height":"grid")<<'\n';
}
