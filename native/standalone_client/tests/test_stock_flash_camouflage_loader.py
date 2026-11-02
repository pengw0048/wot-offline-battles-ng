"""Execute the production compositor/decoder owner with explicit DLL/BW/GFx boundaries."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags

ROOT = Path(__file__).resolve().parents[1]


class FlashCamouflageLoaderTests(unittest.TestCase):
    def test_actual_pinned_loader_dispatches_before_legacy_gpu_protocols(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine archive required')
        spec = importlib.util.spec_from_file_location('camouflage_patch', ROOT / 'bigworld/patch_scaleform_sdk.py')
        patch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(patch)
        with tarfile.open(archive) as files:
            member = next(value for value in files if value.name.endswith('/programming/bigworld/lib/scaleform/loader.cpp'))
            original = files.extractfile(member).read().decode('utf-8-sig').replace('\r\n', '\n')
        source = original

        def edit(target, before, after, label):
            nonlocal source
            self.assertEqual(target, ROOT / 'unused/lib/scaleform/loader.cpp')
            source = patch.replace_once(source, before, after, label)

        patch.protocol_image_copy_edits(ROOT / 'unused', edit)
        patch.protocol_camouflage_image_edits(ROOT / 'unused', edit)
        first = source
        patch.protocol_image_copy_edits(ROOT / 'unused', edit)
        patch.protocol_camouflage_image_edits(ROOT / 'unused', edit)
        self.assertEqual(source, first)
        self.assertEqual(source.count('#include "stock_flash_camouflage_loader.hpp"'), 1)
        self.assertEqual(source.count('return loadStockFlashCamouflage('), 1)
        start = source.index('BWToGFxImageLoader::LoadProtocolImage')
        self.assertLess(source.index('return loadStockFlashCamouflage(', start),
                        source.index('new Moo::SysMemTexture(', start))
        source = original.replace('BWToGFxImageLoader::LoadProtocolImage', 'changed_native_contract')
        with self.assertRaises(ValueError):
            patch.protocol_camouflage_image_edits(ROOT / 'unused', edit)

    def test_decode_completion_pixels_failures_and_owned_image_retirement(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        source = (ROOT / 'bigworld/stock_flash_camouflage_loader.hpp').read_text()
        source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include '))
        with tempfile.TemporaryDirectory(prefix='flash-camouflage-loader-') as folder:
            root = Path(folder)
            path = root / 'host.cpp'
            path.write_text(HOST.replace('@OWNER@', source))
            binary = root / 'host'
            subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off',
                            *native_test_compile_flags(compiler), '-I', str(ROOT / 'bigworld'),
                            str(path), str(ROOT / 'bigworld/stock_flash_camouflage.cpp'), '-o', str(binary)], check=True)
            for case in ('success', 'success-flat', 'success-depth', 'success-layers', 'success-next',
                         'success-cube', 'read-failure', 'generate-failure', 'flatten-generate-failure',
                         'disable-failure', 'flatten-size-failure', 'flatten-convert-failure',
                         'overlay-failure', 'load-failure', 'convert-failure', 'scale-failure',
                         'invalid-image-info', 'invalid-source-info', 'allocation-failure',
                         'image-data-failure', 'invalid-output-size', 'invalid-output-pitch',
                         'invalid-output-bounds', 'missing-library', 'missing-symbol'):
                result = subprocess.run([str(binary), case], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, (case, result.returncode, result.stdout, result.stderr))


HOST = r'''
#include "stock_flash_camouflage.hpp"
#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#define __stdcall
#define CHECK(value) do{if(!(value))return 41;}while(0)
using HMODULE=void*;
const unsigned LOAD_WITH_ALTERED_SEARCH_PATH=8;
std::string failure;unsigned libraries=0,freed=0,initialized=0,shutdowns=0,generated=0,removed=0,current=0,errors=0,stageErrors=0;
std::set<unsigned>images;
void error(const char*format,...){++errors;if(std::strstr(format,"stage="))++stageErrors;}
#define ERROR_MSG error
struct IFileSystem{enum{FT_FILE=1};};
namespace BW {
using wstring=std::wstring;
struct Block{std::vector<unsigned char>bytes;int len(){return int(bytes.size());}const void*data(){return bytes.data();}};
using BinaryPtr=std::shared_ptr<Block>;
struct Files{BinaryPtr readBinary(const char*path){if(failure=="read-failure")return nullptr;auto p=std::make_shared<Block>();p->bytes={static_cast<unsigned char>(std::string(path)=="mask.dds"?1:2)};return p;}};
struct BWResource{
 Files files;static BWResource&instance(){static BWResource value;return value;}
 Files*rootSection(){return &files;}
 static int resolveToAbsolutePath(wstring&path){if(failure=="missing-library")return 0;assert(path==L"../ResIL.dll"||path==L"../ILU.dll");path=L"C:/original/"+path.substr(3);return IFileSystem::FT_FILE;}
};
}
HMODULE LoadLibraryExW(const wchar_t*path,void*,unsigned flags){assert(flags==8&&std::wstring(path).find(L'/')==std::wstring::npos);return reinterpret_cast<void*>(static_cast<std::uintptr_t>(++libraries));}
unsigned long GetLastError(){return 126;}
void FreeLibrary(HMODULE){++freed;}
void imageInit(){++initialized;}void utilityInit(){}void shutdown(){++shutdowns;}
unsigned imageError(){return 0x506;}
struct Decoded {
 unsigned marker=0,width=1,height=1,depth=1,mipmaps=0,layers=0,next=0,cube=0;
 bool loaded=false,converted=false;std::vector<unsigned>pixels{0};
};
std::map<unsigned,Decoded>decoded;
unsigned flattened=0,overlaid=0;
unsigned generate(){
 if(failure=="generate-failure"||(failure=="flatten-generate-failure"&&images.size()==1))return 0;
 images.insert(++generated);decoded[generated]=Decoded();return generated;
}
void bind(unsigned value){assert(images.count(value));current=value;}
void removeImage(unsigned value){assert(images.erase(value)==1);decoded.erase(value);++removed;}
unsigned char disable(unsigned value){assert(value==0x636);return failure!="disable-failure";}
unsigned char load(unsigned type,const void*data,unsigned length){
 assert(type==0&&length==1);auto&i=decoded[current];i.marker=*static_cast<const unsigned char*>(data);
 i.width=4;i.height=6;i.loaded=true;i.pixels.assign(24,i.marker==1?0x00ff0000u:0xff001122u);
 if(failure=="success-depth")i.depth=2;else if(failure=="success-layers")i.layers=1;
 else if(failure=="success-next")i.next=1;else if(failure=="success-cube")i.cube=0x400;
 else if(failure!="success-flat")i.mipmaps=9;
 return failure!="load-failure";
}
unsigned char convert(unsigned format,unsigned type){
 assert(format==0x80e1&&type==0x1401);auto&i=decoded[current];
 if(failure=="convert-failure"&&i.loaded)return 0;
 if(failure=="flatten-convert-failure"&&!i.loaded)return 0;
 i.converted=true;return 1;
}
unsigned char scale(unsigned w,unsigned h,unsigned depth){
 assert(depth==1);auto&i=decoded[current];assert(!i.mipmaps&&!i.layers&&!i.next&&!i.cube&&i.depth==1);
 if(!i.loaded){++flattened;if(failure=="flatten-size-failure")return 0;}
 else if(failure=="scale-failure")return 0;
 i.width=w;i.height=h;i.pixels.resize(w*h);
 if(i.loaded)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
  i.pixels[y*w+x]=i.marker==1?((y==0?255u:y==1?128u:0u)<<16):(0xff001122u+y*0x10000u+x*0x100u);
 return 1;
}
unsigned char overlay(unsigned source,int x,int y,int z){
 assert(x==0&&y==0&&z==0);auto&from=decoded[source];auto&to=decoded[current];
 assert(to.width==from.width&&to.height==from.height&&to.converted&&to.depth==1&&source!=current);
 if(failure=="overlay-failure")return 0;
 to.marker=from.marker;to.loaded=true;to.pixels=from.pixels;++overlaid;return 1;
}
struct MirrorImageInfo{
 unsigned id;unsigned char*data;unsigned width,height,depth;unsigned char bpp;
 unsigned size,format,type,origin;unsigned char*palette;unsigned paletteType,paletteSize,cubeFlags,next,mipmaps,layers;
};
void describe(void*out){
 auto*p=static_cast<MirrorImageInfo*>(out);auto&i=decoded[current];
 p->width=i.width;p->height=i.height;p->depth=i.depth;p->bpp=4;p->size=i.width*i.height*4;
 p->format=0x80e1;p->type=0x1401;p->origin=0x601;p->data=reinterpret_cast<unsigned char*>(i.pixels.data());
 p->mipmaps=i.mipmaps;p->layers=i.layers;p->next=i.next;p->cubeFlags=i.cube;
 if(failure=="invalid-source-info"||(failure=="invalid-image-info"&&i.width==2))p->size=0;
}
void*GetProcAddress(HMODULE,const char*name){
 const std::string n(name);if(failure=="missing-symbol"&&n=="iluScale")return nullptr;
 if(n=="ilInit")return reinterpret_cast<void*>(&imageInit);if(n=="ilShutDown")return reinterpret_cast<void*>(&shutdown);
 if(n=="ilGetError")return reinterpret_cast<void*>(&imageError);
 if(n=="_ilGenImage@0")return reinterpret_cast<void*>(&generate);if(n=="ilBindImage")return reinterpret_cast<void*>(&bind);
 if(n=="_ilDeleteImage@4")return reinterpret_cast<void*>(&removeImage);
 if(n=="_ilDisable@4")return reinterpret_cast<void*>(&disable);
 if(n=="_ilOverlayImage@16")return reinterpret_cast<void*>(&overlay);
 if(n=="ilLoadL")return reinterpret_cast<void*>(&load);if(n=="ilConvertImage")return reinterpret_cast<void*>(&convert);
 if(n=="iluInit")return reinterpret_cast<void*>(&utilityInit);if(n=="iluScale")return reinterpret_cast<void*>(&scale);
 if(n=="_iluGetImageInfo@4")return reinterpret_cast<void*>(&describe);assert(false);return nullptr;
}
namespace Scaleform {
namespace Memory{void*pGlobalHeap=nullptr;}
template<class T>struct Ptr{
 T*p=nullptr;Ptr(){}Ptr(T&value):p(&value){}~Ptr(){if(p)p->Release();}
 T*operator->(){return p;}T*GetPtr(){return p;}explicit operator bool()const{return p!=nullptr;}
};
namespace Render {
enum{Image_R8G8B8A8=1};struct ImageSize{unsigned Width,Height;ImageSize(unsigned w,unsigned h):Width(w),Height(h){}};
struct ImagePlane{unsigned Width,Height,Pitch,DataSize;unsigned char*pData;};
struct ImageData{ImagePlane plane;void GetPlane(unsigned,ImagePlane*out){*out=plane;}};
unsigned alive=0;struct Image{unsigned refs=1;virtual~Image(){--alive;}void AddRef(){++refs;}void Release(){if(!--refs)delete this;}};
struct RawImage:Image {
 unsigned width,height,pitch;std::vector<unsigned char>data;
 RawImage(unsigned w,unsigned h):width(w),height(h),pitch(w*4+8),data(pitch*h,0xcd){++alive;}
 static RawImage*Create(unsigned format,unsigned levels,ImageSize size,unsigned,void*){
  assert(format==1&&levels==1);return failure=="allocation-failure"?nullptr:new RawImage(size.Width,size.Height);
 }
 bool GetImageData(ImageData*out){out->plane={width,height,pitch,unsigned(data.size()),data.data()};
  if(failure=="invalid-output-size")out->plane.Width=0;
  if(failure=="invalid-output-pitch")out->plane.Pitch=0;
  if(failure=="invalid-output-bounds")out->plane.DataSize=pitch*(height-1)+width*4-1;
  return failure!="image-data-failure";}
};
}}
namespace BW{namespace ScaleformBW{using namespace ::Scaleform;using UByte=unsigned char;}}
@OWNER@
int main(int argc,char**argv){
 CHECK(argc==2);failure=argv[1];
 CHECK(!BW::ScaleformBW::loadStockFlashCamouflage("bad"));CHECK(libraries==0);
 const char*uri="img://camouflage,2,3,\"mask.dds\",\"background.dds\",4279246896,0,0,0,1,0,0,0";
 for(unsigned i=0;i<2;++i){
  auto*image=BW::ScaleformBW::loadStockFlashCamouflage(uri);
  if(failure.find("success")==0){
   CHECK(image);auto*raw=static_cast<Scaleform::Render::RawImage*>(image);
   for(unsigned y=0;y<3;++y){for(unsigned x=0;x<2;++x){const auto*p=raw->data.data()+y*raw->pitch+x*4;wot::flash_camouflage::Parameters params;params.colors[0]=4279246896u;params.weights[0]=1.f;
    const auto expected=wot::flash_camouflage::pixel((y==0?255u:y==1?128u:0u)<<16,0xff001122u+y*0x10000u+x*0x100u,params);
    CHECK(p[0]==(expected&255)&&p[1]==((expected>>8)&255)&&p[2]==((expected>>16)&255)&&p[3]==255);}
    for(unsigned x=8;x<raw->pitch;++x)CHECK(raw->data[y*raw->pitch+x]==0xcd);}
   CHECK(raw->refs==1);image->Release();
  }else CHECK(!image);
  CHECK(images.empty()&&Scaleform::Render::alive==0&&generated==removed);
 }
 CHECK(libraries==(failure=="missing-library"?0u:2u));
 CHECK(initialized==((failure=="missing-library"||failure=="missing-symbol")?0u:1u));
 const bool outputFailure=failure=="allocation-failure"||failure=="image-data-failure"||failure.find("invalid-output-")==0;
 if(failure.find("success")==0)CHECK(overlaid==(failure=="success-flat"?0u:4u));
 CHECK(stageErrors==((failure.find("success")==0||outputFailure)?0u:1u));
}
'''


if __name__ == '__main__':
    unittest.main()
