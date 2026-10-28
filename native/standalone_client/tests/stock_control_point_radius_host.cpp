// Production radius projection/queue/buffer owner with guarded SDK boundaries.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <functional>
#include <fstream>
#include <map>
#include <memory>
#include <limits>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define BW_GUARD
using uint32=unsigned;
#define FAILED(x) ((x)<0)
#define SUCCEEDED(x) ((x)>=0)
enum {D3DLOCK_READONLY=1,D3DUSAGE_WRITEONLY=8,D3DPOOL_MANAGED=1,D3DPT_TRIANGLELIST=4};
namespace BW {
template<class T>struct Ptr:std::shared_ptr<T>{using Base=std::shared_ptr<T>;Ptr()=default;Ptr(T* p):Base(p){}T* getObject()const{return this->get();}};
class Vector3{public:float x,y,z;Vector3(float a=0,float b=0,float c=0):x(a),y(b),z(c){}Vector3 operator-(Vector3 p)const{return {x-p.x,y-p.y,z-p.z};}};
class Vector4{public:float x,y,z,w;Vector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}};
class Matrix{public:float x=1,y=1,z=1;Vector3 p;void setScale(float a,float b,float c){x=a;y=b;z=c;p={};}void translation(Vector3 v){p=v;}bool invert(const Matrix& m){x=1/m.x;y=1/m.y;z=1/m.z;p={-m.p.x*x,-m.p.y*y,-m.p.z*z};return true;}};
void D3DXVec3TransformCoord(Vector3* out,const Vector3* p,const Matrix* m){*out={p->x*m->x+m->p.x,p->y*m->y+m->p.y,p->z*m->z+m->p.z};}
void D3DXVec3Cross(Vector3* out,const Vector3* a,const Vector3* b){*out={a->y*b->z-a->z*b->y,a->z*b->x-a->x*b->z,a->x*b->y-a->y*b->x};}
void D3DXVec3Normalize(Vector3* out,const Vector3* p){const float n=std::sqrt(p->x*p->x+p->y*p->y+p->z*p->z);*out=n?Vector3(p->x/n,p->y/n,p->z/n):Vector3();}
class BoundingBox{public:Vector3 low={-.5f,0,-.5f},high={.5f,0,.5f};bool insideOut()const{return low.x>high.x;}Vector3 minBounds()const{return low;}Vector3 maxBounds()const{return high;}void transformBy(const Matrix& m){D3DXVec3TransformCoord(&low,&low,&m);D3DXVec3TransformCoord(&high,&high,&m);}void setBounds(Vector3 a,Vector3 b){low=a;high=b;}};
class ClientSpace{public:bool dead=false,missing=false;float slope=.001f;unsigned queries=0;bool cleared()const{return dead;}float findTerrainHeight(Vector3 p){++queries;return missing?std::numeric_limits<float>::min():10.f+p.x*slope;}};
struct ID3DXEffect{bool inPass=false,committed=false;Vector4 colour;int SetVector(const char* name,const Vector4* v){assert(inPass&&std::string(name)=="g_color");colour=*v;return 0;}int CommitChanges(){assert(inPass);committed=true;return 0;}};
namespace Moo{
enum ERenderingPassType {RENDERING_PASS_COLOR,RENDERING_PASS_SHADOWS};
struct DeviceCallback{static std::vector<DeviceCallback*> callbacks;DeviceCallback(){callbacks.push_back(this);}virtual~DeviceCallback(){callbacks.erase(std::find(callbacks.begin(),callbacks.end(),this));}virtual bool recreateForD3DExDevice()const{return false;}virtual void deleteManagedObjects(){}virtual void deleteUnmanagedObjects(){}virtual void createManagedObjects(){}virtual void createUnmanagedObjects(){}static bool& getLocked(){static bool value=false;return value;}static void deleteAllUnmanaged(bool);static void createAllUnmanaged(bool);static void deleteAllManaged();static void createAllManaged();static void resetEx(){deleteAllUnmanaged(false);deleteAllManaged();createAllManaged();createAllUnmanaged(false);}};std::vector<DeviceCallback*> DeviceCallback::callbacks;
using CallbackList=std::vector<DeviceCallback*>;
struct DeviceCallbackStatics{struct Lock{void grab(){}void give(){}} lock;CallbackList* callbacks_=&DeviceCallback::callbacks;Lock* callbacksLock_=&lock;DeviceCallback* s_curCallback=nullptr;static DeviceCallbackStatics& instance(){static DeviceCallbackStatics value;return value;}};
struct VertexBuffer{std::shared_ptr<std::vector<unsigned char>> bytes=std::make_shared<std::vector<unsigned char>>();bool writeOnly=false;int create(uint32 size,int usage,int,int,const char*){bytes=std::make_shared<std::vector<unsigned char>>(size);writeOnly=(usage&D3DUSAGE_WRITEONLY)!=0;return 0;}int lock(uint32 offset,uint32 size,void** out,unsigned flags){assert(!((flags&D3DLOCK_READONLY)&&writeOnly));assert(offset+size<=bytes->size());*out=bytes->data()+offset;return 0;}int unlock(){return 0;}void release(){bytes->clear();}int set(unsigned,unsigned,unsigned stride)const;};
struct Indices{std::vector<unsigned>* data=nullptr;bool valid()const{return data!=nullptr;}unsigned& operator[](unsigned i){return (*data)[i];}};
struct IndexBuffer{std::vector<unsigned> data;int create(unsigned n,int,int,int,const char*){data.resize(n);return 0;}Indices lock(){return {&data};}int unlock(){return 0;}int set()const;void release(){data.clear();}};
struct VertexElement{struct StorageType{enum Value{FLOAT3,FLOAT4};};struct SemanticType{enum Value{POSITION};};};
struct VertexFormat{bool findElement(VertexElement::SemanticType::Value,unsigned,unsigned* stream,size_t* offset,VertexElement::StorageType::Value* type)const{*stream=0;*offset=0;*type=VertexElement::StorageType::FLOAT3;return true;}};
struct VertexDeclaration{VertexFormat f;const VertexFormat& format()const{return f;}};
struct Vertices{VertexBuffer vb;VertexDeclaration declaration;unsigned count=4,stride=20;unsigned nVertices()const{return count;}unsigned vertexStride()const{return stride;}VertexBuffer vertexBuffer(){return vb;}unsigned vertexBufferOffset(unsigned)const{return 0;}const VertexDeclaration* pDecl()const{return &declaration;}int setVertices(bool software){assert(!software);return 0;}};using VerticesPtr=Ptr<Vertices>;
struct IndexSource{std::vector<unsigned> data={0,1,2,0,2,3};unsigned size()const{return unsigned(data.size());}unsigned operator[](unsigned i)const{return data[i];}};
struct Primitive{IndexSource source;const IndexSource& indices(){return source;}int indicesFormat()const{return 16;}};using PrimitivePtr=Ptr<Primitive>;
struct EffectProperty{Ptr<EffectProperty> clone(){return new EffectProperty;}};
struct ManagedEffect{ID3DXEffect effect;ID3DXEffect* pEffect(){return &effect;}};
struct EffectMaterial{using Properties=std::map<unsigned,Ptr<EffectProperty>>;Properties props={{0,new EffectProperty}};ManagedEffect managed;Properties& properties(){return props;}ManagedEffect* pEffect(){return &managed;}bool begin(){return true;}unsigned numPasses()const{return 1;}bool beginPass(unsigned){managed.effect.inPass=true;managed.effect.committed=false;return true;}bool endPass(){managed.effect.inPass=false;return true;}bool end(){return true;}};using EffectMaterialPtr=Ptr<EffectMaterial>;
struct ComplexEffectMaterial{EffectMaterialPtr material=new EffectMaterial;ComplexEffectMaterial()=default;ComplexEffectMaterial(const ComplexEffectMaterial& p):material(new EffectMaterial(*p.material)){}EffectMaterialPtr baseMaterial()const{return material;}EffectMaterialPtr pass(ERenderingPassType){return material;}};using ComplexEffectMaterialPtr=Ptr<ComplexEffectMaterial>;
struct Visual{struct PrimitiveGroup{ComplexEffectMaterialPtr material_=new ComplexEffectMaterial;};struct Geometry{VerticesPtr vertices_=new Vertices;PrimitivePtr primitives_=new Primitive;std::vector<PrimitiveGroup> primitiveGroups_={PrimitiveGroup()};};struct RenderSet{std::vector<Geometry> geometry_={Geometry()};};std::vector<RenderSet> sets={RenderSet()};struct ReadGuard{explicit ReadGuard(Visual*){}};const std::vector<RenderSet>& renderSets()const{return sets;}BoundingBox box;BoundingBox boundingBox()const{return box;}};using VisualPtr=Ptr<Visual>;
struct VisualManager{VisualPtr visual=new Visual;static VisualManager* instance(){static VisualManager value;return &value;}VisualPtr get(const char* name){return std::string(name)=="missing"?VisualPtr():visual;}};
struct EffectVisualContext{const Matrix* world=nullptr;void worldMatrix(const Matrix* v){world=v;}};
struct RC{bool deviceAlive=true;unsigned draws=0,count=0,expectedVertices=4;Matrix matrix;EffectVisualContext effect;const VertexBuffer* vb=nullptr;const IndexBuffer* ib=nullptr;bool usingD3DDeviceEx()const{return true;}void* device(){return deviceAlive?this:nullptr;}void push(){}void pop(){}void world(const Matrix& m){matrix=m;}EffectVisualContext& effectVisualContext(){return effect;}unsigned drawIndexedPrimitive(int type,int,unsigned,unsigned vertices,unsigned,unsigned triangles){assert(type==D3DPT_TRIANGLELIST&&vertices==expectedVertices&&vb&&ib&&triangles==ib->data.size()/3);++draws;count=triangles;return 0;}};
RC& rc(){static RC value;return value;}
@SDK_DEVICE_CALLBACK_DISPATCH@
int VertexBuffer::set(unsigned stream,unsigned offset,unsigned stride)const{assert(stream==0&&offset==0&&stride>=12);rc().vb=this;return 0;}int IndexBuffer::set()const{rc().ib=this;return 0;}
struct DrawContext{enum ChannelMask{TRANSPARENT_CHANNEL_MASK=2};struct UserDrawItem{virtual~UserDrawItem()=default;virtual void draw()=0;virtual void fini(){}};std::vector<UserDrawItem*> queue;ERenderingPassType pass=RENDERING_PASS_COLOR;ERenderingPassType renderingPassType()const{return pass;}void drawUserItem(UserDrawItem* p,ChannelMask mask){assert(mask==TRANSPARENT_CHANNEL_MASK);queue.push_back(p);}void flush(){auto old=queue;queue.clear();for(auto* p:old){p->draw();p->fini();}}};
}
class SceneDrawContext{public:Moo::DrawContext context;Moo::DrawContext& drawContext()const{return const_cast<Moo::DrawContext&>(context);}};
struct Water{mutable unsigned calls=0;float stockGroundRayDistance(const Vector3& start,const Vector3& end)const{++calls;assert(start.y==-1000&&end.y==1000);return -1.f;}};
struct Waters{static std::vector<Water*> surfaces;static const std::vector<Water*>& instance(){return surfaces;}};std::vector<Water*> Waters::surfaces;
}
#include "stock_control_point_radius.cpp"

int main(int argc,char** argv)
{
    using namespace BW;
    auto source = Moo::VisualManager::instance()->visual;
    if(argc==3)
    {
        std::ifstream input(argv[1],std::ios::binary);
        std::ofstream output(argv[2],std::ios::binary);
        unsigned header[3];float values[7];
        input.read(reinterpret_cast<char*>(header),sizeof(header));input.read(reinterpret_cast<char*>(values),sizeof(values));
        auto& geometry=source->sets[0].geometry_[0];
        geometry.vertices_->stride=header[0];geometry.vertices_->count=header[1];
        source->box.low={values[0],values[1],values[2]};source->box.high={values[3],values[4],values[5]};
        auto& bytes=*geometry.vertices_->vb.bytes;bytes.resize(header[0]*header[1]);
        input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        geometry.primitives_->source.data.resize(header[2]);
        input.read(reinterpret_cast<char*>(geometry.primitives_->source.data.data()),header[2]*4);assert(input);
        ClientSpace space;space.slope=values[6];Moo::rc().expectedVertices=header[1];
        auto radius=loadStockControlPointRadius("radius",&space,{100,20,200},50,.2f,{1,1,1,1});assert(radius&&radius->project());
        output.write(reinterpret_cast<const char*>(radius->vertexBuffer.bytes->data()),radius->vertexBuffer.bytes->size());
        output.write(reinterpret_cast<const char*>(radius->indexBuffer.data.data()),radius->indexBuffer.data.size()*4);
        return 0;
    }
    assert(argc==1);
    const float mesh[][5]={{-.5f,0,-.5f,0,0},{.5f,0,-.5f,1,0},{.5f,0,.5f,1,1},{-.5f,0,.5f,0,1}};
    auto& vertices=source->sets[0].geometry_[0].vertices_->vb;
    vertices.bytes->resize(sizeof(mesh));std::memcpy(vertices.bytes->data(),mesh,sizeof(mesh));
    const auto authored=*vertices.bytes;
    auto space=std::make_unique<ClientSpace>();Water water;Waters::surfaces.push_back(&water);
    SceneDrawContext context;
    assert(!loadStockControlPointRadius("missing",space.get(),{100,20,200},50,.2f,{1,1,1,1}));
    auto radius=loadStockControlPointRadius("radius",space.get(),{100,20,200},50,.2f,{.1f,.2f,.3f,.4f});
    assert(radius&&radius->source.empty()&&radius->bounds.minBounds().y==20);
    drawStockControlPointRadius(radius,context);assert(context.context.queue.empty()&&space->queries==0);
    BoundingBox projectedBounds;
    assert(prepareStockControlPointRadiusBounds(radius,projectedBounds));
    assert(projectedBounds.minBounds().y<11&&projectedBounds.maxBounds().y>=20);
    assert(space->queries==4&&radius->vertexBuffer.bytes->empty()&&!radius->projected);
    drawStockControlPointRadius(radius,context);assert(context.context.queue.size()==1&&space->queries==4);
    context.context.flush();assert(space->queries==4&&water.calls==4&&Moo::rc().draws==1&&Moo::rc().count==2);
    assert(*vertices.bytes==authored&&radius->source==authored&&radius->projected);
    for(unsigned i=0;i<4;++i){Vector3 projected;std::memcpy(&projected,radius->vertexBuffer.bytes->data()+i*20,12);assert(std::fabs(projected.y-(10.2f+(mesh[i][0]*100.f+100.f)*.001f))<1e-5f);assert(std::memcmp(radius->vertexBuffer.bytes->data()+i*20+12,&mesh[i][3],8)==0);}
    assert(radius->material->baseMaterial()->managed.effect.colour.w==.4f);
    Moo::DeviceCallback::resetEx();assert(!radius->projected);
    drawStockControlPointRadius(radius,context);context.context.flush();assert(space->queries==4&&Moo::rc().draws==2);
    space->slope=1;
    auto steep=loadStockControlPointRadius("radius",space.get(),{100,20,200},50,.2f,{1,1,1,1});
    assert(prepareStockControlPointRadiusBounds(steep,projectedBounds));
    drawStockControlPointRadius(steep,context);context.context.flush();assert(steep->projected&&steep->indexCount==0&&Moo::rc().draws==2);
    space->missing=true;
    auto pending=loadStockControlPointRadius("radius",space.get(),{100,20,200},50,.2f,{1,1,1,1});
    assert(!prepareStockControlPointRadiusBounds(pending,projectedBounds));
    drawStockControlPointRadius(pending,context);assert(context.context.queue.empty()&&!pending->projected);
    space->missing=false;space->slope=.001f;
    assert(prepareStockControlPointRadiusBounds(pending,projectedBounds));
    drawStockControlPointRadius(pending,context);context.context.flush();assert(pending->projected&&Moo::rc().draws==3);
    steep.reset();pending.reset();
    drawStockControlPointRadius(radius,context);std::weak_ptr<StockControlPointRadius> weak=radius;
    retireStockControlPointRadius(radius);radius.reset();space.reset();assert(!weak.expired());context.context.flush();assert(weak.expired());
    assert(Moo::DeviceCallback::callbacks.empty());
}
