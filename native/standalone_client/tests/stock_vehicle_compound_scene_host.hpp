// Production compound and SDK attachment/scene bodies are injected by Python.
@MATHS@
#include <map>
#include <utility>
using uint32=unsigned;
namespace BW {using string=std::string;}
#define SCOPED_DISABLE_FRAME_BEHIND_WARNING
#define bw_safe_delete(value) do {delete value;value=nullptr;} while(0)
#define PyErr_SetString(type,message) ((void)0)
struct MatrixLiaison {
    virtual ~MatrixLiaison(){}
    virtual const Matrix& getMatrix()const=0;
    virtual bool setMatrix(const Matrix&)=0;
};
struct ModelMatrixLiaison:MatrixLiaison {
    Matrix value;
    const Matrix& getMatrix()const override{return value;}
    bool setMatrix(const Matrix& pose)override{value=pose;return true;}
};
struct Provider {
    Matrix value;std::function<void()> callback;
    void matrix(Matrix& target){if(callback)callback();target=value;}
};
using MatrixProviderPtr=std::shared_ptr<Provider>;
using PyObjectPtr=std::shared_ptr<int>;
struct PyAttachment {
    MatrixLiaison* pOwnWorld_=new ModelMatrixLiaison();
    bool attached_=false,inWorld_=false;
    virtual ~PyAttachment(){assert(!attached_);delete pOwnWorld_;}
    bool isAttached()const{return attached_;}bool isInWorld()const{return inWorld_;}
    MatrixLiaison* matrixLiaison()const{return pOwnWorld_;}
    const Matrix& worldTransform()const{return pOwnWorld_?pOwnWorld_->getMatrix():Matrix::identity;}
    void worldTransform(const Matrix& value){if(pOwnWorld_)pOwnWorld_->setMatrix(value);}
    virtual bool attach(MatrixLiaison*);virtual void detach();
    virtual void enterWorld(){inWorld_=true;}virtual void leaveWorld(){inWorld_=false;}
    virtual void tick(float){}
    virtual void updateAnimations(const Matrix&,float){}
    virtual void localBoundingBox(BoundingBox&,bool=false){}
    virtual void localVisibilityBox(BoundingBox& out,bool skinny=false){localBoundingBox(out,skinny);}
    virtual void worldBoundingBox(BoundingBox& out,const Matrix& world,bool skinny=false){BoundingBox local;localBoundingBox(local,skinny);if(!local.insideOut()){local.transformBy(world);out.addBounds(local);}}
};
@ATTACH@
@DETACH@
using PyAttachmentPtr=std::shared_ptr<PyAttachment>;
struct Model:PyAttachment {
    BoundingBox bounds{{-1,-2,-3},{2,4,1}};
    Matrix drawn;float lastLod=0;std::function<void()> onTick;
    void tick(float)override{if(onTick)onTick();}
    void updateAnimations(const Matrix& world,float lod)override{drawn=world;lastLod=lod;}
    void localBoundingBox(BoundingBox& out,bool=false)override{out.addBounds(bounds);}
};
using PyModelPtr=std::shared_ptr<Model>;
@NODE@
@PART@
struct CompoundAttachment;
using CompoundAttachmentPtr=std::shared_ptr<CompoundAttachment>;
@STATE@
using CompoundStatePtr=std::shared_ptr<CompoundState>;
@ITEM@
@VISIT@
namespace Moo {
struct Context {uint32 frameTimestamp()const{return 1;}};
static Context context;
Context& rc(){return context;}
}
struct Owner:PyAttachment {
    CompoundStatePtr state_;std::map<unsigned,PyObjectPtr> handles_;
    explicit Owner(CompoundStatePtr state):state_(state){}
    @MATRIX@
    @TICK@
    @UPDATE@
    @LOCAL@
    @VISIBILITY@
    @WORLD@
    @ENTER@
    @LEAVE@
    @RESET@
};
struct DynamicScene {
    struct Record{Matrix pose;BoundingBox collision,visibility;};
    std::map<unsigned,Record> live,pending;unsigned next=1,updates=0,stale=0;
    unsigned addObject(int,const Matrix& pose,const BoundingBox& collision,const BoundingBox& visibility){unsigned id=next++;live[id]={pose,collision,visibility};return id;}
    void updateObject(unsigned id,const Matrix& pose,const BoundingBox& collision,const BoundingBox& visibility){if(!live.count(id)){++stale;return;}pending[id]={pose,collision,visibility};++updates;}
    void removeObject(unsigned id){live.erase(id);pending.erase(id);}
    void onPostTick(){for(const auto& item:pending)live[item.first]=item.second;pending.clear();}
};
struct CompiledSpace {DynamicScene scene;DynamicScene& dynamicScene(){return scene;}};
using ClientSpacePtr=std::shared_ptr<CompiledSpace>;
struct LodSettings {
    static LodSettings& instance(){static LodSettings value;return value;}
    float calculateLod(const Matrix& world){const auto& p=world.rows[3];return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);}
};
struct PyAttachmentEntityEmbodiment:MatrixLiaison {
    PyAttachmentPtr pAttachment_;Matrix worldTransform_;
    CompiledSpace* pEnteredSpace_=nullptr;unsigned dynamicObjectHandle_=0;int sceneObject_=0;
    explicit PyAttachmentEntityEmbodiment(PyAttachmentPtr object):pAttachment_(object){assert(pAttachment_->attach(this));}
    ~PyAttachmentEntityEmbodiment(){assert(!pEnteredSpace_);pAttachment_->detach();}
    const Matrix& getMatrix()const override{return worldTransform_;}
    bool setMatrix(const Matrix&)override;
    const Matrix& doWorldTransform()const{return worldTransform_;}
    void doWorldTransform(const Matrix&);
    void doTick(float);void doUpdateAnimations(float);void syncTransform();
    void doEnterSpace(ClientSpacePtr,bool);void doLeaveSpace(bool);
};
@SCENE_TICK@
@SCENE_ANIMATE@
@SCENE_SYNC@
@SCENE_TRANSFORM@
@SCENE_ENTER@
@SCENE_LEAVE@
@SCENE_SET@
struct ITickSceneListener {
    virtual ~ITickSceneListener(){}
    virtual void onPreTick()=0;virtual void onPostTick()=0;
};
struct ProviderInterface {virtual ~ProviderInterface(){};virtual void tick(float)=0;};
struct TickSceneView {
    using Listeners=std::vector<ITickSceneListener*>;
    using ProviderCollection=std::vector<ProviderInterface*>;
    Listeners listeners_;ProviderCollection providers_;
    void tick(float);
};
@SCENE_PHASES@
static void sceneTick(PyAttachmentEntityEmbodiment& embodiment,CompiledSpace& space){
    struct Adapter:ITickSceneListener,ProviderInterface {
        PyAttachmentEntityEmbodiment& model;DynamicScene& scene;
        Adapter(PyAttachmentEntityEmbodiment& value,DynamicScene& owner):model(value),scene(owner){}
        void onPreTick()override{}
        void tick(float elapsed)override{model.doTick(elapsed);}
        void onPostTick()override{scene.onPostTick();}
    } adapter(embodiment,space.scene);
    TickSceneView phases;phases.providers_.push_back(&adapter);phases.listeners_.push_back(&adapter);
    phases.tick(.016f);
}
static bool near(float a,float b){return std::fabs(a-b)<.0001f;}
static bool equal(const Vector3&a,const Vector3&b){return near(a.x,b.x)&&near(a.y,b.y)&&near(a.z,b.z);}
static bool equal(const Matrix&a,const Matrix&b){for(int i=0;i<4;++i)if(!equal(a.rows[i],b.rows[i]))return false;return true;}
static bool equal(const BoundingBox&a,const BoundingBox&b){return a.insideOut()==b.insideOut()&&(a.insideOut()||(equal(a.min_,b.min_)&&equal(a.max_,b.max_)));}
static void require(bool value){if(!value)throw 41;}
static Matrix pose(float x,float z,float angle){Matrix m;float c=std::cos(angle),s=std::sin(angle);m.rows[0]={c,0,-s};m.rows[2]={s,0,c};m.rows[3]={x,5,z};return m;}
struct Fixture {
    std::shared_ptr<Provider> provider=std::make_shared<Provider>();
    CompoundStatePtr state=std::make_shared<CompoundState>();
    std::shared_ptr<Owner> owner;std::shared_ptr<Model> model=std::make_shared<Model>();
    Fixture(){state->nodes.resize(2);state->nodes[0].parent=-1;state->nodes[1].parent=0;state->nodes[1].local.translation({2,0,3});state->parts.push_back({model,{},"part",1,{}});owner=std::make_shared<Owner>(state);}
    void bind(){owner->matrix(provider);}
};
static void verify(Fixture& fixture,PyAttachmentEntityEmbodiment& embodiment,CompiledSpace& space){
    auto record=space.scene.live.at(embodiment.dynamicObjectHandle_);
    require(equal(record.pose,fixture.state->nodes[0].world));
    require(equal(embodiment.worldTransform_,fixture.state->nodes[0].world));
    BoundingBox expected;fixture.owner->worldBoundingBox(expected,Matrix::identity);
    require(equal(record.collision,expected));require(equal(record.visibility,expected));
    embodiment.doUpdateAnimations(.016f);
    require(equal(fixture.model->drawn,fixture.state->nodes[1].world));
    require(near(fixture.model->lastLod,LodSettings::instance().calculateLod(fixture.provider->value)));
}
static void motion(){
    Fixture f;f.bind();auto space=std::make_shared<CompiledSpace>();PyAttachmentEntityEmbodiment wrapper(f.owner);
    wrapper.doEnterSpace(space,false);
    try {
        for(int i=0;i<8;++i){f.provider->value=pose(100+i*37,-80+i*19,i*.37f);sceneTick(wrapper,*space);verify(f,wrapper,*space);}
        require(space->scene.updates==8);require(space->scene.stale==0);
    }catch(...){wrapper.doLeaveSpace(false);throw;}
    wrapper.doLeaveSpace(false);
    std::puts("PASS moving/rotating compounds update scene bounds and LOD");
}
static void latebind(){
    Fixture f;auto space=std::make_shared<CompiledSpace>();PyAttachmentEntityEmbodiment wrapper(f.owner);
    wrapper.doEnterSpace(space,false);
    try {
        sceneTick(wrapper,*space); // The stock appearance assigns its provider after model attachment.
        f.provider->value=pose(-410,175,1.2f);f.bind();sceneTick(wrapper,*space);verify(f,wrapper,*space);
        auto replacement=std::make_shared<Provider>();replacement->value=pose(620,-360,-.5f);
        f.provider=replacement;f.bind();
        // Tick runs for every dynamic object, without a visibility-dependent draw.
        sceneTick(wrapper,*space);verify(f,wrapper,*space);
    }catch(...){wrapper.doLeaveSpace(false);throw;}
    wrapper.doLeaveSpace(false);
    std::puts("PASS late root binding and offscreen publication");
}
static void rounds(){
    Fixture f;f.bind();auto first=std::make_shared<CompiledSpace>(),second=std::make_shared<CompiledSpace>();
    {
        PyAttachmentEntityEmbodiment wrapper(f.owner);wrapper.doEnterSpace(first,false);
        try{f.provider->value=pose(130,70,.4f);sceneTick(wrapper,*first);verify(f,wrapper,*first);}
        catch(...){wrapper.doLeaveSpace(false);throw;}
        wrapper.doLeaveSpace(false);require(first->scene.live.empty());
    }
    unsigned updates=first->scene.updates;
    f.provider->value=pose(-90,350,-.9f);f.owner->tick(.016f);
    require(first->scene.updates==updates&&first->scene.stale==0);
    {
        PyAttachmentEntityEmbodiment wrapper(f.owner);wrapper.doEnterSpace(second,false);
        // Base attach copies the detached liaison's already published transform.
        verify(f,wrapper,*second);
        f.provider->value=pose(280,-420,2.1f);sceneTick(wrapper,*second);verify(f,wrapper,*second);
        wrapper.doLeaveSpace(false);
    }
    require(first->scene.updates==updates&&second->scene.stale==0);
    std::puts("PASS detached owner and second round use distinct scene membership");
}
static void reentry(){
    for(int callback=0;callback<2;++callback){
        Fixture f;f.bind();auto space=std::make_shared<CompiledSpace>();PyAttachmentEntityEmbodiment wrapper(f.owner);
        wrapper.doEnterSpace(space,false);unsigned updates=space->scene.updates;
        auto reset=[&](){wrapper.doLeaveSpace(false);f.owner->reset();};
        if(callback==0)f.provider->callback=reset;else f.model->onTick=reset;
        sceneTick(wrapper,*space);require(space->scene.updates==updates);
        require(space->scene.live.empty()&&space->scene.stale==0);
    }
    std::puts("PASS reset callbacks cannot republish a retired compound");
}
int main(int argc,char**argv){
    if(argc!=2)return 2;
    try{std::string mode=argv[1];if(mode=="motion")motion();else if(mode=="latebind")latebind();else if(mode=="rounds")rounds();else if(mode=="reentry")reentry();else return 2;}
    catch(int value){return value;}
}
