"""Execute SDK batching with native/public consumers and count matrix uploads."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_stock_space_models as fixtures


HOST = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>
using uint32=std::uint32_t;using HRESULT=int;using D3DPRIMITIVETYPE=unsigned;
const HRESULT E_FAIL=-1;
const unsigned D3DUSAGE_WRITEONLY=1,D3DUSAGE_DYNAMIC=2,D3DPOOL_DEFAULT=0;
const unsigned D3DLOCK_DISCARD=1,D3DSTREAMSOURCE_INDEXEDDATA=0x40000000u,D3DSTREAMSOURCE_INSTANCEDATA=0x80000000u;
const unsigned NON_INSTANCED_ROP=0xffffffffu;
#define SUCCEEDED(value) ((value)>=0)
#define BW_GUARD
#define MF_ASSERT(value) assert(value)
#define ERROR_MSG(...) (++errors)
unsigned errors=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
namespace BW{template<class T>using vector=std::vector<T>;}
struct Matrix{float data[16];};static_assert(sizeof(Matrix)==64,"native matrix width");
struct Effect{bool stock;Effect* pEffect(){return this;}void setAutoConstants(){}void commitChanges(){}};
namespace wot{bool is_stock_effect(Effect* effect){return effect->stock;}}
struct Visual{const Matrix* matrices=nullptr;unsigned count=0;
void stockInstancingStream(const Matrix* value,unsigned size){matrices=value;count=size;}
void worldMatrix(const Matrix*){}};
struct Context{
Effect* effect=nullptr;Visual* visual=nullptr;unsigned bound=0,offset=0,stride=0,frequency=0,publicBindings=0;
const std::vector<unsigned char>* bytes=nullptr;std::vector<unsigned> draws;
Context* device(){return this;}
bool isHWInstancingAvailable()const{return true;}
void consume(const Matrix& value){
 const auto identity=unsigned(value.data[0]);
 for(unsigned column=0;column<16;++column)
  require(value.data[column]==float(identity+column*10000),"instance matrix payload changed");
 draws.push_back(identity);
}
void SetStreamSourceFreq(unsigned stream,unsigned value){if(stream==0)frequency=value;}
void drawIndexedInstancedPrimitive(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned count){
require(effect&&visual,"draw lost its owner");
if(effect->stock){
 require(bound==2&&offset==0&&stride==4&&count<=16&&visual->count==count,"native index/matrix packet changed");
 for(unsigned i=0;i<count;++i)consume(visual->matrices[i]);
}else{
 require(bound==1&&stride==64&&bytes,"public matrix stream changed");
 for(unsigned i=0;i<count;++i){Matrix value;
  require(offset+(i+1)*64<=bytes->size(),"public instance offset exceeded upload");
  memcpy(&value,bytes->data()+offset+i*64,64);consume(value);}
}
require((frequency&~D3DSTREAMSOURCE_INDEXEDDATA)==count,"instance frequency changed");
}
};
Context context;Context& rc(){return context;}
namespace Moo{Context& rc(){return context;}}
struct VertexBuffer{
unsigned id=1,locks=0,unlocks=0,uploads=0,uploadedBytes=0,pending=0;
bool available=true,failCreate=false,failLock=false,failUnlock=false,nullDestination=false;std::vector<unsigned char> bytes;
bool valid()const{return available;}
HRESULT create(unsigned,unsigned,unsigned,unsigned,const char*){available=!failCreate;return available?0:E_FAIL;}
void release(){available=false;}
HRESULT lock(unsigned offset,unsigned size,void** output,unsigned){
 ++locks;require(offset==0&&!pending,"invalid matrix lock");*output=nullptr;
 if(!available||failLock)return E_FAIL;
 pending=size;bytes.resize(size);if(!nullDestination)*output=bytes.data();return 0;
}
HRESULT unlock(){
 ++unlocks;require(pending!=0,"unlock without successful lock");
 if(!failUnlock&&!nullDestination){++uploads;uploadedBytes+=pending;}
 pending=0;return failUnlock?-1:0;
}
void set(unsigned,unsigned offset,unsigned stride){
 require(available,"invalid instance buffer was bound");
 context.bound=id;context.offset=offset;context.stride=stride;context.bytes=&bytes;
 if(id==1)++context.publicBindings;
}
static void reset(unsigned){context.bound=0;}
};
struct InstancingStream{enum{STREAM_NUMBER=9,ELEMENT_SIZE=64};};
struct DrawContext{struct InstanceData{unsigned paletteSize_=0;Matrix value;
Matrix* matrix(){return &value;}};};
struct RenderOp{
enum{FLAG_HW_INSTANCING=128,PRIMTYPE_MASK=15};unsigned flags_=FLAG_HW_INSTANCING|4;
Effect* effect_;DrawContext::InstanceData* instanceData_;
void *technique_=nullptr,*materialProperties_=nullptr,*vertexDeclaration_=nullptr,*vertexBuffer_=nullptr,*indexBuffer_=nullptr;
unsigned globalStateBlockIndex_=0;
struct{unsigned startIndex_=0,nVertices_=3,nPrimitives_=1,startVertex_=0;}primitiveGroupInfo_;
};
struct InstancedRenderOp{const RenderOp* reference_;unsigned numInstances_;};
template<class T>struct Array:std::vector<T>{T& push_back(){this->emplace_back();return this->back();}};
struct Processor{
static const unsigned MAX_NUM_INSTANCED_MATRICES=4096;
@FIELDS@
Visual& effectVisualContext_;Array<InstancedRenderOp> instancedRenderOps_;
const RenderOp* instancingChainRef_=nullptr;
Matrix *instanceMatrixArray_=nullptr,*startInstanceMatrix_=nullptr,*nextInstanceMatrix_=nullptr;
explicit Processor(Visual& visual):effectVisualContext_(visual){stockInstanceIndexVB_.id=2;context.visual=&visual;}
void nullifyActiveState(){}void closeActiveEffect(){}
void applyInstanceData(DrawContext::InstanceData*){}
void processRop(const RenderOp* rop,unsigned count,unsigned offset){context.effect=rop->effect_;flushRop(rop,count,offset);}
@METHODS@
};

void exercise(const std::vector<bool>& native,unsigned failure=0,bool oldControl=false){
context=Context();errors=0;Visual visual;Processor processor(visual);
const bool publicFailure=failure>=1&&failure<=5;
processor.stockInstanceIndexVB_.available=failure!=6;
processor.instancedVB_.failLock=failure==1;processor.instancedVB_.nullDestination=failure==2;
processor.instancedVB_.failUnlock=failure==3;
processor.instancedVB_.available=failure!=4;
if(failure==5){
 processor.stockInstanceIndexVB_.available=false;processor.instancedVB_.available=false;
 processor.instancedVB_.failCreate=true;processor.createUnmanagedObjects();
 require(processor.stockInstanceIndexVB_.valid()&&!processor.instancedVB_.valid()&&errors==1,
         "unused public creation failure cancelled the native stream");
 errors=0;
}
Effect nativeEffect{true},publicEffect{false};
std::vector<DrawContext::InstanceData> packets(native.size());std::vector<RenderOp> operations(native.size());
for(unsigned i=0;i<native.size();++i){
 for(unsigned j=0;j<16;++j)packets[i].value.data[j]=float(i+j*10000);
 operations[i].effect_=native[i]?&nativeEffect:&publicEffect;operations[i].instanceData_=&packets[i];
 processor.processRenderOp(&operations[i]);
}
processor.flushInstancedRenderOps();
std::vector<unsigned> expected;
for(unsigned i=0;i<native.size();++i)if(native[i]?failure!=6:!publicFailure)expected.push_back(i);
require(context.draws==expected,"mixed batching changed instance identity/order");
require(processor.instancedRenderOps_.empty()&&!processor.instanceMatrixArray_,"batch retained pending storage");
unsigned batches=0,expectedBytes=0;
for(unsigned begin=0;begin<native.size();begin+=4096){
 const unsigned end=std::min(begin+4096,unsigned(native.size()));
 if(std::find(native.begin()+begin,native.begin()+end,false)!=native.begin()+end){++batches;expectedBytes+=(end-begin)*64;}
}
if(oldControl)require(processor.instancedVB_.uploads==0,"stock-only batch uploaded an unused public matrix stream");
require(processor.instancedVB_.locks==((failure==4||failure==5)?0:batches),"unused matrix lock or missed public upload");
require(processor.instancedVB_.uploads==(publicFailure?0:batches),"accepted matrix upload count changed");
require(processor.instancedVB_.uploadedBytes==(publicFailure?0:expectedBytes),"public upload did not retain exact consumed prefix");
require(errors==(publicFailure?batches:0),"upload failure did not publish its local outcome");
require(processor.instancedVB_.pending==0,"failed upload leaked a lock");
if(publicFailure)require(context.publicBindings==0,"failed public upload was still consumed");
if(publicFailure&&batches){
 // Retry on the same batching owner after a failed upload, without device reset.
 processor.instancedVB_.failLock=false;processor.instancedVB_.nullDestination=false;
 processor.instancedVB_.failUnlock=false;processor.instancedVB_.available=true;context.draws.clear();errors=0;
 if(failure==5){
  processor.instancedVB_.available=false;processor.instancedVB_.failCreate=false;processor.createUnmanagedObjects();
  require(processor.instancedVB_.valid(),"public stream could not be recreated after creation failure");
 }
 processor.processRenderOp(&operations[17]);processor.processRenderOp(&operations[0]);
 processor.flushInstancedRenderOps();
 require(context.draws==std::vector<unsigned>({17,0}),"upload failure poisoned the next batch");
 require(processor.instancedVB_.locks==(failure>=4?1:batches+1)&&processor.instancedVB_.uploads==1&&
         processor.instancedVB_.uploadedBytes==128&&errors==0,"recovered upload retained stale batch state");
}
}
int main(int argc,char** argv){try{
if(argc>1&&std::strcmp(argv[1],"invalid-public")==0){exercise(std::vector<bool>(37,true),4);return 0;}
exercise(std::vector<bool>(37,true),0,argc>1);
exercise({});exercise({false});exercise(std::vector<bool>(9000,true));
for(unsigned failure:{4u,5u}){exercise(std::vector<bool>(37,true),failure);exercise(std::vector<bool>(9000,true),failure);}
exercise(std::vector<bool>(37,true),6);
exercise(std::vector<bool>(4096,false));
std::vector<bool> mixed;for(auto group:{std::pair<unsigned,bool>{17,true},{3,false},{2,true},{5,false}})
 mixed.insert(mixed.end(),group.first,group.second);
exercise(mixed);for(unsigned failure:{1u,2u,3u,4u,5u,6u})exercise(mixed,failure);
std::vector<bool> large;for(unsigned i=0;i<6000;++i)large.push_back((i/13)%2==0);exercise(large);
exercise({false,false,true});
std::puts("PASS pure-stock zero uploads, mixed offsets/order, 4096 rollover, exact used bytes and local failure containment");
return 0;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
'''


class InstancingUploadTests(unittest.TestCase):
    def test_actual_sdk_batches_upload_only_consumed_matrices(self):
        fixtures.StockSpaceModelsTests.setUpClass()
        fixture = fixtures.StockSpaceModelsTests()
        with tempfile.TemporaryDirectory(prefix='instance-consumed-stream-') as temporary:
            root = Path(temporary)
            sources = {}
            for legacy in (True, False):
                fixture.fixture(root)
                original_stage = fixtures.effects.staged_instance_uploads
                try:
                    if legacy:
                        fixtures.effects.staged_instance_uploads = lambda text: text
                    fixtures.effects.patch_instancing(root)
                finally:
                    fixtures.effects.staged_instance_uploads = original_stage
                source = (root / 'lib/moo/draw_context.cpp').read_text()
                if not legacy:
                    fixtures.effects.patch_instancing(root)
                    self.assertEqual((root / 'lib/moo/draw_context.cpp').read_text(), source)
                start = source.index('VertexBuffer\t\t\tinstancedVB_;')
                end = source.index('EffectVisualContext&', start)
                fields = source[start:end]
                methods = '\n'.join(fixtures.definition(source, signature) for signature in (
                    'void processRenderOp(', 'void\tcreateUnmanagedObjects()', 'inline bool notIdenticalRenderOps(',
                    'void openInstancingBuffer(', 'void flushInstancedRenderOps(', 'void flushRop('))
                sources[legacy] = HOST.replace('@FIELDS@', fields).replace('@METHODS@', methods)
            for legacy, host in sources.items():
                name = 'old-unused-upload' if legacy else 'after'
                source, binary = root / (name + '.cpp'), root / name
                source.write_text(host)
                built = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                        '-UNDEBUG', str(source), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary)] + (['old-control'] if legacy else []), capture_output=True, text=True)
                if legacy:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('stock-only batch uploaded an unused public matrix stream', result.stderr)
                    guarded = subprocess.run([str(binary), 'invalid-public'], capture_output=True, text=True)
                    self.assertNotEqual(guarded.returncode, 0)
                    self.assertIn('instancedVB_.valid()', guarded.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('exact used bytes and local failure containment', result.stdout)


if __name__ == '__main__':
    unittest.main()
