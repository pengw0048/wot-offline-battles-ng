"""Exercise production Math provider methods and their original x86 fixtures."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('WGYawOnlyTransform', 'WGInverseTransform', 'WGBoundMatrixProvider',
         'WGCombinedMP', 'WGTranslationOnlyMP', 'WGStrategicAreaViewMP',
         'WGSmoothTranslationOnlyMP', 'WGReplayAwaredSmoothTranslationOnlyMP')


def block(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    while depth:
        end += 1
        depth += (source[end] == '{') - (source[end] == '}')
    return source[start:end + 1]


def production_probe():
    source = (ROOT / 'bigworld/stock_math_providers.cpp').read_text()
    declarations = [block(source, 'Vector3 stockSmoothTranslation(')]
    declarations += [block(source, 'class ' + name + ' :') + ';' for name in NAMES]
    return r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <type_traits>
#include <vector>
using PyTypeObject=int;
#define Py_Header(...) inline static PyTypeObject s_type_=0;
#define PY_RW_ATTRIBUTE_DECLARE(member,name) auto name()const{return member;} template<class T>void name(T value){member=value;}
#define PY_AUTO_METHOD_DECLARE(...)
#define PY_RW_ACCESSOR_ATTRIBUTE_DECLARE(...)
#define PY_AUTO_FACTORY_DECLARE(...)
struct Vector2 {float x,y;Vector2(float a,float b):x(a),y(b){}};
struct Vector3 {
 float x=0,y=0,z=0;
 Vector3(){};Vector3(float a,float b,float c):x(a),y(b),z(c){}
 void set(float a,float b,float c){x=a;y=b;z=c;}
 float& operator[](unsigned index){return index==0?x:index==1?y:z;}
 float operator[](unsigned index)const{return index==0?x:index==1?y:z;}
};
struct Matrix {
 float values[16]{};static const Matrix identity;
 float& operator()(unsigned row,unsigned column){return values[row*4+column];}
 float operator()(unsigned row,unsigned column)const{return values[row*4+column];}
 void setZero(){std::memset(values,0,sizeof(values));}
};
const Matrix Matrix::identity=[](){Matrix m;for(int n=0;n!=4;++n)m(n,n)=1;return m;}();
struct MatrixProvider {
 bool autoTick;
 explicit MatrixProvider(bool tick=false,PyTypeObject* =nullptr):autoTick(tick){}
 virtual ~MatrixProvider(){};
 virtual void matrix(Matrix&)const=0;
 virtual void tick(float){}
};
using MatrixProviderPtr=std::shared_ptr<MatrixProvider>;
struct Camera {float aspect=2;float aspectRatio()const{return aspect;}};
namespace Moo {
struct Context {Camera value;Camera& camera(){return value;}};
Context& rc(){static Context context;return context;}
}
bool paused=false;bool stockReplayPlaybackPaused(){return paused;}
std::vector<unsigned> samples;
struct Source:MatrixProvider {
 Matrix value;unsigned id;mutable unsigned calls=0;mutable std::function<void()> callback;
 explicit Source(unsigned index):id(index){}
 void matrix(Matrix&out)const override{
  ++calls;samples.push_back(id);
  auto invoke=callback;if(invoke)invoke();out=value;
 }
};
std::vector<float> boundary;Matrix multiplyLeft,multiplyRight;
bool inverseAlias=false,inverseNull=false,normaliseAlias=false,multiplyAlias=false;Vector3 normaliseInput;
Matrix* D3DXMatrixTranslation(Matrix*out,float x,float y,float z){
 boundary={x,y,z};*out=Matrix::identity;(*out)(3,0)=x;(*out)(3,1)=y;(*out)(3,2)=z;return out;
}
Matrix* D3DXMatrixRotationY(Matrix*out,float yaw){boundary={yaw};*out=Matrix::identity;return out;}
Vector3* D3DXVec3Normalize(Vector3*out,const Vector3*input){normaliseAlias=out==input;normaliseInput=*input;*out=Vector3(0,0,1);return out;}
Matrix* D3DXMatrixInverse(Matrix*out,float*det,const Matrix*input){inverseAlias=out==input;inverseNull=!det;return out;}
Matrix* D3DXMatrixScaling(Matrix*out,float x,float y,float z){
 boundary={x,y,z};for(unsigned n=0;n!=16;++n)out->values[n]=100.f+n;return out;
}
Matrix* D3DXMatrixMultiply(Matrix*out,const Matrix*left,const Matrix*right){
 multiplyAlias=out==right;multiplyLeft=*left;multiplyRight=*right;*out=*right;return out;
}
@PRODUCTION@
float floating(uint32_t word){float value;std::memcpy(&value,&word,4);return value;}
uint32_t word(float value){uint32_t result;std::memcpy(&result,&value,4);return result;}
void matrixWords(Matrix&matrix,const std::vector<uint32_t>&words,size_t offset){
 for(size_t n=0;n!=16;++n)matrix.values[n]=floating(words[offset+n]);
}
void fixtures(){
 unsigned kind,flags;
 while(std::cin>>kind>>flags){
  unsigned count=kind==0?22:kind==1?32:kind<4?8:kind==6?20:16;
  std::vector<uint32_t> words(count);for(auto&value:words)std::cin>>value;
  assert(std::cin);auto a=std::make_shared<Source>(0),b=std::make_shared<Source>(1);
  Matrix result;
  if(kind==0){
   WGBoundMatrixProvider provider;matrixWords(a->value,words,0);
   provider.minBound(Vector3(floating(words[16]),floating(words[17]),floating(words[18])));
   provider.maxBound(Vector3(floating(words[19]),floating(words[20]),floating(words[21])));
   if(flags&1){provider.source(a);}
   samples.clear();provider.matrix(result);
  }else if(kind==1){
   WGCombinedMP provider;matrixWords(a->value,words,0);matrixWords(b->value,words,16);
   if(flags&1){provider.rotationSrc(a);}
   if(flags&2){provider.translationSrc(b);}
   samples.clear();provider.matrix(result);
  }else if(kind<4){
   std::unique_ptr<WGSmoothTranslationOnlyMP> provider(kind==2?
    new WGSmoothTranslationOnlyMP:new WGReplayAwaredSmoothTranslationOnlyMP);
   a->value(3,0)=floating(words[0]);a->value(3,1)=floating(words[1]);a->value(3,2)=floating(words[2]);
   provider->source(a);if(!(flags&1))provider->source(MatrixProviderPtr());
   a->value(3,0)=floating(words[3]);a->value(3,1)=floating(words[4]);a->value(3,2)=floating(words[5]);
   paused=(flags&2)&&floating(words[6])==0.f;
   samples.clear();provider->tick(floating(words[7]));provider->matrix(result);
  }else{
   matrixWords(a->value,words,0);samples.clear();
   if(kind==4){WGTranslationOnlyMP provider;if(flags&1)provider.source(a);provider.matrix(result);}
   if(kind==5){WGInverseTransform provider;if(flags&1)provider.source(a);provider.matrix(result);}
   if(kind==6){WGStrategicAreaViewMP provider;if(flags&1)provider.source(a);
    provider.baseScale(Vector2(floating(words[16]),floating(words[17])));
    provider.setBaseHeight(floating(words[18]));Moo::rc().camera().aspect=floating(words[19]);provider.matrix(result);}
   if(kind==7){WGYawOnlyTransform provider;if(flags&1)provider.source(a);provider.matrix(result);}
   std::cout<<(flags&1)<<' ';
   if(flags&1){
    if(kind==4||kind==6)for(float value:boundary)std::cout<<word(value)<<' ';
    if(kind==5){for(float value:a->value.values)std::cout<<word(value)<<' ';
     std::cout<<inverseAlias<<' '<<inverseNull<<' ';}
    if(kind==6){for(float value:multiplyLeft.values)std::cout<<word(value)<<' ';
     for(float value:multiplyRight.values){std::cout<<word(value)<<' ';}
     std::cout<<multiplyAlias<<' ';}
    if(kind==7)std::cout<<word(normaliseInput.x)<<' '<<word(normaliseInput.y)<<' '<<word(normaliseInput.z)<<' '<<normaliseAlias<<' ';
   }else for(float value:result.values)std::cout<<word(value)<<' ';
   std::cout<<samples.size();for(unsigned value:samples)std::cout<<' '<<value;std::cout<<'\n';continue;
  }
  unsigned first=kind<2?0:12,last=kind<2?16:15;
  for(unsigned n=first;n!=last;++n)std::cout<<word(result.values[n])<<' ';
  std::cout<<samples.size();for(unsigned value:samples)std::cout<<' '<<value;std::cout<<'\n';
 }
}
void contracts(){
 Matrix result;auto input=std::make_shared<Source>(0);
 for(unsigned n=0;n!=16;++n)input->value.values[n]=float(n+1);
 const Matrix original=input->value;
 @FACTORIES@
 WGCombinedMP combined;combined.rotationSrc(input);samples.clear();combined.matrix(result);
 assert(samples.empty()&&!std::memcmp(&result,&Matrix::identity,sizeof(result)));
 auto translation=std::make_shared<Source>(1);translation->value(3,0)=90;
 combined.translationSrc(translation);combined.matrix(result);
 assert((samples==std::vector<unsigned>{0,1})&&result(3,0)==90&&result(3,3)==1);
 assert(!std::memcmp(&input->value,&original,sizeof(original)));
 WGInverseTransform inverse;inverse.source(input);inverse.matrix(result);assert(inverseAlias);
 WGTranslationOnlyMP translationOnly;translationOnly.source(input);translationOnly.matrix(result);
 assert((boundary==std::vector<float>{13,14,15}));
 WGYawOnlyTransform yaw;yaw.source(input);yaw.matrix(result);
 assert(normaliseInput.x==9&&normaliseInput.y==10&&normaliseInput.z==11&&boundary[0]==0);
 WGStrategicAreaViewMP strategic;strategic.source(input);strategic.baseScale(Vector2(3,4));
 strategic.setBaseHeight(7);strategic.matrix(result);
 assert(boundary[0]==6&&boundary[1]==1&&boundary[2]==4);
 assert(!std::memcmp(&multiplyRight,&original,sizeof(original))&&multiplyLeft.values[0]==100&&multiplyAlias);
 assert(!std::memcmp(&input->value,&original,sizeof(original)));
 WGSmoothTranslationOnlyMP smooth;smooth.source(input);assert(input->calls>0);
 smooth.matrix(result);assert(result(3,0)==13&&result(3,1)==14&&result(3,2)==15);
 smooth.source(MatrixProviderPtr());input->value(3,0)=900;smooth.tick(100);
 smooth.matrix(result);assert(result(3,0)==13);
 paused=true;WGReplayAwaredSmoothTranslationOnlyMP replay;replay.source(input);
 input->value(3,0)=1000;samples.clear();replay.tick(1);assert(samples.empty());
 replay.matrix(result);assert(result(3,0)==900);paused=false;replay.tick(0);
 replay.matrix(result);assert(result(3,0)>900&&result(3,0)<1000);
 // Replacing a source during its sample must keep the executing provider
 // alive until matrix() returns, even when it held the final external ref.
 auto reentrant=std::make_shared<Source>(2);std::weak_ptr<Source> weak=reentrant;
 WGBoundMatrixProvider bound;bound.source(reentrant);
 reentrant->callback=[&]{bound.source(MatrixProviderPtr());assert(!weak.expired());};
 reentrant.reset();bound.matrix(result);assert(weak.expired());
 // Combined keeps both sampled identities alive before invoking the first
 // source. A first-source callback may release the second public binding.
 auto rotate=std::make_shared<Source>(3),translate=std::make_shared<Source>(4);
 std::weak_ptr<Source> translateLifetime=translate;
 WGCombinedMP swapped;swapped.rotationSrc(rotate);swapped.translationSrc(translate);
 translate->value(3,0)=123;translate.reset();
 rotate->callback=[&]{swapped.translationSrc(MatrixProviderPtr());assert(!translateLifetime.expired());};
 samples.clear();swapped.matrix(result);
 assert(result(3,0)==123&&(samples==std::vector<unsigned>{3,4})&&translateLifetime.expired());
 // A nested setter publishes a newer identity/current value. The outer
 // sample must never overwrite it when that sample subsequently returns.
 WGSmoothTranslationOnlyMP changed;
 auto removed=std::make_shared<Source>(5);removed->value(3,0)=50;
 removed->callback=[&]{changed.source(MatrixProviderPtr());};
 changed.source(removed);changed.matrix(result);assert(result(3,0)==0&&!changed.source());
 auto replacement=std::make_shared<Source>(6);replacement->value(3,0)=80;
 auto outer=std::make_shared<Source>(7);outer->value(3,0)=90;
 outer->callback=[&]{changed.source(replacement);};
 changed.source(outer);changed.matrix(result);
 assert(changed.source()==replacement&&result(3,0)==80);
 outer->callback={};changed.source(outer);outer->value(3,0)=100;
 outer->callback=[&]{changed.source(replacement);};
 changed.tick(.1f);changed.matrix(result);
 assert(changed.source()==replacement&&result(3,0)==80);
 replacement->callback=[&]{changed.source(MatrixProviderPtr());};
 changed.tick(.1f);changed.matrix(result);assert(!changed.source()&&result(3,0)==80);
}
int main(int argc,char**){if(argc>1)fixtures();else contracts();}
'''.replace('@PRODUCTION@', '\n'.join(declarations)).replace('@FACTORIES@', '\n'.join(
        '{std::unique_ptr<MatrixProvider> value(' + name + '::New());'
        'static_assert(!std::is_invocable_v<decltype(&' + name + '::New),int>);'
        'assert(value->autoTick==' + ('true' if 'Smooth' in name else 'false') + ');'
        'value->matrix(result);}' for name in NAMES))


def compile_probe(directory, sanitise=True):
    if not shutil.which('c++'):
        raise unittest.SkipTest('C++ compiler unavailable: c++')
    source = directory / 'probe.cpp'
    source.write_text(production_probe())
    command = ['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
               '-ffp-contract=off', '-fno-fast-math', '-UNDEBUG']
    if sanitise and sys.platform != 'win32':
        command += ['-fsanitize=address,undefined']
    target = directory / 'probe'
    subprocess.run(command + [str(source), '-o', str(target)], check=True)
    return target


class MathProviderTests(unittest.TestCase):
    def test_production_factories_sampling_and_reentrant_source_ownership(self):
        with tempfile.TemporaryDirectory(prefix='math-provider-owner-') as temporary:
            probe = compile_probe(Path(temporary))
            subprocess.run([str(probe)], check=True)

    def test_original_instruction_fixture(self):
        directory = os.environ.get('WOT_MATH_PROVIDER_FIXTURES')
        if not directory:
            self.skipTest('Set WOT_MATH_PROVIDER_FIXTURES to original x86 oracle output')
        inputs = (Path(directory) / 'math-provider-inputs.txt').read_text()
        expected = (Path(directory) / 'math-provider-expected.txt').read_text().splitlines()
        with tempfile.TemporaryDirectory(prefix='math-provider-instructions-') as temporary:
            probe = compile_probe(Path(temporary))
            actual = subprocess.run([str(probe), '--fixtures'], input=inputs,
                                    text=True, capture_output=True, check=True).stdout.splitlines()
        self.assertEqual(len(actual), len(expected))
        for index, (observed, original) in enumerate(zip(actual, expected)):
            self.assertEqual(observed.split(), original.split(), 'Original x86 case %d' % index)


if __name__ == '__main__':
    unittest.main()
