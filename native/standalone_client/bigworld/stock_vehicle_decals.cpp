#include "pch.hpp"
#include "stock_vehicle_decals.hpp"
#include "stock_vehicle_decals_data.hpp"
#include "stock_deferred_decal_draw.hpp"
#include "stock_space_decals_texture.hpp"
#include "duplo/py_attachment.hpp"
#include "moo/device_callback.hpp"
#include "moo/draw_context.hpp"
#include "moo/format_name_map.hpp"
#include "moo/render_context.hpp"
#include "moo/texture_manager.hpp"
#include "physics2/collision_callback.hpp"
#include "physics2/collision_obstacle.hpp"
#include "resmgr/auto_config.hpp"
#include "resmgr/bwresource.hpp"
#include "space/client_space.hpp"
#include "space/deprecated_space_helpers.hpp"
#include <algorithm>
#include <memory>
#include <map>

BW_BEGIN_NAMESPACE
int StockVehicleDecals_token = 1;
namespace
{
AutoConfigString configuration("system/decalsConfig");
bool failure(const char* message) { ERROR_MSG("StockVehicleDecals: %s\n",message); return false; }
struct Image
{
    Moo::BaseTexturePtr source;
    std::map<unsigned,ComObjectWrap<DX::Texture>> views;
};
struct Decal
{
    std::shared_ptr<Image> image;
    std::weak_ptr<class Renderer> renderer;
    unsigned material=2,priority=0,influence=2,generation=0;
    bool active=true;
};
struct Pending
{
    std::shared_ptr<Decal> decal;
    Matrix world;
    unsigned generation;
};
class Renderer : public Moo::DeviceCallback
{
public:
    bool open=true,frame=false;
    int quality=0;
    unsigned anisotropy=2,minimumWidth=0,minimumHeight=0,maximumWidth=0,maximumHeight=0;
    D3DFORMAT format=D3DFMT_UNKNOWN;
    ClientSpacePtr space;
    std::vector<Pending> queue;
    std::vector<std::weak_ptr<Decal>> decals;
    std::map<BW::string,std::weak_ptr<Image>> images;
    const float lodBias[5]={1.f,.8f,.6f,.4f,0.f};
    const unsigned mipSkip[5]={0,1,1,2,3};
    void syncSpace()
    {
        ClientSpacePtr next=DeprecatedSpaceHelpers::cameraSpace();
        if(next && next->cleared()) next=NULL;
        if(space==next) return;
        // Creation may precede camera-space promotion. Only a previously live
        // space retires registrations; an old attachment cannot adopt a new one.
        if(space) retire();
        space=next;
    }
    void retire()
    {
        clearStockDeferredDecalTextures();
        queue.clear(); images.clear();
        for(auto& weak:decals) if(auto value=weak.lock())
        { value->active=false; ++value->generation; value->image.reset(); }
        decals.clear();
    }
    void deleteUnmanagedObjects() override
    {
        clearStockDeferredDecalTextures();
        queue.clear(); frame=false;
        for(auto& weak:images) if(auto image=weak.second.lock()) image->views.clear();
    }
    bool recreateForD3DExDevice() const override { return true; }
    std::shared_ptr<Image> load(const BW::string& name)
    {
        auto cached=images.find(name);
        if(cached!=images.end()) if(auto image=cached->second.lock()) return image;
        auto* textures=Moo::TextureManager::instance();
        if(!textures) { failure("The native texture manager is unavailable"); return {}; }
        auto image=std::make_shared<Image>();
        image->source=textures->getSystemMemoryTexture(name);
        if(!image->source || image->source->status()!=Moo::BaseTexture::STATUS_READY ||
           !image->source->pTexture() || image->source->pTexture()->GetType()!=D3DRTYPE_TEXTURE)
        { failure("The attachment decal texture is unavailable"); return {}; }
        D3DSURFACE_DESC desc;
        auto* texture=static_cast<DX::Texture*>(image->source->pTexture());
        if(FAILED(texture->GetLevelDesc(0,&desc)) || desc.Format!=format ||
           desc.Width<minimumWidth || desc.Height<minimumHeight ||
           desc.Width>maximumWidth || desc.Height>maximumHeight ||
           !desc.Width || !desc.Height || (desc.Width & (desc.Width-1)) || (desc.Height & (desc.Height-1)))
        { failure("The attachment texture violates the original OtherDecals constraints"); return {}; }
        images[name]=image; return image;
    }
};
std::shared_ptr<Renderer> renderer;
int selectedQuality=0;

class SceneHit : public CollisionCallback
{
public:
    float distance=-1.f;
    int operator()(const CollisionObstacle&,const WorldTriangle& triangle,float value) override
    {
        if((triangle.collisionFlags() & 0x12u)==2u) return COLLIDE_ALL;
        distance=value; return COLLIDE_BEFORE;
    }
};

class VehicleDecalAttachment : public PyAttachment
{
public:
    explicit VehicleDecalAttachment(PyTypeObject* type) : PyAttachment(type),decal_(std::make_shared<Decal>()) {}
    ~VehicleDecalAttachment() { decal_->active=false; ++decal_->generation; }
    bool needsUpdate() const override { return dirty_; }
    bool isLodVisible() const override { return true; }
#if ENABLE_TRANSFORM_VALIDATION
    bool isTransformFrameDirty(uint32) const override { return false; }
    bool isVisibleTransformFrameDirty(uint32) const override { return false; }
#endif
    void tick(float) override { dirty_=true; }
    void updateAnimations(const Matrix&,float) override {}
    void detach() override { ++decal_->generation; PyAttachment::detach(); }
    void leaveWorld() override { ++decal_->generation; PyAttachment::leaveWorld(); }
    void draw(Moo::DrawContext& context,const Matrix& world) override
    {
        dirty_=false;
        auto owner=decal_->renderer.lock();
        if(!owner || owner!=renderer || !owner->open || !owner->frame || !decal_->active ||
           !decal_->image || context.renderingPassType()!=Moo::RENDERING_PASS_COLOR ||
           Moo::rc().reflectionScene()) return;
        owner->syncSpace();
        if(!decal_->active || !owner->space) return;
        Matrix rotate,local,combined,pose=world;
        rotate.setRotateX(1.5707963705062866f);
        local.multiply(rotate,local_);
        if(collide_)
        {
            // a138e0 projects the world origin down using ClosestTriangle.
            Vector3 start=pose.applyToOrigin(),end=start;
            start.y+=10.f; end.y-=1000.f;
            SceneHit hit; owner->space->collide(start,end,hit);
            if(hit.distance>0.f) { Vector3 position=pose.applyToOrigin(); position.y=start.y-hit.distance; pose.translation(position); }
        }
        combined.multiply(local,pose);
        owner->queue.push_back({decal_,combined,decal_->generation});
    }
    void setLocalTransform(const Matrix& matrix) { local_=matrix; }
    void localBoundingBox(BoundingBox& bounds,bool=false) override
    {
        Matrix rotate,combined; rotate.setRotateX(1.5707963705062866f);
        combined.multiply(rotate,local_);
        BoundingBox box(Vector3(-.5f,-.5f,-.5f),Vector3(.5f,.5f,.5f));
        box.transformBy(combined); bounds.addBounds(box);
    }
    void localVisibilityBox(BoundingBox& bounds,bool skinny=false) override { localBoundingBox(bounds,skinny); }
    void worldBoundingBox(BoundingBox& bounds,const Matrix& world,bool skinny=false) override
    { BoundingBox box; localBoundingBox(box,skinny); box.transformBy(world); bounds.addBounds(box); }
    void worldVisibilityBox(BoundingBox& bounds,const Matrix& world,bool skinny=false) override
    { worldBoundingBox(bounds,world,skinny); }
protected:
    bool configure(const BW::string& texture,unsigned material,unsigned priority,unsigned influence)
    {
        if(!renderer || !renderer->open) return false;
        auto owner=renderer; owner->syncSpace();
        // 933160 preserves the manager registration after its first success.
        if(!decal_->renderer.expired()) return decal_->active;
        auto image=owner->load(texture);
        if(!image || renderer!=owner || !owner->open) return false;
        decal_->image=image; decal_->renderer=owner; decal_->material=material;
        decal_->priority=priority; decal_->influence=influence;
        owner->decals.push_back(decal_); return true;
    }
    bool collide_=false;
private:
    Matrix local_=Matrix::identity;
    std::shared_ptr<Decal> decal_;
    bool dirty_=true;
};
}

class WGOcclusionDecal : public VehicleDecalAttachment
{
    Py_Header(WGOcclusionDecal,PyAttachment)
public:
    WGOcclusionDecal() : VehicleDecalAttachment(&s_type_) {}
    bool create(const BW::string&,const BW::string&,const BW::string&,const BW::string& texture,
        uint32 priority,uint32,uint32 influence,uint32,uint32)
    { return configure(texture,2,priority,influence); }
    PY_AUTO_METHOD_DECLARE(RETDATA,create,ARG(BW::string,ARG(BW::string,ARG(BW::string,ARG(BW::string,
        ARG(uint32,ARG(uint32,ARG(uint32,ARG(uint32,ARG(uint32,END))))))))))
    PY_AUTO_METHOD_DECLARE(RETVOID,setLocalTransform,ARG(Matrix,END))
    PY_AUTO_CONSTRUCTOR_FACTORY_DECLARE(WGOcclusionDecal,END)
};
class WGShadowForwardDecal : public VehicleDecalAttachment
{
    Py_Header(WGShadowForwardDecal,PyAttachment)
public:
    WGShadowForwardDecal() : VehicleDecalAttachment(&s_type_) {}
    bool setup(const BW::string& texture,uint32 material,uint32 priority,uint32 influence)
    {
        // The sole #1513 producer requests SHADOWS_FORWARD. Other materials
        // require their own texture schema and completed render-phase owner.
        if(material!=6) return failure("Unsupported vehicle shadow decal material");
        return configure(texture,material,priority,influence);
    }
    void collideWithScene(bool enabled) { collide_=enabled; }
    PY_AUTO_METHOD_DECLARE(RETDATA,setup,ARG(BW::string,ARG(uint32,ARG(uint32,ARG(uint32,END)))))
    PY_AUTO_METHOD_DECLARE(RETVOID,setLocalTransform,ARG(Matrix,END))
    PY_AUTO_METHOD_DECLARE(RETVOID,collideWithScene,ARG(bool,END))
    PY_AUTO_CONSTRUCTOR_FACTORY_DECLARE(WGShadowForwardDecal,END)
};
PY_TYPEOBJECT(WGOcclusionDecal)
PY_BEGIN_METHODS(WGOcclusionDecal)
    PY_METHOD(create)
    PY_METHOD(setLocalTransform)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(WGOcclusionDecal)
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED(WGOcclusionDecal,"WGOcclusionDecal",BigWorld)
PY_TYPEOBJECT(WGShadowForwardDecal)
PY_BEGIN_METHODS(WGShadowForwardDecal)
    PY_METHOD(setup)
    PY_METHOD(setLocalTransform)
    PY_METHOD(collideWithScene)
PY_END_METHODS()
PY_BEGIN_ATTRIBUTES(WGShadowForwardDecal)
PY_END_ATTRIBUTES()
PY_FACTORY_NAMED(WGShadowForwardDecal,"WGShadowForwardDecal",BigWorld)

bool initialiseStockVehicleDecals()
{
    if(renderer) return true;
    auto owner=std::make_shared<Renderer>(); owner->quality=selectedQuality;
    DataSectionPtr config=BWResource::openSection(configuration.value());
    DataSectionPtr scene=config?config->openSection("semiDynamic"):NULL;
    if(!scene) return failure("The original semiDynamic decal configuration is unavailable");
    const int anisotropy=scene->readInt("anisotropy",2);
    if(anisotropy<0 || anisotropy>256) return failure("Invalid original semiDynamic anisotropy");
    owner->anisotropy=static_cast<unsigned>(anisotropy);
    // a63610..a6384f owns the persistent-provider quality table. It does
    // not consume scene/quality, which belongs to compiled scene decals.
    BW::vector<DataSectionPtr> types; scene->openSections("type",types);
    for(const auto& type:types)
    {
        if(type->readString("name")!="OtherDecals") continue;
        BW::vector<DataSectionPtr> atlases; type->openSections("atlas",atlases);
        if(atlases.size()!=1) return failure("The original attachment OtherDecals atlas is unavailable");
        DataSectionPtr limits=atlases[0]->openSection("constrains");
        if(!limits) return failure("Missing original attachment texture constraints");
        BW::string format=limits->readString("format"); owner->format=Moo::FormatNameMap::formatType(format);
        owner->minimumWidth=limits->readUInt("minWidth"); owner->minimumHeight=limits->readUInt("minHeight");
        owner->maximumWidth=limits->readUInt("maxWidth"); owner->maximumHeight=limits->readUInt("maxHeight");
    }
    if(owner->format==D3DFMT_UNKNOWN || !owner->minimumWidth || !owner->minimumHeight)
        return failure("The original attachment texture atlas family is unavailable");
    renderer=owner; return true;
}
void finaliseStockVehicleDecals()
{
    auto owner=renderer; renderer.reset();
    if(!owner) return;
    owner->open=false; owner->frame=false; owner->retire(); owner->deleteUnmanagedObjects(); owner->space=NULL;
}
void setStockVehicleDecalsQuality(int option)
{
    if(option<0 || option>=5) { failure("Invalid native decal quality"); return; }
    selectedQuality=option; if(renderer) renderer->quality=option;
}
void beginStockVehicleDecals()
{
    if(!renderer || !renderer->open) return;
    renderer->syncSpace(); renderer->queue.clear(); renderer->frame=true;
}
void endStockVehicleDecals()
{
    if(!renderer) return;
    renderer->queue.clear(); renderer->frame=false;
}
bool drawStockVehicleDecalsPhase(unsigned phase)
{
    auto owner=renderer;
    if(!owner || !owner->open || !owner->frame) return true;
    owner->syncSpace();
    const float bias=owner->lodBias[owner->quality];
    if(!owner->space || bias<=0.f) return true;
    const unsigned skip=owner->mipSkip[owner->quality];
    if(skip>16) return failure("The original attachment mip skip exceeds texture dimensions");
    const float start=wot::stockDecalMultiply(100.f,bias),end=wot::stockDecalMultiply(150.f,bias);
    const float startSquared=wot::stockDecalMultiply(start,start);
    const float inverse=wot::stockDecalDivide(1.f,wot::stockDecalSubtract(wot::stockDecalMultiply(end,end),startSquared));
    const Vector3 eye=Moo::rc().invView().applyToOrigin();
    std::vector<StockDeferredDecalPacket> packets;
    for(const auto& pending:owner->queue)
    {
        const auto& value=pending.decal;
        if(!value->active || value->generation!=pending.generation || !value->image) continue;
        if((phase==1 && value->material!=2) || (phase==0 && value->material!=6) || phase>1) continue;
        BoundingBox bounds(Vector3(-.5f,-.5f,-.5f),Vector3(.5f,.5f,.5f)); bounds.transformBy(pending.world);
        bounds.calculateOutcode(Moo::rc().viewProjection()); if(bounds.combinedOutcode()) continue;
        const Vector3 position=pending.world.applyToOrigin();
        const float point[]={position.x,position.y,position.z},camera[]={eye.x,eye.y,eye.z};
        const float distance=wot::stockSpaceDecalDistance(point,camera,Moo::rc().lodZoomFactor());
        const float alpha=std::max(0.f,std::min(1.f,wot::stockDecalSubtract(1.f,
            wot::stockDecalMultiply(wot::stockDecalSubtract(distance,startSquared),inverse))));
        if(alpha<=.003921569f) continue;
        auto* source=static_cast<DX::Texture*>(value->image->source->pTexture());
        D3DSURFACE_DESC desc;
        if(!source || FAILED(source->GetLevelDesc(skip,&desc)) || !desc.Width || !desc.Height ||
           desc.Width>65536 || desc.Height>65536) return failure("The attachment source mip is unavailable");
        auto& view=value->image->views[skip];
        if(!view && FAILED(wot::copyStockSpaceDecalMipTexture(Moo::rc().device(),source,skip,
            wot::stockSpaceDecalMipLevels(owner->anisotropy,skip),&view)))
            return failure("Cannot upload the original attachment decal texture");
        D3DXVECTOR3 axes[]={{pending.world(0,0),pending.world(0,1),pending.world(0,2)},
            {pending.world(1,0),pending.world(1,1),pending.world(1,2)},
            {pending.world(2,0),pending.world(2,1),pending.world(2,2)}};
        const float scale[]={D3DXVec3Length(&axes[0]),D3DXVec3Length(&axes[1]),D3DXVec3Length(&axes[2])};
        D3DXVec3Normalize(&axes[1],&axes[1]); D3DXVec3Normalize(&axes[2],&axes[2]);
        const D3DXVECTOR3 origin(0.f,0.f,0.f); D3DXMATRIX rotationMatrix; D3DXQUATERNION rotation;
        D3DXMatrixLookAtLH(&rotationMatrix,&origin,&axes[2],&axes[1]);
        D3DXQuaternionRotationMatrix(&rotation,&rotationMatrix);
        const std::array<uint16,4> rectangle={{0,0,static_cast<uint16>(desc.Width-1),static_cast<uint16>(desc.Height-1)}};
        StockDeferredDecalPacket packet;
        packet.record=wot::stockVehicleDecalRecord(value->material,value->priority,value->influence,distance,
            alpha,&rotation.x,point,scale,rectangle);
        packet.kind=0;
        packet.material.controls[4]=owner->anisotropy;
        packet.material.controls[5]=((owner->anisotropy>>1)<<4)/(1u<<skip);
        packet.material.controls[6]=1;
        packet.material.textureSizes[12]=float(desc.Width); packet.material.textureSizes[13]=float(desc.Height);
        packet.material.textureSizes[14]=1.f/float(desc.Width); packet.material.textureSizes[15]=1.f/float(desc.Height);
        packet.textures[3]=view.pComObject(); packets.push_back(packet);
    }
    std::sort(packets.begin(),packets.end(),[](const StockDeferredDecalPacket& a,const StockDeferredDecalPacket& b)
    { return wot::stockDecalAdmissionLess(stockDeferredDecalAdmissionKey(a.record),stockDeferredDecalAdmissionKey(b.record)); });
    const unsigned limit=stockDeferredDecalVisibleLimit(); if(packets.size()>limit) packets.resize(limit);
    return drawStockDeferredDecalPackets(packets,phase);
}
BW_END_NAMESPACE
