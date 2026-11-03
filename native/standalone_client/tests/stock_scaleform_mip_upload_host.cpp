#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using UByte=unsigned char;using UInt32=std::uint32_t;using UPInt=std::uint32_t;
#define SF_STDCALL
#define SF_ASSERT(x) assert(x)
#define SF_UNUSED(x) ((void)(x))
namespace Alg {template<class T>T Min(T a,T b){return std::min(a,b);}template<class T>T Max(T a,T b){return std::max(a,b);}}
enum ImageFormat {Image_None,Image_R8G8B8A8,Image_B8G8R8A8,Image_R8G8B8,Image_B8G8R8,
 Image_A8,Image_P8,Image_Y8_U2_V2,Image_Y8_U2_V2_A8,Image_DXT1,Image_DXT3,Image_DXT5,
 Image_PVRTC_RGB_2BPP,Image_PVRTC_RGBA_2BPP,Image_PVRTC_RGB_4BPP,Image_PVRTC_RGBA_4BPP,
 Image_ETC1_RGB_4BPP,Image_ATCIC,Image_ATCICA,ImageFormat_Mask=0xffff};
struct ImageSize {UInt32 Width,Height;bool operator==(const ImageSize&o)const{return Width==o.Width&&Height==o.Height;}};
struct ImagePlane {
 UInt32 Width=0,Height=0;UPInt Pitch=0,DataSize=0;UByte*pData=nullptr;
 ImageSize GetSize()const{return {Width,Height};}
 void SetNextMipSize(){Width=std::max(1u,Width/2);Height=std::max(1u,Height/2);}
 void GetMipLevel(ImageFormat,unsigned,ImagePlane*,unsigned=0)const;
};
struct PalettePtr {void*GetPtr()const{return nullptr;}};
struct ImageData {
 enum{Flag_SeparateMipmaps=1};ImageFormat Format=Image_None;unsigned Flags=0,LevelCount=0,RawPlaneCount=1;
 PalettePtr pPalette;std::vector<ImagePlane> planes;
 bool IsHWSpecific()const{return false;}bool HasSeparateMipmaps()const{return (Flags&Flag_SeparateMipmaps)!=0;}
 ImageFormat GetFormat()const{return Format;}ImageFormat GetFormatNoConv()const{return Format;}
 ImageSize GetSize()const{return planes[0].GetSize();}UInt32 GetWidth()const{return planes[0].Width;}UInt32 GetHeight()const{return planes[0].Height;}
 unsigned GetBitsPerPixel()const{return GetFormatBitsPerPixel(Format);}
 unsigned GetMipLevelCount()const{return LevelCount;}
 const ImagePlane&GetPlaneRef(unsigned p)const{assert(p<RawPlaneCount);return planes[p];}
 unsigned GetPlaneCount()const;void GetPlane(unsigned,ImagePlane*)const;
 void GetMipLevelPlane(unsigned level,unsigned plane,ImagePlane*out)const{GetPlane(level*GetFormatPlaneCount(Format)+plane,out);}
 static unsigned GetFormatPlaneCount(ImageFormat);static unsigned GetFormatBitsPerPixel(ImageFormat,unsigned=0);
 static UPInt GetFormatPitch(ImageFormat,UInt32,unsigned=0);static UPInt GetFormatScanlineCount(ImageFormat,UInt32,unsigned=0);
 static UPInt GetMipLevelSize(ImageFormat,const ImageSize&,unsigned=0);
};
using CopyScanlineFunc=void(*)(UByte*,const UByte*,UPInt,void*,void*);
struct RawImage {ImageData Data;bool Decode(ImageData*,CopyScanlineFunc,void*)const;};
@GEOMETRY@
@DECODE@
struct Range {UByte*data;UPInt size;};
std::vector<Range> writable,readable;bool overflow=false;
bool contained(const std::vector<Range>&ranges,const UByte*p,UPInt n){
 for(const auto&r:ranges)if(reinterpret_cast<std::uintptr_t>(p)>=reinterpret_cast<std::uintptr_t>(r.data)&&
    reinterpret_cast<std::uintptr_t>(p)-reinterpret_cast<std::uintptr_t>(r.data)<=r.size&&
    n<=r.size-(reinterpret_cast<std::uintptr_t>(p)-reinterpret_cast<std::uintptr_t>(r.data)))return true;
 return false;
}
void copy(UByte*d,const UByte*s,UPInt n,void*,void*){
 if(!contained(writable,d,n)){overflow=true;return;}
 assert(contained(readable,s,n));std::memcpy(d,s,n);
}
struct Storage {std::vector<std::vector<UByte>> blocks;ImageData data;};
Storage make(ImageFormat format,unsigned levels,unsigned width,unsigned height,bool separate,unsigned pad,UByte fill){
 Storage s;s.data.Format=format;s.data.LevelCount=levels;s.data.Flags=separate?ImageData::Flag_SeparateMipmaps:0;
 unsigned count=ImageData::GetFormatPlaneCount(format);
 s.data.RawPlaneCount=count*(separate?levels:1);s.blocks.resize(s.data.RawPlaneCount);s.data.planes.resize(s.data.RawPlaneCount);
 for(unsigned plane=0;plane<count;++plane){
  unsigned pw=width,ph=height;
  if((format==Image_Y8_U2_V2||format==Image_Y8_U2_V2_A8)&&(plane==1||plane==2)){pw/=2;ph/=2;}
  unsigned total=0,w=pw,h=ph;
  for(unsigned l=0;l<levels;++l){total+=ImageData::GetMipLevelSize(format,{w,h},plane);w=std::max(1u,w/2);h=std::max(1u,h/2);}
  if(!separate){s.blocks[plane].assign(total+64,fill+plane);s.data.planes[plane]={pw,ph,ImageData::GetFormatPitch(format,pw,plane),total,s.blocks[plane].data()};}
  else for(unsigned l=0;l<levels;++l){
   unsigned i=l*count+plane,pitch=ImageData::GetFormatPitch(format,pw,plane)+pad,bytes=pitch*ImageData::GetFormatScanlineCount(format,ph,plane);
   s.blocks[i].assign(bytes+64,fill+plane);s.data.planes[i]={pw,ph,pitch,bytes,s.blocks[i].data()};
   pw=std::max(1u,pw/2);ph=std::max(1u,ph/2);
  }
 }
 return s;
}
int check(ImageFormat format,unsigned levels,unsigned targetLevels,unsigned width,unsigned height,bool sourceSeparate,bool targetSeparate,const UByte*payload=nullptr,unsigned payloadSize=0){
 Storage source=make(format,levels,width,height,sourceSeparate,8,0x5a),dest=make(format,targetLevels,width,height,targetSeparate,16,0xcc);
 if(payload){assert(!sourceSeparate&&source.data.RawPlaneCount==1);assert(payloadSize>=source.data.planes[0].DataSize);std::memcpy(source.blocks[0].data(),payload,source.data.planes[0].DataSize);}
 readable.clear();writable.clear();overflow=false;
 for(unsigned i=0;i<source.data.RawPlaneCount;++i)readable.push_back({source.data.planes[i].pData,source.data.planes[i].DataSize});
 for(unsigned i=0;i<dest.data.RawPlaneCount;++i)writable.push_back({dest.data.planes[i].pData,dest.data.planes[i].DataSize});
 RawImage image;image.Data=source.data;
 if(!image.Decode(&dest.data,copy,nullptr))return 42;
 if(overflow){std::puts("copy outside destination mip allocation");return 41;}
 unsigned planes=ImageData::GetFormatPlaneCount(format);
 for(unsigned l=0;l<std::min(levels,targetLevels);++l)for(unsigned plane=0;plane<planes;++plane){
  ImagePlane p,s;dest.data.GetMipLevelPlane(l,plane,&p);source.data.GetMipLevelPlane(l,plane,&s);
  unsigned rows=ImageData::GetFormatScanlineCount(format,p.Height,plane);
  unsigned rowBytes=(format==Image_DXT1||format==Image_DXT3||format==Image_DXT5)?ImageData::GetFormatPitch(format,p.Width,plane):p.Width*ImageData::GetFormatBitsPerPixel(format,plane)/8;
  for(unsigned y=0;y<rows;++y)if(std::memcmp(p.pData+y*p.Pitch,s.pData+y*s.Pitch,rowBytes))return 43;
 }
 for(unsigned i=0;i<dest.data.RawPlaneCount;++i)for(unsigned j=dest.data.planes[i].DataSize;j<dest.blocks[i].size();++j)assert(dest.blocks[i][j]==UByte(0xcc+i%planes));
 return 0;
}
int main(int argc,char**argv){
 // The first case is a two-LockRect upload with independent GPU mip allocations.
 if(int r=check(Image_R8G8B8A8,3,2,8,8,false,true))return r;
 unsigned cases=1;
 for(ImageFormat fmt:{Image_R8G8B8A8,Image_A8,Image_R8G8B8,Image_DXT1,Image_DXT3,Image_DXT5,Image_Y8_U2_V2,Image_Y8_U2_V2_A8})
  for(unsigned width:{1u,2u,3u,4u,5u,8u,9u,128u,256u})for(unsigned height:{1u,3u,4u,7u,128u})
   for(unsigned levels:{1u,2u,3u})for(bool src:{false,true})for(bool dst:{false,true}){
    if((fmt==Image_Y8_U2_V2||fmt==Image_Y8_U2_V2_A8)&&(width<4||height<4))continue;
    if(int r=check(fmt,levels,levels,width,height,src,dst))return r;
    ++cases;
   }
 for(int i=1;i<argc;++i){
  FILE*f=std::fopen(argv[i],"rb");assert(f);std::fseek(f,0,SEEK_END);long n=std::ftell(f);assert(n>128);std::rewind(f);
  std::vector<UByte> dds(static_cast<std::size_t>(n));assert(std::fread(dds.data(),1,dds.size(),f)==dds.size());std::fclose(f);
  assert(!std::memcmp(dds.data(),"DDS ",4));unsigned width,height,levels;std::memcpy(&height,dds.data()+12,4);std::memcpy(&width,dds.data()+16,4);std::memcpy(&levels,dds.data()+28,4);
  assert(!std::memcmp(dds.data()+84,"DXT5",4)&&levels>1);
  if(int r=check(Image_DXT5,levels,levels-1,width,height,false,true,dds.data()+128,unsigned(dds.size()-128)))return r;
  std::printf("exact DDS upload passed: %ux%u mips=%u %s\n",width,height,levels,argv[i]);
 }
 std::printf("guarded mip layouts passed: %u\n",cases);return 0;
}
