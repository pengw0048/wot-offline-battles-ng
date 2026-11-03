"""Execute SDK batching with native/public consumers and count matrix uploads."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_stock_space_models as fixtures
from native_test_compile_flags import native_test_compile_flags


HOST = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
using uint32=std::uint32_t;using HRESULT=int;using D3DPRIMITIVETYPE=unsigned;
const HRESULT E_FAIL=-1;
const unsigned D3DUSAGE_WRITEONLY=1,D3DUSAGE_DYNAMIC=2,D3DPOOL_DEFAULT=0;
const unsigned D3DLOCK_DISCARD=1,D3DSTREAMSOURCE_INDEXEDDATA=0x40000000u,D3DSTREAMSOURCE_INSTANCEDATA=0x80000000u;
const unsigned NON_INSTANCED_ROP=0xffffffffu;
#define SUCCEEDED(value) ((value)>=0)
#define FAILED(value) ((value)<0)
#define BW_GUARD
#define MF_ASSERT(value) assert(value)
#define ERROR_MSG(...) (++errors)
unsigned errors=0;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
namespace BW{template<class T>using vector=std::vector<T>;}
struct Matrix{float data[16];};static_assert(sizeof(Matrix)==64,"native matrix width");
struct Effect{bool stock;Effect* pEffect(){return this;}void setAutoConstants(){}void commitChanges(){}};
namespace wot{bool is_stock_effect(Effect* effect){return effect->stock;}}
class StockInstanceIndexStream;
struct Visual{const Matrix* matrices=nullptr;unsigned count=0;
std::unique_ptr<StockInstanceIndexStream> stockInstanceIndexStream_;
Visual();~Visual();bool stockBindInstanceIndexStream();
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
 require(bytes&&bytes->size()==16*sizeof(float),"shared native index stream width changed");
 for(unsigned i=0;i<16;++i){float index;memcpy(&index,bytes->data()+i*sizeof(float),sizeof(float));
  require(index==float(i),"shared native index contents changed");}
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
unsigned indexCreates=0,indexLocks=0,indexUploads=0,indexReleases=0,publicCreates=0,publicAllocatedBytes=0,indexFailure=0;
struct VertexBuffer{
unsigned id=1,locks=0,unlocks=0,uploads=0,uploadedBytes=0,pending=0;
bool available=false,failCreate=false,failLock=false,failUnlock=false,nullDestination=false;std::vector<unsigned char> bytes;
bool valid()const{return available;}
HRESULT create(unsigned size,unsigned,unsigned,unsigned pool,const char* label){
 require(pool==D3DPOOL_DEFAULT,"device resource pool changed");
 if(std::strstr(label,"original instance indices")){id=2;++indexCreates;available=indexFailure!=1;}
 else{++publicCreates;publicAllocatedBytes+=size;available=!failCreate;}
 return available?0:E_FAIL;
}
void release(){if(id==2&&available)++indexReleases;available=false;}
HRESULT lock(unsigned offset,unsigned size,void** output,unsigned){
 if(id==2)++indexLocks;
 ++locks;require(offset==0&&!pending,"invalid matrix lock");*output=nullptr;
 if(!available||failLock||(id==2&&indexFailure==2))return E_FAIL;
 pending=size;bytes.resize(size);if(!nullDestination&&!(id==2&&indexFailure==3))*output=bytes.data();return 0;
}
HRESULT unlock(){
 ++unlocks;require(pending!=0,"unlock without successful lock");
 const bool failed=failUnlock||(id==2&&indexFailure==4);
 if(!failed&&!nullDestination&&!(id==2&&indexFailure==3)){++uploads;uploadedBytes+=pending;if(id==2)++indexUploads;}
 pending=0;return failed?-1:0;
}
void set(unsigned,unsigned offset,unsigned stride){
 require(available,"invalid instance buffer was bound");
 context.bound=id;context.offset=offset;context.stride=stride;context.bytes=&bytes;
 if(id==1)++context.publicBindings;
}
static void reset(unsigned){context.bound=0;}
};
struct InstancingStream{enum{STREAM_NUMBER=9,ELEMENT_SIZE=64};};
struct DeviceCallback{
 static std::vector<DeviceCallback*> callbacks;
 DeviceCallback(){callbacks.push_back(this);}
 virtual ~DeviceCallback(){callbacks.erase(std::find(callbacks.begin(),callbacks.end(),this));}
 virtual void createUnmanagedObjects(){}virtual void deleteUnmanagedObjects(){}
 static void lose(){for(auto* callback:callbacks)callback->deleteUnmanagedObjects();}
 static void restore(){for(auto* callback:callbacks)callback->createUnmanagedObjects();}
};
std::vector<DeviceCallback*> DeviceCallback::callbacks;
@INDEX_OWNER@
Visual::Visual()=default;Visual::~Visual()=default;
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
explicit Processor(Visual& visual):effectVisualContext_(visual){context.visual=&visual;}
void nullifyActiveState(){}void closeActiveEffect(){}
void applyInstanceData(DrawContext::InstanceData*){}
void processRop(const RenderOp* rop,unsigned count,unsigned offset){context.effect=rop->effect_;flushRop(rop,count,offset);}
@METHODS@
};

void exercise(const std::vector<bool>& native,unsigned failure=0,bool oldControl=false){
context=Context();errors=0;Visual visual;Processor processor(visual);
indexFailure=failure==6?1:0;
const bool publicFailure=failure>=1&&failure<=5;
processor.instancedVB_.failLock=failure==1;processor.instancedVB_.nullDestination=failure==2;
processor.instancedVB_.failUnlock=failure==3;
processor.instancedVB_.available=oldControl;
processor.instancedVB_.failCreate=failure==4||failure==5;
if(oldControl)processor.createUnmanagedObjects();
if(failure==5){
 processor.createUnmanagedObjects();
 require(!processor.instancedVB_.valid()&&errors==0,
         "processor startup allocated an unused public stream");
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
 if(failure>=4){
  processor.instancedVB_.available=false;processor.instancedVB_.failCreate=false;processor.createUnmanagedObjects();
  require(!processor.instancedVB_.valid(),"public stream was allocated before its recovered consumer");
 }
 processor.processRenderOp(&operations[17]);processor.processRenderOp(&operations[0]);
 processor.flushInstancedRenderOps();
 require(context.draws==std::vector<unsigned>({17,0}),"upload failure poisoned the next batch");
 require(processor.instancedVB_.locks==(failure>=4?1:batches+1)&&processor.instancedVB_.uploads==1&&
         processor.instancedVB_.uploadedBytes==128&&errors==0,"recovered upload retained stale batch state");
}
}
void sharedOwnerLifecycle(){
context=Context();errors=0;indexFailure=0;
indexCreates=indexLocks=indexUploads=indexReleases=publicCreates=publicAllocatedBytes=0;
{
 Visual visual;Effect effect{true};DrawContext::InstanceData packet;
 for(unsigned i=0;i<16;++i)packet.value.data[i]=float(7+i*10000);
 RenderOp rop;rop.effect_=&effect;rop.instanceData_=&packet;
 const std::vector<unsigned char>* first=nullptr;
 for(unsigned i=0;i<300;++i){
  Processor processor(visual);processor.createUnmanagedObjects();
  processor.processRenderOp(&rop);processor.flushInstancedRenderOps();
  if(!first)first=context.bytes;
  require(context.bytes==first,"ephemeral processor replaced the shared index stream");
  require(!processor.instancedVB_.valid(),"native-only processor allocated public matrices");
 }
 require(indexCreates==1&&indexLocks==1&&indexUploads==1&&publicCreates==0&&publicAllocatedBytes==0,
         "ephemeral DrawContexts repeated immutable upload or unused public allocation");
 require(DeviceCallback::callbacks.size()==1,"shared stream registered per processor");
 DeviceCallback::lose();DeviceCallback::lose();
 require(indexReleases==1,"repeated device loss released a shared resource twice");
 DeviceCallback::restore();
 require(indexCreates==2&&indexLocks==2&&indexUploads==2,"device restore did not upload indices once");
 for(unsigned failure:{1u,2u,3u,4u}){
  DeviceCallback::lose();indexFailure=failure;const auto uploaded=indexUploads;
  DeviceCallback::restore();
  require(!visual.stockBindInstanceIndexStream()&&indexUploads==uploaded,
          "failed shared index initialization was consumed");
  indexFailure=0;
  require(visual.stockBindInstanceIndexStream()&&indexUploads==uploaded+1,
          "shared index stream failed to recover on its next consumer");
  require(visual.stockBindInstanceIndexStream()&&indexUploads==uploaded+1,
          "recovered shared indices were uploaded again");
 }
 // A genuinely consumed public stream retains its own reset and lazy recovery.
 Processor publicProcessor(visual);Effect legacy{false};rop.effect_=&legacy;
 publicProcessor.createUnmanagedObjects();require(publicCreates==0,"public stream created before use");
 publicProcessor.processRenderOp(&rop);publicProcessor.flushInstancedRenderOps();
 require(publicCreates==1&&publicAllocatedBytes==4096*64,"public consumer missed its exact allocation");
 publicProcessor.deleteUnmanagedObjects();publicProcessor.createUnmanagedObjects();
 require(publicCreates==1,"public device callback eagerly recreated unused matrices");
 publicProcessor.processRenderOp(&rop);publicProcessor.flushInstancedRenderOps();
 require(publicCreates==2&&publicAllocatedBytes==2*4096*64,"public consumer did not recover after loss");
}
require(DeviceCallback::callbacks.empty(),"visual context teardown retained a device callback");
}
int main(int argc,char** argv){try{
if(argc>1&&std::strcmp(argv[1],"shared-lifecycle")==0){sharedOwnerLifecycle();return 0;}
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
sharedOwnerLifecycle();
std::puts("PASS pure-stock zero uploads, mixed offsets/order, 4096 rollover, exact used bytes and local failure containment");
std::puts("PASS 300 ephemeral processors share one immutable upload; lazy public allocations, reset, all initialization failures and recovery");
return 0;}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}}
'''


class InstancingUploadTests(unittest.TestCase):
    def test_cached_per_processor_streams_upgrade_idempotently(self):
        fixtures.StockSpaceModelsTests.setUpClass()
        fixture = fixtures.StockSpaceModelsTests()
        with tempfile.TemporaryDirectory(prefix='instance-stream-owner-upgrade-') as temporary:
            root = Path(temporary)
            fixture.fixture(root)
            old_streams = fixtures.effects.shared_instance_streams
            old_owner = fixtures.effects.patch_shared_instance_index_owner
            try:
                fixtures.effects.shared_instance_streams = lambda text: text
                fixtures.effects.patch_shared_instance_index_owner = lambda root: None
                fixtures.effects.patch_instancing(root)
            finally:
                fixtures.effects.shared_instance_streams = old_streams
                fixtures.effects.patch_shared_instance_index_owner = old_owner
            old_header = (root / 'lib/moo/effect_visual_context.hpp').read_text()
            self.assertIn('VertexBuffer stockInstanceIndexVB_;', (root / 'lib/moo/draw_context.cpp').read_text())
            fixtures.effects.patch_instancing(root)
            new_header = (root / 'lib/moo/effect_visual_context.hpp').read_text()
            appended = ('\n\t// Append device ownership without changing existing inline field offsets.\n'
                        '\tstd::unique_ptr<StockInstanceIndexStream> stockInstanceIndexStream_;')
            # Existing SDK libraries contain inline EVC field offsets. Every
            # prior data declaration stays identical; only the tail grows.
            self.assertEqual(new_header.count(appended), 1)
            self.assertGreater(new_header.index(appended), new_header.index('currentObjectID_;'))
            self.assertEqual(old_header.partition('\nprivate:\n')[2],
                             new_header.partition('\nprivate:\n')[2].replace(appended, ''))
            upgraded = {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()}
            fixtures.effects.patch_instancing(root)
            self.assertEqual(upgraded, {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()})
            fixture.fixture(root)
            fixtures.effects.patch_instancing(root)
            self.assertEqual(upgraded, {path.relative_to(root): path.read_bytes() for path in root.rglob('*') if path.is_file()})

    def test_actual_sdk_batches_upload_only_consumed_matrices(self):
        fixtures.StockSpaceModelsTests.setUpClass()
        fixture = fixtures.StockSpaceModelsTests()
        with tempfile.TemporaryDirectory(prefix='instance-consumed-stream-') as temporary:
            root = Path(temporary)
            sources = {}
            for legacy in (True, False):
                fixture.fixture(root)
                original_stage = fixtures.effects.staged_instance_uploads
                original_streams = fixtures.effects.shared_instance_streams
                try:
                    if legacy:
                        fixtures.effects.staged_instance_uploads = lambda text: text
                        fixtures.effects.shared_instance_streams = lambda text: text
                    fixtures.effects.patch_instancing(root)
                finally:
                    fixtures.effects.staged_instance_uploads = original_stage
                    fixtures.effects.shared_instance_streams = original_streams
                source = (root / 'lib/moo/draw_context.cpp').read_text()
                if not legacy:
                    fixtures.effects.patch_instancing(root)
                    self.assertEqual((root / 'lib/moo/draw_context.cpp').read_text(), source)
                start = source.index('VertexBuffer\t\t\tinstancedVB_;')
                end = source.index('EffectVisualContext&', start)
                fields = source[start:end]
                signatures = [
                    'void processRenderOp(', 'void\tcreateUnmanagedObjects()', 'inline bool notIdenticalRenderOps(',
                    'void openInstancingBuffer(', 'void flushInstancedRenderOps(', 'void flushRop(',
                    'void\tdeleteUnmanagedObjects()']
                if 'bool ensurePublicInstancingBuffer()' in source:
                    signatures.append('bool ensurePublicInstancingBuffer()')
                methods = '\n'.join(fixtures.definition(source, signature) for signature in signatures)
                visual = (root / 'lib/moo/effect_visual_context.cpp').read_text()
                owner = fixtures.definition(visual, 'class StockInstanceIndexStream : public DeviceCallback') + ';\n'
                owner += fixtures.definition(visual, 'bool EffectVisualContext::stockBindInstanceIndexStream()').replace(
                    'EffectVisualContext::', 'Visual::')
                sources[legacy] = HOST.replace('@FIELDS@', fields).replace('@METHODS@', methods).replace('@INDEX_OWNER@', owner)
            for legacy, host in sources.items():
                name = 'old-unused-upload' if legacy else 'after'
                source, binary = root / (name + '.cpp'), root / name
                source.write_text(host)
                built = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                        '-UNDEBUG', *native_test_compile_flags(), str(source), '-o', str(binary)], capture_output=True, text=True)
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
                    self.assertIn('300 ephemeral processors share one immutable upload', result.stdout)
            current = sources[False]
            for name, host, reason in (
                ('per-bind-indices', current.replace('if (!stockInstanceIndexStream_)', 'if (true)'),
                 'ephemeral processor replaced the shared index stream'),
                ('eager-public', current.replace(
                    '// CPU staging and the shared native index stream need no local GPU buffer.',
                    '(void)ensurePublicInstancingBuffer();'),
                 'native-only processor allocated public matrices')):
                source, binary = root / (name + '.cpp'), root / name
                source.write_text(host)
                built = subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                                        '-UNDEBUG', *native_test_compile_flags(), str(source), '-o', str(binary)], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                result = subprocess.run([str(binary), 'shared-lifecycle'], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(reason, result.stderr)


if __name__ == '__main__':
    unittest.main()
