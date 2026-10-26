"""Extract production FileEmitter birth with isolated resource and SDK boundaries.

Define WOT_STOCK_TPS_BIRTH_ENGINE_MATH on Windows to use the actual public
BigWorld Matrix/Vector3 and D3DX library instead of the host math adapter.
"""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'bigworld/stock_tps_scene.cpp').read_text()
helpers=s[s.index('using TPSVector'):s.index('struct FxParticle')]
types=s[s.index('struct BirthContext'):s.index('Matrix buildVisualBasis(')]
method=s[s.index('void SceneSource::emitVisual()'):s.index('std::shared_ptr<const wot::StockTPSResource> Runtime::preload')]
pre=r'''
#include "stock_tps_model.hpp"
#include "stock_tps_random.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <fstream>
#include <limits>
#include <memory>
#ifdef _WIN32
#include <cstring>
#else
#include <strings.h>
#endif
#include <vector>
#ifdef WOT_STOCK_TPS_BIRTH_ENGINE_MATH
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4458)
#endif
#include "math/matrix.hpp"
#include "math/vector3.hpp"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
using BW::Vector3;using BW::Matrix;
#else
namespace BW { using string=std::string; }
#endif
#ifdef ERROR_MSG
#undef ERROR_MSG
#endif
#define ERROR_MSG(...) ((void)0)
#ifndef _WIN32
#define _stricmp strcasecmp
#endif
#ifndef WOT_STOCK_TPS_BIRTH_ENGINE_MATH
struct Vector3 {
 float x,y,z;Vector3(float a,float b,float c):x(a),y(b),z(c){}
 void normalise(){const float l=std::sqrt(x*x+y*y+z*z);x/=l;y/=l;z/=l;}
};
struct Matrix {
 float v[16]{};static const Matrix identity;
 void translation(const Vector3 &p){v[12]=p.x;v[13]=p.y;v[14]=p.z;}
 Vector3 applyPoint(const Vector3&p)const{return Vector3(p.x*v[0]+p.y*v[4]+p.z*v[8]+v[12],p.x*v[1]+p.y*v[5]+p.z*v[9]+v[13],p.x*v[2]+p.y*v[6]+p.z*v[10]+v[14]);}
 Vector3 applyVector(const Vector3&p)const{return Vector3(p.x*v[0]+p.y*v[4]+p.z*v[8],p.x*v[1]+p.y*v[5]+p.z*v[9],p.x*v[2]+p.y*v[6]+p.z*v[10]);}
};
const Matrix Matrix::identity=[](){Matrix v;v.v[0]=v.v[5]=v.v[10]=v.v[15]=1.f;return v;}();
using D3DXMATRIX=Matrix;
void D3DXMatrixScaling(Matrix *m,float x,float y,float z){*m=Matrix{};m->v[0]=x;m->v[5]=y;m->v[10]=z;m->v[15]=1.f;}
#endif
'''
post=r'''
struct Runtime;
struct SceneEffect {SceneEffect(Runtime&,std::shared_ptr<const wot::StockTPSResource>,const Matrix&){}void setImplicitPose(const TPSVector&,const TPSVector&){} };
struct Runtime {
 wot::StockTPSRandom random{[](){return 0;}};
 unsigned modelCount=0;
 std::vector<std::shared_ptr<SceneEffect>> pendingEffects;
 std::shared_ptr<const wot::StockTPSResource> preload(const BW::string&,std::string&){return nullptr;}
};
struct VisualResource {bool load(std::string&){return true;}};
struct SceneSource {
 Runtime& runtime;const wot::StockTPSEmitterResource&resource;
 BirthContext transform;
 std::vector<VisualParticle>models;
 std::shared_ptr<VisualResource> visualResource=std::make_shared<VisualResource>();
 SceneSource(Runtime&r,const wot::StockTPSEmitterResource&s):runtime(r),resource(s){prepareBirthContext(transform,s);}
 void emitVisual();
};
'''
main=r'''
std::uint32_t bits(float f){std::uint32_t v;std::memcpy(&v,&f,4);return v;}
int main(int argc,char **argv){if(argc!=2)return 2;std::ifstream in(argv[1],std::ios::binary);Runtime r;
 in.read((char*)r.random.values.data(),sizeof(r.random.values));in.read((char*)r.random.circle.data(),sizeof(r.random.circle));
 std::uint32_t cursor,explicitWorld;
 while(in.read((char*)&cursor,4)){
  wot::StockTPSEmitterResource resource;in.read((char*)resource.descriptor.data(),resource.descriptor.size());
  SceneSource source(r,resource);in.read((char*)&source.transform.position,sizeof(TPSVector));in.read((char*)&source.transform.direction,sizeof(TPSVector));in.read((char*)&explicitWorld,4);
  source.transform.explicitTransform=explicitWorld!=0;r.random.cursor=cursor;r.random.faceAxis=0;r.random.faceSign=-1;r.modelCount=0;source.emitVisual();
  std::cout<<source.models.size()<<' ';
  for(const auto&m:source.models){const auto&p=m.state.movement;std::cout<<p.flags<<' '<<m.state.rotationFlags<<' ';
   for(float v:{m.state.angles.x,m.state.angles.y,m.state.angles.z,m.state.angularVelocity.x,m.state.angularVelocity.y,m.state.angularVelocity.z,
    p.windResponse,p.lifetime,p.position.x,p.position.y,p.position.z,p.gravity,
    p.additionalVelocity.x,p.additionalVelocity.y,p.additionalVelocity.z,(p.flags&0x200000)?p.windMultiplier:0.f,
    p.velocity.x,p.velocity.y,p.velocity.z,m.size,p.delay})std::cout<<bits(v)<<' ';
  }
  std::cout<<r.random.cursor<<' '<<r.random.faceAxis<<' '<<r.random.faceSign<<'\n';
 }
}
'''
def make_probe():
    return pre+helpers+types+post+method+main


if __name__ == '__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    args.output.write_text(make_probe())
