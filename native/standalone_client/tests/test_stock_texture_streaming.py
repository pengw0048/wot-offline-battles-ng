"""Exercise the patched SDK owners for unsupported DDS and failed registration."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    'texture_streaming_patch', ROOT / 'bigworld/patch_stock_texture_streaming.py')
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


def sources(test):
    archive = Path(os.environ.get('WOT_PUBLIC_ENGINE_ARCHIVE',
                                  '/tmp/wot-bigworld-engine-14.4.1.tar.gz'))
    if not archive.is_file():
        if 'WOT_PUBLIC_ENGINE_ARCHIVE' in os.environ:
            test.fail('Configured engine archive is missing')
        test.skipTest('Pinned public engine archive is required')
    result = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            for relative in (PATCH.MANAGER, PATCH.TEXTURE):
                if member.name.endswith('/programming/bigworld/' + relative):
                    result[relative] = tar.extractfile(member).read().decode('utf-8')
            if len(result) == 2:
                break
    test.assertEqual(len(result), 2)
    return result


def body(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


HOST = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#define MF_ASSERT(condition) do { if (!(condition)) { std::fputs(#condition, stderr); std::exit(73); } } while(0)
using uint32 = unsigned;
enum { D3DFMT_UNKNOWN=0, D3DFMT_R8G8B8=20, D3DFMT_A8R8G8B8=21, D3DFMT_DXT1=71 };
namespace BW { struct StringRef : std::string {
 using std::string::string;
 bool equals_lower(const char* other) const { std::string copy=*this; for(char& c:copy)c=char(std::tolower(c)); return copy==other; }
}; }
struct Header { unsigned numMipLevels_=8; bool isFlatTexture_=true; int format_=D3DFMT_A8R8G8B8; };
static Header input;
static bool readable=true, validHeader=true, deviceSupports=true;
struct Stream {};
using FileStreamerPtr=std::shared_ptr<Stream>;
struct FileSystem { FileStreamerPtr streamFile(const BW::StringRef&) { return readable ? std::make_shared<Stream>() : nullptr; } };
struct BWResource {
 static BWResource& instance(){ static BWResource resource;return resource; }
 FileSystem* fileSystem(){ static FileSystem fs;return &fs; }
 static BW::StringRef getExtension(const BW::StringRef& name){return BW::StringRef(name.c_str()+name.find_last_of('.')+1);}
};
struct StreamingTextureLoader {
 using HeaderInfo=Header;
 static bool loadHeaderFromStream(Stream*,Header& header){header=input;return validHeader;}
};
namespace Moo { struct Context {bool supportsTextureFormat(int){return deviceSupports;}}; Context& rc(){static Context context;return context;} }
struct StreamingTextureHandle {int value=-1;bool isValid() const{return value>=0;}};
struct StreamingTexture;
struct TextureDetailLevel {int streamingPriority() const{return 4;}};
struct Entry {StreamingTexture* pTexture=nullptr;unsigned currentMip=0,requestedMip=0;int streamingPriority=0,format=0;bool streamingActive=false;};
using StreamingTextureEntry=Entry;
struct Entries {
 Entry value;bool occupied=false;unsigned releases=0;
 StreamingTextureHandle create(){assert(!occupied);occupied=true;value=Entry();return {0};}
 Entry& operator[](StreamingTextureHandle handle){MF_ASSERT(handle.isValid()&&occupied);return value;}
 void release(StreamingTextureHandle handle){MF_ASSERT(handle.isValid()&&occupied);occupied=false;++releases;}
};
struct SimpleMutexHolder {explicit SimpleMutexHolder(int&) {}};
struct TextureStreamingManager {
 enum {MODE_DISABLE=1,INVALID_MIP=~0u};
 unsigned streamingMode_=0,minMipLevel_=0;int textureLock_=0;Entries streamingTextures_;bool failLoad=false;
 bool canStreamResource(const BW::StringRef&);
 bool registerTexture(StreamingTexture*,const TextureDetailLevel*);
 void unregisterTexture(StreamingTexture*);
 void requestTextureUpdate(Entry& entry,unsigned,bool){if(!failLoad){entry.currentMip=2;entry.format=input.format_;}}
 void setIdealMip(Entry&,unsigned){}
};
static TextureStreamingManager* manager=nullptr;
static unsigned assigns=0;
struct StreamingTexture {
 StreamingTextureHandle streamingTextureHandle_;
 ~StreamingTexture();
 TextureStreamingManager* streamingManager(){return manager;}
 StreamingTextureHandle streamingTextureHandle()const{return streamingTextureHandle_;}
 void streamingTextureHandle(StreamingTextureHandle value){streamingTextureHandle_=value;}
 void assignTexture(void*){++assigns;}
};
@OWNERS@
int main(int argc,char** argv){
 if(argc!=2)return 1;
 TextureStreamingManager owner;manager=&owner;TextureDetailLevel detail;
 if(std::string(argv[1])=="capabilities"){
  assert(owner.canStreamResource("ordinary.DDS"));
  for(int format:{D3DFMT_R8G8B8,D3DFMT_A8R8G8B8,D3DFMT_DXT1}){
   input.format_=format;deviceSupports=true;assert(owner.canStreamResource("ordinary.dds"));
   deviceSupports=false;if(owner.canStreamResource("ordinary.dds"))return 74;
  }
  deviceSupports=true;input.format_=D3DFMT_UNKNOWN;assert(!owner.canStreamResource("ordinary.dds"));
  input.format_=D3DFMT_A8R8G8B8;input.numMipLevels_=1;assert(!owner.canStreamResource("ordinary.dds"));
  input.numMipLevels_=8;input.isFlatTexture_=false;assert(!owner.canStreamResource("ordinary.dds"));
  input.isFlatTexture_=true;readable=false;assert(!owner.canStreamResource("ordinary.dds"));
  readable=true;validHeader=false;assert(!owner.canStreamResource("ordinary.dds"));
  validHeader=true;assert(!owner.canStreamResource("ordinary.png"));
  owner.streamingMode_=TextureStreamingManager::MODE_DISABLE;assert(!owner.canStreamResource("ordinary.dds"));
 }else if(std::string(argv[1])=="failed"){
  owner.failLoad=true;auto* texture=new StreamingTexture;
  assert(!owner.registerTexture(texture,&detail));assert(!texture->streamingTextureHandle().isValid());
  assert(owner.streamingTextures_.releases==1);delete texture;
  assert(owner.streamingTextures_.releases==1&&assigns==1);
  // A failed attempt does not poison the next successful texture or its slot.
  owner.failLoad=false;texture=new StreamingTexture;assert(owner.registerTexture(texture,&detail));
  delete texture;assert(owner.streamingTextures_.releases==2&&assigns==2);
 }else{
  for(unsigned round=0;round<3;++round){
   auto* texture=new StreamingTexture;assert(owner.registerTexture(texture,&detail));
   delete texture;assert(!owner.streamingTextures_.occupied);
   assert(owner.streamingTextures_.releases==round+1);
  }
  manager=nullptr;delete new StreamingTexture;assert(assigns==4);
 }
 std::puts("texture owners passed");return 0;
}
'''


def generated(original):
    manager, texture = original[PATCH.MANAGER], original[PATCH.TEXTURE]
    owners = '\n'.join((body(manager, 'bool TextureStreamingManager::canStreamResource('),
                        body(manager, 'bool TextureStreamingManager::registerTexture('),
                        body(manager, 'void TextureStreamingManager::unregisterTexture('),
                        body(texture, 'StreamingTexture::~StreamingTexture()')))
    return HOST.replace('@OWNERS@', owners)


class TextureStreamingTest(unittest.TestCase):
    def test_patch_is_scoped_idempotent_and_checks_all_owners_before_writing(self):
        original = sources(self)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in original.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            PATCH.patch(root)
            patched = {name: (root / name).read_text() for name in original}
            PATCH.patch(root)
            self.assertEqual(patched, {name: (root / name).read_text() for name in original})
            for name, text in original.items():
                (root / name).write_text(text)
            (root / PATCH.TEXTURE).write_text('unknown owner')
            with self.assertRaises(ValueError):
                PATCH.patch(root)
            self.assertEqual((root / PATCH.MANAGER).read_text(), original[PATCH.MANAGER])

    def test_real_sdk_functions_handle_formats_failure_cleanup_and_reentry(self):
        original = sources(self)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, text in original.items():
                (root / name).parent.mkdir(parents=True, exist_ok=True)
                (root / name).write_text(text)
            PATCH.patch(root)
            patched = {name: (root / name).read_text() for name in original}
            for label, code in (('before', original), ('after', patched)):
                source, binary = root / (label + '.cpp'), root / label
                source.write_text(generated(code))
                subprocess.run(['c++', '-std=c++14', '-Wall', '-Werror', str(source),
                                '-o', str(binary)], check=True)
                for scenario in ('capabilities', 'failed', 'success'):
                    run = subprocess.run([str(binary), scenario], capture_output=True, text=True)
                    expected = {'capabilities': 74, 'failed': 73}.get(scenario, 0) if label == 'before' else 0
                    self.assertEqual(run.returncode, expected, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
