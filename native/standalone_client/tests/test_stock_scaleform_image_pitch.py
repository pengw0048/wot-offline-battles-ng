"""Protocol image copies honor D3D/RawImage strides, including compressed block rows."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest

from native_test_compile_flags import native_test_compile_flags
from test_stock_cursor_camera import function
from test_stock_scaleform_scene import REVISION

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('protocol_image_pitch', ROOT / 'bigworld/patch_scaleform_sdk.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class ScaleformImagePitchTests(unittest.TestCase):
    def test_copy_owner_matches_the_pinned_public_loader(self):
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine archive required')
        with tarfile.open(archive) as files:
            member = next(value for value in files if value.name.endswith('/programming/bigworld/lib/scaleform/loader.cpp'))
            source = files.extractfile(member).read().decode('utf-8-sig').replace('\r\n', '\n')
        self.assertEqual(source.count(PATCH.PROTOCOL_IMAGE_COPY_BEFORE), 1)

    def test_fresh_cached_and_repeated_patch_share_one_copy_owner(self):
        path = ROOT / 'unused-engine/lib/scaleform/loader.cpp'
        for initial in (PATCH.PROTOCOL_IMAGE_COPY_BEFORE, PATCH.PROTOCOL_IMAGE_COPY_PREVIOUS):
            changes = {path: initial}

            def edit(target, before, after, label):
                changes[target] = PATCH.replace_once(changes[target], before, after, label)

            PATCH.protocol_image_copy_edits(ROOT / 'unused-engine', edit)
            self.assertEqual(changes[path], PATCH.PROTOCOL_IMAGE_COPY_AFTER)
            PATCH.protocol_image_copy_edits(ROOT / 'unused-engine', edit)
            self.assertEqual(changes[path], PATCH.PROTOCOL_IMAGE_COPY_AFTER)
        changes[path] = initial.replace('tex->UnlockRect(0);', 'changed source')
        with self.assertRaises(ValueError):
            PATCH.protocol_image_copy_edits(ROOT / 'unused-engine', edit)
        self.assertIn('changed source', changes[path])

    def test_actual_sdk_geometry_and_production_copy_with_negative_control(self):
        compiler = shutil.which(os.environ.get('CXX', 'c++'))
        if not compiler:
            self.skipTest('Host C++ compiler required')
        sdk = Path(os.environ.get('WOT_SCALEFORM_SDK_GIT', '/Users/peng/.cache/wot-independent-client/scaleform-sdk-4.2'))
        try:
            image = subprocess.check_output(['git', '-C', str(sdk), 'show',
                REVISION + ':Src/Render/Render_Image.cpp']).decode('utf-8-sig').replace('\r\n', '\n')
        except (OSError, subprocess.CalledProcessError):
            self.skipTest('Pinned Scaleform SDK required')
        geometry = '\n'.join(function(image, signature) for signature in (
            'UPInt  ImageData::GetFormatPitch(', 'UPInt  ImageData::GetFormatScanlineCount('))
        archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE', '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
        if not archive.is_file():
            self.skipTest('Pinned public engine archive required')
        with tarfile.open(archive) as files:
            member = next(value for value in files if value.name.endswith('/programming/bigworld/lib/moo/moo_dx.cpp'))
            dx = files.extractfile(member).read().decode('utf-8-sig').replace('\r\n', '\n')
        surface_size = function(dx, 'BW_NAMESPACE uint32 DX::surfaceSize(')
        formats = ','.join(sorted(set(re.findall(r'D3DFMT_[A-Za-z0-9_]+', surface_size))))
        with tempfile.TemporaryDirectory(prefix='scaleform-pitched-images-') as folder:
            root = Path(folder)
            for name, body in (('production', PATCH.PROTOCOL_IMAGE_COPY_AFTER),
                               ('old-contiguous-copy', PATCH.PROTOCOL_IMAGE_COPY_PREVIOUS)):
                path = root / (name + '.cpp')
                path.write_text(HOST.replace('@GEOMETRY@', geometry).replace('@COPY@', body)
                                .replace('@SURFACE_SIZE@', surface_size).replace('@D3D_FORMATS@', formats))
                binary = root / name
                subprocess.run([compiler, '-std=c++14', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
                                *native_test_compile_flags(compiler), str(path), '-o', str(binary)], check=True)
                result = subprocess.run([str(binary)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0 if name == 'production' else 41,
                                 (name, result.returncode, result.stdout, result.stderr))


HOST = r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <vector>
#include <limits>
using UInt32=std::uint32_t;using UPInt=std::uint32_t;using UByte=unsigned char;
using UInt=UInt32;
using uint=unsigned;using uint32=std::uint32_t;using UINT=unsigned;
enum D3DFORMAT{@D3D_FORMATS@};
#define BW_NAMESPACE
#define BW_USE_NAMESPACE
#define MF_ASSERT(value) assert(value)
uint32 roundUpToPow2Multiple(uint32 value,uint32 multiple){return (value+multiple-1)&~(multiple-1);}
const UPInt SF_MAX_UPINT=std::numeric_limits<UPInt>::max();
#define SF_UNUSED(value) ((void)(value))
#define CHECK(value) do{if(!(value))return 41;}while(0)
enum {D3D_OK=0,D3DLOCK_READONLY=1};
UByte*copyTarget=nullptr;unsigned copyTargetSize=0;bool attemptedOverflow=false;
void*copyBytes(void*dest,const void*source,unsigned length){
 if(length>copyTargetSize||static_cast<UByte*>(dest)-copyTarget>copyTargetSize-length){attemptedOverflow=true;return dest;}
 return std::memcpy(dest,source,length);
}
namespace Render {
enum ImageFormat {Image_A8,Image_P8,Image_Y8_U2_V2,Image_Y8_U2_V2_A8,
 Image_R8G8B8A8,Image_B8G8R8A8,Image_R8G8B8,Image_B8G8R8,
 Image_DXT1,Image_DXT3,Image_DXT5,Image_PVRTC_RGB_2BPP,Image_PVRTC_RGBA_2BPP,
 Image_PVRTC_RGB_4BPP,Image_PVRTC_RGBA_4BPP,Image_ETC1_RGB_4BPP,Image_ATCIC,Image_ATCICA,
 ImageFormat_Mask=0xffff};
struct ImagePlane {UInt32 Width,Height;UPInt Pitch,DataSize;UByte*pData;};
struct ImageData {
 ImagePlane plane;
 static UPInt GetFormatPitch(ImageFormat,UInt32,unsigned plane=0);
 static UPInt GetFormatScanlineCount(ImageFormat,UInt32,unsigned plane=0);
 void GetPlane(unsigned,ImagePlane*out){*out=plane;}
};
@GEOMETRY@
unsigned alive=0;
struct RawImage {
 unsigned references=1;ImagePlane plane;bool available;
 RawImage(ImagePlane p,bool a):plane(p),available(a){++alive;}
 ~RawImage(){--alive;}
 void AddRef(){++references;}void Release(){if(!--references)delete this;}
 bool GetImageData(ImageData*out){out->plane=plane;copyTarget=plane.pData;copyTargetSize=plane.DataSize;return available;}
};
}
struct D3DLOCKED_RECT {int Pitch;void*pBits;};
namespace DX {
struct Texture {
 D3DLOCKED_RECT rect;bool lockable;unsigned locks=0,unlocks=0;
 int LockRect(unsigned,D3DLOCKED_RECT*out,void*,unsigned){++locks;*out=rect;return lockable?0:-1;}
 void UnlockRect(unsigned){++unlocks;}
};
uint32 surfaceSize(uint,uint,D3DFORMAT);
}
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
@SURFACE_SIZE@
#pragma GCC diagnostic pop
struct SysTexture{DX::Texture*tex;Render::ImageFormat fmt;void*pTexture(){return tex;}
 D3DFORMAT format(){return fmt==Render::Image_DXT1?D3DFMT_DXT1:fmt==Render::Image_DXT3?D3DFMT_DXT3:fmt==Render::Image_DXT5?D3DFMT_DXT5:D3DFMT_A8R8G8B8;}};
struct ImagePtr {
 Render::RawImage*value;~ImagePtr(){if(value)value->Release();}
 Render::RawImage*operator->(){return value;}Render::RawImage*GetPtr(){return value;}
 operator bool()const{return value!=nullptr;}
};
#define memcpy copyBytes
Render::RawImage*copy(SysTexture*pTex,Render::RawImage*raw,Render::ImageFormat fmt,UInt width,UInt height){
 (void)fmt;ImagePtr pimage{raw};@COPY@
}
int main(){
 // The old helper estimates an aligned GPU allocation, not RawImage bytes.
 CHECK(DX::surfaceSize(3,5,D3DFMT_A8R8G8B8)==512);
 CHECK(Render::ImageData::GetFormatPitch(Render::Image_B8G8R8A8,3)*5==60);
 CHECK(DX::surfaceSize(52,52,D3DFMT_A8R8G8B8)==13312);
 CHECK(Render::ImageData::GetFormatPitch(Render::Image_B8G8R8A8,52)*52==10816);
 for(Render::ImageFormat fmt:{Render::Image_R8G8B8A8,Render::Image_B8G8R8A8,Render::Image_DXT1,Render::Image_DXT3,Render::Image_DXT5})
 for(unsigned width:{1u,3u,4u,5u,8u,13u})for(unsigned height:{1u,2u,4u,7u,12u})
 for(unsigned sp:{0u,8u})for(unsigned dp:{0u,12u}){
  const unsigned bytes=Render::ImageData::GetFormatPitch(fmt,width),rows=Render::ImageData::GetFormatScanlineCount(fmt,height);
  std::vector<UByte>source((bytes+sp)*rows,0xee),dest((bytes+dp)*rows,0xdd);
  for(unsigned row=0;row<rows;++row)for(unsigned x=0;x<bytes;++x)source[row*(bytes+sp)+x]=UByte(row*31+x);
  DX::Texture tex{{int(bytes+sp),source.data()},true};SysTexture pTex{&tex,fmt};
  auto*out=copy(&pTex,new Render::RawImage({width,height,bytes+dp,unsigned(dest.size()),dest.data()},true),fmt,width,height);
  if(attemptedOverflow){if(out)out->Release();return 41;}
  CHECK(out&&out->references==1&&tex.locks==1&&tex.unlocks==1);
  for(unsigned row=0;row<rows;++row){
   for(unsigned x=0;x<bytes;++x)CHECK(dest[row*(bytes+dp)+x]==UByte(row*31+x));
   for(unsigned x=bytes;x<bytes+dp;++x)CHECK(dest[row*(bytes+dp)+x]==0xdd);
  }
  out->Release();CHECK(Render::alive==0);
 }
 // Reject every invalid stride/bounds form before writing a partial image.
 for(unsigned which=0;which<14;++which){
  std::vector<UByte>source(256,0xee),dest(256,0xdd);
  Render::ImageFormat fmt=Render::Image_B8G8R8A8;unsigned width=3,height=5;
  Render::ImagePlane plane{width,height,16,80,dest.data()};
  DX::Texture tex{{16,source.data()},true};bool available=true,present=true;
  switch(which){
   case 0:tex.rect.Pitch=0;break;case 1:tex.rect.Pitch=-16;break;
   case 2:tex.rect.Pitch=8;break;case 3:plane.Pitch=8;break;
   case 4:plane.DataSize=75;break;case 5:plane.pData=nullptr;break;
   case 6:tex.rect.pBits=nullptr;break;case 7:plane.Width=4;break;
   case 8:plane.Height=4;break;case 9:available=false;break;
   case 10:tex.lockable=false;break;case 11:present=false;break;
   case 12:tex.rect.Pitch=0x7fffffff;break;
   case 13:plane.Pitch=SF_MAX_UPINT;plane.DataSize=SF_MAX_UPINT;break;
  }
  SysTexture pTex{present?&tex:nullptr,fmt};
  CHECK(copy(&pTex,new Render::RawImage(plane,available),fmt,width,height)==nullptr);
  CHECK(Render::alive==0);CHECK(tex.unlocks==unsigned(present&&tex.lockable));
  for(auto value:dest)CHECK(value==0xdd);
 }
}
'''


if __name__ == '__main__':
    unittest.main()
