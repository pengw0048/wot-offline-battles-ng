// Actual SDK AABB and particle method bodies are injected by the Python test.
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define INLINE
#define BW_GUARD
struct SentinelRead {};
#define MF_ASSERT(value) do { if (!(value)) throw SentinelRead(); } while(0)
#define MF_ASSERT_DEBUG(value) MF_ASSERT(value)
enum {X_AXIS=0,Y_AXIS=1,Z_AXIS=2,X_COORD=0,Y_COORD=1,Z_COORD=2};
struct Vector3 {
    float x,y,z;
    Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}
    float& operator[](int i){return (&x)[i];}
    float operator[](int i)const{return (&x)[i];}
    Vector3 operator+(const Vector3& v)const{return {x+v.x,y+v.y,z+v.z};}
    Vector3 operator-(const Vector3& v)const{return {x-v.x,y-v.y,z-v.z};}
    Vector3& operator*=(float n){x*=n;y*=n;z*=n;return *this;}
};
Vector3 operator*(float n,Vector3 v){return v*=n;}
struct Matrix {
    Vector3 rows[4];
    static const Matrix identity;
    Matrix(){rows[0]={1,0,0};rows[1]={0,1,0};rows[2]={0,0,1};}
    Vector3& operator[](int i){return rows[i];}
    Vector3 applyPoint(const Vector3& v)const{return v.x*rows[0]+v.y*rows[1]+v.z*rows[2]+rows[3];}
    Vector3 applyVector(const Vector3& v)const{return v.x*rows[0]+v.y*rows[1]+v.z*rows[2];}
    Vector3 applyToUnitAxisVector(int i)const{return rows[i];}
    void translation(const Vector3& v){rows[3]=v;}
    void multiply(const Matrix& a,const Matrix& b){Matrix out;for(int i=0;i<3;++i)out.rows[i]=b.applyVector(a.rows[i]);out.rows[3]=b.applyPoint(a.rows[3]);*this=out;}
    bool invert(const Matrix& v){
        const float a=v.rows[0].x,b=v.rows[0].y,c=v.rows[0].z;
        const float d=v.rows[1].x,e=v.rows[1].y,f=v.rows[1].z;
        const float g=v.rows[2].x,h=v.rows[2].y,i=v.rows[2].z;
        const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
        if(det==0)return false;
        rows[0]={(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det};
        rows[1]={(f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det};
        rows[2]={(d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det};
        rows[3]=-1.f*applyVector(v.rows[3]);return true;
    }
    void invert(){Matrix copy=*this;assert(invert(copy));}
};
const Matrix Matrix::identity;
struct AABB {
    Vector3 min_,max_;
    AABB();AABB(const Vector3&,const Vector3&);
    bool insideOut()const;
    const Vector3& minBounds()const;const Vector3& maxBounds()const;
    void addBounds(const Vector3&);void addBounds(const AABB&);
    void transformBy(const Matrix&);
};
struct BoundingBox:AABB {
    int oc_,combinedOc_;
    BoundingBox();BoundingBox(const Vector3&,const Vector3&);
};
@SDK_BOUNDS@
struct ParticleSystem {
    bool enabled_=true,static_=false,local_=true;
    std::vector<int> storage;
    std::vector<int>* particles_=&storage;
    BoundingBox boundingBox_;
    Matrix pose;
    bool isStatic()const{return static_;}bool isLocal()const{return local_;}
    Matrix worldTransform()const{return pose;}
    void localBoundingBox(BoundingBox&)const;
};
@SDK_PARTICLE@
struct MetaParticleSystem {
    using ParticleSystems=std::vector<ParticleSystem*>;
    ParticleSystems systems_;
    void localBoundingBox(BoundingBox&)const;
};
@SDK_META@
struct Attachment {
    bool attached=true;
    void* liaison=nullptr;
    std::function<void()> onBounds;
    virtual ~Attachment(){}
    virtual void localBoundingBox(BoundingBox& bounds,bool=false){if(onBounds)onBounds();bounds.addBounds(Vector3());}
    virtual void worldBoundingBox(BoundingBox& bounds,const Matrix& world,bool skinny=false){BoundingBox local;localBoundingBox(local,skinny);if(!local.insideOut()){local.transformBy(world);bounds.addBounds(local);}}
};
struct Model:Attachment {
    BoundingBox box;
    explicit Model(BoundingBox value={}):box(value){}
    void localBoundingBox(BoundingBox& bounds,bool=false)override{if(onBounds)onBounds();bounds.addBounds(box);}
};
struct MetaAttachment:Attachment {
    MetaParticleSystem particles;
    void localBoundingBox(BoundingBox& bounds,bool=false)override{if(onBounds)onBounds();particles.localBoundingBox(bounds);}
};
using PyModelPtr=std::shared_ptr<Model>;
struct CompoundPart {PyModelPtr model;size_t mount;};
struct CompoundNode {Matrix world;};
struct CompoundAttachment {
    std::shared_ptr<Attachment> object;
    size_t node=0;Matrix local,world;bool active=true;
    bool ownsObject(){return object->attached && object->liaison==this;}
};
using CompoundAttachmentPtr=std::shared_ptr<CompoundAttachment>;
struct CompoundState {bool active=true;std::vector<CompoundNode> nodes;std::vector<CompoundPart> parts;std::vector<CompoundAttachmentPtr> attachments;};
using CompoundStatePtr=std::shared_ptr<CompoundState>;
@VISIT@
struct Owner:Attachment {
    CompoundStatePtr state_;
    explicit Owner(CompoundStatePtr value):state_(value){}
    @LOCAL@
    @VISIBILITY@
    @WORLD@
    @WORLD_VISIBILITY@
};
struct MatrixProvider {MatrixProvider(bool,int*){}virtual ~MatrixProvider(){}};
#define Py_Header(TYPE,BASE) inline static int s_type_=0;
@PART_PROVIDER@;
static bool near(float a,float b){return std::fabs(a-b)<.0001f;}
static void vectorEquals(const Vector3& a,const Vector3& b){for(int i=0;i<3;++i)assert(near(a[i],b[i]));}
static void boxEquals(const BoundingBox& a,const BoundingBox& b){assert(a.insideOut()==b.insideOut());if(!a.insideOut()){vectorEquals(a.minBounds(),b.minBounds());vectorEquals(a.maxBounds(),b.maxBounds());}}
static void matrixEquals(const Matrix& a,const Matrix& b){for(int i=0;i<4;++i)vectorEquals(a.rows[i],b.rows[i]);}
static BoundingBox corners(const BoundingBox& box,const Matrix& transform){BoundingBox out;for(int i=0;i<8;++i)out.addBounds(transform.applyPoint({i&1?box.max_.x:box.min_.x,i&2?box.max_.y:box.min_.y,i&4?box.max_.z:box.min_.z}));return out;}
static CompoundStatePtr state(){auto result=std::make_shared<CompoundState>();result->nodes.resize(3);result->nodes[0].world.translation({10,20,30});result->nodes[1].world.translation({13,25,37});result->nodes[2].world.translation({6,18,39});return result;}
static CompoundAttachmentPtr attach(const CompoundStatePtr& value,std::shared_ptr<Attachment> object,size_t node=0){auto item=std::make_shared<CompoundAttachment>();item->object=object;item->node=node;object->liaison=item.get();value->attachments.push_back(item);return item;}
static BoundingBox realBox(){return {{-1,-2,-3},{2,4,1}};}
static void particles(){
    auto value=state();Owner owner(value);
    auto meta=std::make_shared<MetaAttachment>();ParticleSystem particle;meta->particles.systems_.push_back(&particle);attach(value,meta,1);
    // The captured Windows failure is exactly the SDK default sentinel.
    BoundingBox empty;for(int axis=0;axis<3;++axis)assert(empty.min_[axis]==FLT_MAX&&empty.max_[axis]==-FLT_MAX);
    BoundingBox output;owner.localBoundingBox(output,true);assert(output.insideOut());
    particle.enabled_=false;particle.storage.push_back(1);particle.boundingBox_=realBox();
    owner.localBoundingBox(output);assert(output.insideOut());
    particle.enabled_=true;particle.storage.clear();owner.localBoundingBox(output);assert(output.insideOut());
    particle.storage.push_back(1);particle.boundingBox_=BoundingBox();owner.localBoundingBox(output);assert(output.insideOut());
    // A live particle later contributes normally; no persistent suppression.
    particle.boundingBox_=realBox();Matrix relative;relative.translation({3,5,7});
    owner.localBoundingBox(output);boxEquals(output,corners(realBox(),relative));
    BoundingBox visibility;owner.localVisibilityBox(visibility,true);boxEquals(output,visibility);
    BoundingBox world;owner.worldBoundingBox(world,Matrix::identity,true);boxEquals(world,corners(realBox(),value->nodes[1].world));
    std::puts("PASS actual SDK empty particles become populated without fabricated geometry");
}
static void mixed(){
    auto value=state();Owner owner(value);
    value->parts.push_back({std::make_shared<Model>(),1});
    BoundingBox output;owner.localBoundingBox(output);assert(output.insideOut());
    auto model=std::make_shared<Model>(realBox());value->parts.push_back({model,1});
    auto empty=std::make_shared<MetaAttachment>();attach(value,empty,0);
    auto child=std::make_shared<Model>(BoundingBox({-2,-1,-4},{1,2,3}));auto item=attach(value,child,2);
    item->local.rows[0]={0,2,0};item->local.rows[1]={-3,0,0};item->local.rows[2]={0,0,.5f};item->local.rows[3]={2,3,4};
    Matrix inverse;assert(inverse.invert(value->nodes[0].world));
    Matrix partRelative;partRelative.multiply(value->nodes[1].world,inverse);
    Matrix childWorld;childWorld.multiply(item->local,value->nodes[2].world);
    Matrix childRelative;childRelative.multiply(childWorld,inverse);
    BoundingBox expected=corners(realBox(),partRelative);expected.addBounds(corners(child->box,childRelative));
    owner.localBoundingBox(output,true);boxEquals(output,expected);
    BoundingBox visibility;owner.localVisibilityBox(visibility);boxEquals(visibility,expected);
    BoundingBox expectedWorld=corners(realBox(),value->nodes[1].world);expectedWorld.addBounds(corners(child->box,childWorld));
    BoundingBox world;owner.worldBoundingBox(world,Matrix::identity,true);boxEquals(world,expectedWorld);
    BoundingBox worldVisibility;owner.worldVisibilityBox(worldVisibility,Matrix::identity);boxEquals(worldVisibility,expectedWorld);
    // Existing caller contributions survive an all-empty compound.
    auto allEmpty=state();Owner noBounds(allEmpty);allEmpty->parts.push_back({std::make_shared<Model>(),1});
    attach(allEmpty,std::make_shared<MetaAttachment>());BoundingBox prior=realBox();noBounds.localBoundingBox(prior);boxEquals(prior,realBox());
    std::puts("PASS mixed local/world/visibility and affine nonempty contributions");
}
static void provider(){
    auto value=state();auto model=std::make_shared<Model>(realBox());value->parts.push_back({model,1});
    PyCompoundPartBounds provider(value,0);Matrix committed;provider.matrix(committed);
    model->box=BoundingBox();Matrix output;provider.matrix(output);matrixEquals(output,committed);
    value->active=false;provider.matrix(output);matrixEquals(output,committed);
    std::puts("PASS part provider preserves snapshot for empty and retired models");
}
static void retire(){
    for(int scenario=0;scenario<3;++scenario){
        auto value=state();Owner owner(value);auto model=std::make_shared<Model>(realBox());auto item=attach(value,model,1);
        model->onBounds=[value,item,model,scenario](){
            if(scenario==0)value->active=false;
            if(scenario==1)item->active=false;
            if(scenario==2)model->liaison=nullptr;
        };
        BoundingBox output;owner.localBoundingBox(output);assert(output.insideOut());
        model->onBounds=nullptr;
    }
    std::puts("PASS retired contributions excluded after synchronous bbox reentry");
}
int main(int argc,char** argv){
    assert(argc==2);
    try{std::string which=argv[1];if(which=="particles")particles();else if(which=="mixed")mixed();else if(which=="provider")provider();else if(which=="retire")retire();else return 1;}
    catch(const SentinelRead&){std::puts("original SDK inside-out precondition triggered");return 73;}
}
