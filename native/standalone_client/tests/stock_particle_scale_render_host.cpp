// Actual patched owner/draw-item/job bodies; D3DX, GPU submission, texture,
// camera, fog and particle storage are explicit primitive boundaries.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>
#include <cstdio>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define BW_GUARD
#define BW_GUARD_PROFILER(...)
#define PROFILER_SCOPED_DYNAMIC_STRING(...)
#define TRANSFORM_ASSERT(...)
using uint=unsigned;
using DWORD=unsigned;
const float MATH_PI=3.14159265358979323846f,MATH_2PI=2*MATH_PI;
struct Vector3 {
    float x=0,y=0,z=0;
    Vector3()=default;Vector3(float a,float b,float c):x(a),y(b),z(c){}
    bool operator!=(const Vector3& o)const{return x!=o.x||y!=o.y||z!=o.z;}
    Vector3 operator+(const Vector3& o)const{return {x+o.x,y+o.y,z+o.z};}
    Vector3 operator-(const Vector3& o)const{return {x-o.x,y-o.y,z-o.z};}
    Vector3 operator*(float s)const{return {x*s,y*s,z*s};}
    Vector3& operator+=(const Vector3& o){x+=o.x;y+=o.y;z+=o.z;return *this;}
};
struct Row:Vector3 {float w=0;};
struct Matrix {
    Row rows[4];
    static Matrix identity;
    Matrix(){rows[0].x=rows[1].y=rows[2].z=rows[3].w=1;}
    Row& operator[](unsigned i){return rows[i];}
    const Row& operator[](unsigned i)const{return rows[i];}
    Vector3 applyVector(const Vector3& p)const{
        return {p.x*rows[0].x+p.y*rows[1].x+p.z*rows[2].x,
                p.x*rows[0].y+p.y*rows[1].y+p.z*rows[2].y,
                p.x*rows[0].z+p.y*rows[1].z+p.z*rows[2].z};
    }
    Vector3 applyPoint(const Vector3& p)const{return applyVector(p)+rows[3];}
    void applyPoint(Vector3& output,const Vector3& p)const{output=applyPoint(p);}
    void preTranslateBy(const Vector3& p){rows[3]+=applyVector(p);}
    void preMultiply(const Matrix& p);
};
Matrix Matrix::identity;
void D3DXMatrixScaling(Matrix* result,float x,float y,float z){*result=Matrix();(*result)[0].x=x;(*result)[1].y=y;(*result)[2].z=z;}
void D3DXMatrixMultiply(Matrix* result,const Matrix* a,const Matrix* b){
    Matrix output;
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j){
        const float* ar=&(*a)[i].x;float* out=&output[i].x;
        out[j]=0;for(unsigned k=0;k<4;++k)out[j]+=ar[k]*(&(*b)[k].x)[j];
    }
    *result=output;
}
void Matrix::preMultiply(const Matrix& p){D3DXMatrixMultiply(this,&p,this);}
void D3DXVec3TransformNormal(Vector3* output,const Vector3* input,const Matrix* matrix){*output=matrix->applyVector(*input);}
@SCALE_HELPER@
struct Vector2 {float x=0,y=0;void set(float a,float b){x=a;y=b;}};
struct Colour {
    static float getVector4(unsigned c){return float(c);}
    static unsigned getUint32(float c){return unsigned(c);}
};
namespace Math {inline float clamp(float a,float v,float b){return std::max(a,std::min(v,b));}}
struct PlaneEq {
    PlaneEq(Vector3,float){}
    void basis(Vector3& x,Vector3& y){x={1,0,0};y={0,1,0};}
};
struct Particle {
    Vector3 point={0,0,10};float radius=2;bool alive=true;
    bool isAlive()const{return alive;}
    float age()const{return 0;}
    const Vector3& position()const{return point;}
    float size()const{return radius;}
    float yaw()const{return 0;}
    float pitch()const{return 0;}
    unsigned colour()const{return 255;}
};
struct Particles:std::vector<Particle> {virtual ~Particles(){}void addParticle(const Particle& p,bool){push_back(p);}};
struct ContiguousParticles:Particles{};
struct BoundingBox {};
namespace Moo {
struct VertexXYZDUV {Vector3 pos_;DWORD colour_;Vector2 uv_;};
struct DrawContext {struct UserDrawItem {virtual ~UserDrawItem(){}};};
struct Context {
    Matrix view()const{return Matrix();}Matrix invView()const{return Matrix();}
    float halfScreenWidth()const{return 500;}
};
Context& rc(){static Context context;return context;}
}
struct Capture {Matrix matrix;float scale;unsigned count;};
struct ParticleSystemRenderer {
    virtual ~ParticleSystemRenderer(){}
    std::vector<Capture> draws;
    virtual void realDraw(const Matrix& matrix,Particles::iterator beg,Particles::iterator end,float scale=1.f){draws.push_back({matrix,scale,unsigned(end-beg)});}
    void draw(Moo::DrawContext&,const Matrix& matrix,Particles::iterator beg,Particles::iterator end,const BoundingBox&,float scale=1.f){realDraw(matrix,beg,end,scale);}
    bool viewDependent()const{return false;}
    bool local()const{return true;}
};
struct SpriteParticleRenderer:ParticleSystemRenderer {enum MaterialFX {FX_ADDITIVE_ALPHA,FX_ADDITIVE};};
@SPRITE_JOB@
@SPRITE_EXECUTE@
struct ParticleSystem {
    const char* name_="fixture";
    ParticleSystemRenderer* pRenderer_=NULL;
    Vector3 localOffset_={1,2,3};
    Particles particles;
    bool visible=true,enabled_=true,doUpdate_=false;
    bool isLodVisible(float)const{return visible;}
    bool enabled()const{return enabled_;}
    Particles::iterator begin(){return particles.begin();}Particles::iterator end(){return particles.end();}
    const BoundingBox& boundingBox(){static BoundingBox bounds;return bounds;}
    void draw(Moo::DrawContext&,const Matrix&,float,float scale=1.f);
};
@PARTICLE_DRAW@
struct MetaParticleSystem {
    using ParticleSystems=std::vector<std::shared_ptr<ParticleSystem>>;
    ParticleSystems systems_;
    void draw(Moo::DrawContext&,const Matrix&,float,float scale=1.f);
};
@META_DRAW@
struct PyMetaParticleSystem {
    MetaParticleSystem* pSystem_;float lod_=0,scale_=1;
    void draw(Moo::DrawContext&,const Matrix&);
};
@META_WRAPPER_DRAW@
struct PyParticleSystem {
    ParticleSystem* pSystem_;float lod_=0,scale_=1;
    void draw(Moo::DrawContext&,const Matrix&);
};
@PARTICLE_WRAPPER_DRAW@
@DRAW_ITEM_HEADER@
bool drawStockBattlegroundParticles(ParticleSystemRenderer*,const Matrix&,Particles::iterator,Particles::iterator,float);
@DRAW_ITEM_SET@
@DRAW_ITEM_DRAW@
@DRAW_ITEM_SPLIT@
// Existing native pointer/storage wrappers are boundaries of this fixture.
template<class T>struct Ref:std::shared_ptr<T> {
    using std::shared_ptr<T>::shared_ptr;
    Ref(T* value):std::shared_ptr<T>(value){}
    Ref()=default;
    Ref& operator=(T* value){std::shared_ptr<T>::operator=(std::shared_ptr<T>(value,[](T*){}));return *this;}
};
using ParticleSystemRendererPtr=Ref<ParticleSystemRenderer>;
using ParticlesPtr=Ref<Particles>;
struct Splitter {bool classify(float x,float,float)const{return x>0;}};
struct Fog {bool active=true;Splitter splitter;};
std::weak_ptr<Fog> fogOwner;
@DEFERRED_STORAGE@
void finishStockTPSParticleItems(){}
@BATTLEGROUND_DRAW@
@BATTLEGROUND_FINISH@
int main(){
    Moo::DrawContext context;Matrix world;world[3].x=10;world[3].y=20;world[3].z=30;
    SpriteParticleRenderer renderer;
    auto system=std::make_shared<ParticleSystem>();system->pRenderer_=&renderer;system->particles.resize(1);
    MetaParticleSystem meta;meta.systems_.push_back(system);
    PyMetaParticleSystem wrapper{&meta};PyParticleSystem single{system.get()};
    for(float scale:{1.f,2.5f,0.f,-2.f}){
        wrapper.scale_=scale;wrapper.draw(context,world);
        const auto& draw=renderer.draws.back();assert(draw.scale==scale);
        assert(draw.matrix[0].x==scale&&draw.matrix[1].y==scale&&draw.matrix[2].z==scale);
        assert(draw.matrix[3].x==10+scale&&draw.matrix[3].y==20+2*scale&&draw.matrix[3].z==30+3*scale);
        single.scale_=scale;single.draw(context,world);assert(renderer.draws.back().scale==scale);
    }
    const auto count=renderer.draws.size();system->visible=false;wrapper.draw(context,world);assert(renderer.draws.size()==count);
    system->visible=true;system->enabled_=false;wrapper.draw(context,world);assert(renderer.draws.size()==count);
    system->enabled_=true;
    // Independent items retain the value admitted before later wrapper changes.
    ParticleSystemDrawItem a,b;a.set(&renderer,world,system->begin(),system->end(),2.5f);
    b.set(&renderer,world,system->begin(),system->end(),-2.f);wrapper.scale_=999;
    b.draw();a.draw();assert(renderer.draws.back().scale==2.5f);
    assert(renderer.draws[renderer.draws.size()-2].scale==-2.f);
    // Real gas split owner freezes scale in both deferred buckets, including
    // renderer reuse and moving the pending vector before final submission.
    auto fog=std::make_shared<Fog>();fogOwner=fog;mainParticlePhase=&context;
    Particles particles;particles.resize(2);particles[0].point.x=-20;particles[1].point.x=20;
    a.set(&renderer,world,particles.begin(),particles.end(),3.25f);a.drawBattlegroundSplit();
    assert(renderer.draws.back().scale==3.25f&&deferredParticles.size()==1);
    mainParticlePhase=&context;b.set(&renderer,world,particles.begin(),particles.end(),-1.5f);b.drawBattlegroundSplit();
    assert(deferredParticles.size()==2);finishStockBattlegroundParticles();
    assert(renderer.draws[renderer.draws.size()-2].scale==3.25f&&renderer.draws.back().scale==-1.5f);
    assert(deferredParticles.empty()&&mainParticlePhase==NULL);
    // Execute the full actual sprite CPU job and inspect its submitted vertices.
    for(float scale:{1.f,2.5f,0.f,-2.f}){
        Particle particle;Moo::VertexXYZDUV vertices[6];SpriteParticleJob job{};
        job.pParticles_=&particle;job.numParticles_=1;job.view_=&Matrix::identity;
        job.worldTransform_=&Matrix::identity;job.scale_=scale;job.pVerticesOut_=vertices;
        job.fogRange_=1;job.materialFX_=SpriteParticleRenderer::FX_ADDITIVE;job.execute();
        if(scale<=0){assert(job.numTrisOut_==0);continue;}
        assert(job.numTrisOut_==2);assert(vertices[0].pos_.x==-2*scale);
        assert(vertices[0].pos_.y==2*scale&&vertices[0].pos_.z==10);
    }
    std::puts("Production draw chain: wrapper/native matrix, guards, deferred item/buckets and sprite vertices passed");
}
