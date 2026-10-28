#include <Python.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#define BW_BEGIN_NAMESPACE
#define BW_END_NAMESPACE
#define WOT_STOCK_FLASH_BANG_PORTABLE
#define FAILED(v) ((v)<0)
#define SUCCEEDED(v) ((v)>=0)
#define ERROR_MSG(...)
#define PY_AUTO_MODULE_FUNCTION(...)
using uint32=unsigned;using LONG=long;using HRESULT=int;
enum {D3DFMT_A8R8G8B8=21,D3DPT_TRIANGLESTRIP=5,D3DTEXF_LINEAR=2};
struct RECT {LONG left,top,right,bottom;};
struct Vector2 {float x,y;Vector2(float a=0,float b=0):x(a),y(b){}};
struct Vector4 {float x,y,z,w;Vector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}};
using D3DXVECTOR4=Vector4;
void XPVec4Lerp(Vector4* out,const Vector4* a,const Vector4* b,float t){
    *out=Vector4(a->x+(b->x-a->x)*t,a->y+(b->y-a->y)*t,a->z+(b->z-a->z)*t,a->w+(b->w-a->w)*t);
}
struct Vector4Provider {virtual ~Vector4Provider(){}virtual void output(Vector4&)=0;};
using Vector4ProviderPtr=std::shared_ptr<Vector4Provider>;
struct Constant:Vector4Provider {Vector4 value;explicit Constant(Vector4 v):value(v){}void output(Vector4& v)override{v=value;}};
struct Vector4Animation:Vector4Provider {
    using Keyframe=std::pair<float,Vector4ProviderPtr>;using Keyframes=std::vector<Keyframe>;
    Keyframes keyframes_;float time_=0,duration_=1;PyObject* callback=nullptr;
    ~Vector4Animation(){Py_XDECREF(callback);}
    void tick(float);void output(Vector4&)override;
};
@ANIMATION@
template<class T>struct ComObjectWrap {
    T* value=nullptr;operator void*()const{return value;}T** operator&(){return &value;}
    T* pComObject()const{return value;}
};
namespace DX {
struct Surface {unsigned id;};
struct Viewport {unsigned X=0,Y=0,Width=1280,Height=720;float MinZ=0,MaxZ=1;};
}
struct Texture {unsigned id,refs=1;};
bool failSelect=false,failBegin=false,failPass=false,failCopy=false,failLoad=false,failTarget=false;
struct ID3DXEffect {
    std::map<std::string,float> scalars;Texture* texture=nullptr;Vector4 colour;
    ID3DXEffect(){scalars["outputAlphaType"]=2;scalars["outputColourType"]=0;}
    ~ID3DXEffect(){if(texture)--texture->refs;}
    int SetVector(const char* name,const Vector4* v){assert(!strcmp(name,"diffuseColor"));colour=*v;return 0;}
    int SetBool(const char* n,bool v){scalars[n]=v;return 0;}
    int SetInt(const char* n,int v){scalars[n]=float(v);return 0;}
    int SetFloat(const char* n,float v){scalars[n]=v;return 0;}
    int SetTexture(const char* n,Texture* t){assert(!strcmp(n,"colorTexture"));if(t)++t->refs;if(texture)--texture->refs;texture=t;return 0;}
};
using D3DXHANDLE=const char*;
namespace Moo {
struct VertexTLUV {Vector4 pos_;uint32 colour_;Vector2 uv_;static int fvf(){return 0x144;}};
struct DeviceCallback {virtual ~DeviceCallback(){}virtual void createManagedObjects(){}virtual void deleteManagedObjects(){}virtual void deleteUnmanagedObjects(){}};
struct Effect {ID3DXEffect definition,binding;ID3DXEffect* selected=&definition;ID3DXEffect* pEffect(){return selected;}};
struct EffectMaterial {
    std::shared_ptr<Effect> effect=std::make_shared<Effect>();std::string technique;bool begun=false,pass=false;
    bool privateDefaults=false,doubleSided=false;
    void replaceDefaults(){privateDefaults=true;}
    bool setProperty(const char* name,bool v,bool create){assert(privateDefaults&&!strcmp(name,"doubleSided")&&!create);doubleSided=v;return true;}
    struct Properties {ID3DXEffect* binding=nullptr;bool select(Effect& effect){if(failSelect)return false;binding=&effect.binding;effect.selected=binding;return true;}ID3DXEffect* existingStockBinding(){return binding;}} properties_;
    Properties& properties(){return properties_;}std::shared_ptr<Effect> pEffect(){return effect;}
    bool setTechnique(const char* name){technique=name;return true;}
    bool begin(){assert(!begun);if(failBegin)return false;assert(properties_.select(*effect));effect->binding.SetBool("doubleSided",doubleSided);begun=true;return true;}
    uint32 numPasses(){return 1;}
    bool beginPass(uint32){assert(begun&&!pass);if(failPass)return false;pass=true;return true;}
    bool endPass(){assert(pass);pass=false;return true;}
    bool end(){assert(begun&&!pass);begun=false;effect->selected=&effect->definition;return true;}
};
using EffectMaterialPtr=std::shared_ptr<EffectMaterial>;
struct RenderTarget {Texture texture{3};DX::Surface surface{3};~RenderTarget(){assert(texture.refs==1);}Texture* pTexture(){return &texture;}int pSurface(ComObjectWrap<DX::Surface>& v){v.value=&surface;return 0;}};
using RenderTargetPtr=std::shared_ptr<RenderTarget>;
EffectMaterial* drawnMaterial=nullptr;
bool createEffect(EffectMaterialPtr& m,const char* n){assert(!strcmp(n,"system/materials/geometrics.fx"));m=std::make_shared<EffectMaterial>();drawnMaterial=m.get();return !failLoad;}
bool createRenderTarget(RenderTargetPtr& r,int w,int h,int format,const char* n,bool a,bool b){assert(w==512&&h==512&&format==21&&!strcmp(n,"FlashBang")&&!a&&b);r=std::make_shared<RenderTarget>();return !failTarget;}
struct Draw {std::string technique;uint32 colour;bool previous;float alphaType,colourType;Vector2 corner;};
std::vector<Draw> draws;unsigned copies=0;
struct Device {
    DX::Surface source{1};DX::Viewport viewport;
    int GetRenderTarget(unsigned slot,DX::Surface** v){assert(slot==0);*v=&source;return 0;}
    int GetViewport(DX::Viewport* v){*v=viewport;return 0;}
    int StretchRect(DX::Surface* a,const RECT* crop,DX::Surface* b,void* rect,int mode){
        assert(a==&source&&b&&crop->right==LONG(viewport.X+viewport.Width)&&crop->bottom==LONG(viewport.Y+viewport.Height)&&!rect&&mode==2);
        if(failCopy)return -1;++copies;return 0;
    }
};
struct Context {
    Device device_;int format=0;float screenWidth(){return 1280;}float screenHeight(){return 720;}
    Device* device(){return &device_;}void setFVF(int f){format=f;}void setTexture(unsigned slot,void* t){assert(slot==0&&!t);}
    int drawPrimitiveUP(int primitive,int count,const void* data,unsigned stride){
        assert(primitive==5&&count==2&&stride==28&&format==0x144);
        auto* m=drawnMaterial;assert(m&&m->begun&&m->pass&&m->pEffect()->selected==&m->effect->binding);
        auto* effect=m->pEffect()->pEffect();auto* vertices=static_cast<const VertexTLUV*>(data);
        assert(effect->scalars["blendMode"]==2&&effect->scalars["zTestDisable"]==1&&effect->scalars["zWriteEnable"]==0);
        assert(effect->scalars["doubleSided"]==1&&m->privateDefaults);
        assert(effect->colour.x==1&&effect->colour.w==1);
        assert(vertices[0].pos_.x==-2&&vertices[0].pos_.y==-2&&vertices[3].pos_.w==1);
        assert(vertices[0].uv_.x==0&&vertices[3].uv_.x==1&&vertices[3].uv_.y==1);
        draws.push_back({m->technique,vertices[0].colour_,bool(effect->texture),effect->scalars["outputAlphaType"],effect->scalars["outputColourType"],Vector2(vertices[3].pos_.x,vertices[3].pos_.y)});
        return 0;
    }
};
Context& rc(){static Context context;return context;}
}
@HEADER@
struct IRendererPipeline {std::shared_ptr<StockFlashBangSupport> owner;std::shared_ptr<StockFlashBangSupport> stockFlashBangSupport(){return owner;}};
struct Renderer {
    IRendererPipeline* current=nullptr;static Renderer* selected;
    static Renderer* pInstance(){return selected;}IRendererPipeline* pipeline(){return current;}
};
Renderer* Renderer::selected=nullptr;
#include "stock_flash_bang.cpp"
struct Animation:PyObject {Vector4Animation* native;};
PyTypeObject animationType={PyObject_HEAD_INIT(NULL) 0};
std::vector<Animation*> animations;
int readVector(PyObject* object,Vector4& out){return PyArg_ParseTuple(object,"ffff",&out.x,&out.y,&out.z,&out.w)?0:-1;}
PyObject* newAnimation(PyTypeObject*,PyObject*,PyObject*){
    auto* a=PyObject_New(Animation,&animationType);if(!a)return NULL;a->native=new Vector4Animation;animations.push_back(a);return a;
}
void deleteAnimation(PyObject* object){auto* a=static_cast<Animation*>(object);animations.erase(std::find(animations.begin(),animations.end(),a));delete a->native;PyObject_Del(a);}
int setFrames(PyObject* o,PyObject* v,void*){
    auto* a=static_cast<Animation*>(o);PyObject* sequence=PySequence_Fast(v,"Expected keyframes");if(!sequence)return -1;
    Vector4Animation::Keyframes frames;
    for(Py_ssize_t i=0;i<PySequence_Fast_GET_SIZE(sequence);++i){PyObject* row=PySequence_Fast_GET_ITEM(sequence,i);PyObject* value;float time;Vector4 vector;
        if(!PyArg_ParseTuple(row,"fO",&time,&value)||readVector(value,vector)){Py_DECREF(sequence);return -1;}
        frames.emplace_back(time,std::make_shared<Constant>(vector));}
    a->native->keyframes_=frames;Py_DECREF(sequence);return 0;
}
int setDuration(PyObject* o,PyObject* v,void*){float d=float(PyFloat_AsDouble(v));if(PyErr_Occurred())return -1;static_cast<Animation*>(o)->native->duration_=d;return 0;}
namespace Script {
int setData(PyObject* o,Vector4ProviderPtr& out,const char*){
    if(o==Py_None){out.reset();return 0;}
    Vector4 value;
    if(PyTuple_Check(o)&&readVector(o,value)==0){out=std::make_shared<Constant>(value);return 0;}
    PyErr_Clear();
    if(!PyObject_TypeCheck(o,&animationType)){PyErr_SetString(PyExc_TypeError,"Expected Vector4Provider");return -1;}
    Py_INCREF(o);out=Vector4ProviderPtr(static_cast<Animation*>(o)->native,[o](Vector4Provider*){Py_DECREF(o);});return 0;
}
}
PyObject* wrapper(PyObject* args,bool remove){PyObject* input;Vector4ProviderPtr provider;if(!PyArg_UnpackTuple(args,remove?"removeFlashBangAnimation":"flashBangAnimation",1,1,&input)||Script::setData(input,provider,"argument 1"))return NULL;if(remove)removeFlashBangAnimation(provider);else flashBangAnimation(provider);Py_RETURN_NONE;}
PyObject* add(PyObject*,PyObject* args){return wrapper(args,false);}PyObject* remove(PyObject*,PyObject* args){return wrapper(args,true);}
Renderer renderer;IRendererPipeline pipeline;
PyObject* fixture(PyObject*,PyObject* args){
    const char* command;double dt=0;if(!PyArg_ParseTuple(args,"s|d",&command,&dt))return NULL;
    if(!strcmp(command,"tick")){for(auto* a:animations)a->native->tick(float(dt));auto owner=activeFlashBang();if(owner)owner->tick();}
    else if(!strcmp(command,"draw")){auto owner=activeFlashBang();if(owner&&!owner->draw()){Py_RETURN_FALSE;}}
    else if(!strcmp(command,"close"))finiStockFlashBangAnimations();
    else if(!strcmp(command,"open"))initStockFlashBangAnimations();
    else if(!strcmp(command,"deviceLoss")){pipeline.owner->deleteUnmanagedObjects();}
    else if(!strcmp(command,"managedLoss")){pipeline.owner->deleteUnmanagedObjects();pipeline.owner->deleteManagedObjects();}
    else if(!strcmp(command,"fail")){failSelect=dt==1;failBegin=dt==2;failPass=dt==3;failCopy=dt==4;failLoad=dt==5;failTarget=dt==6;}
    else if(!strcmp(command,"absent"))renderer.current=nullptr;
    else if(!strcmp(command,"present"))renderer.current=&pipeline;
    else if(!strcmp(command,"stats")){if(Moo::draws.empty())return Py_BuildValue("(IIsI)",0,Moo::copies,"",0);const auto& d=Moo::draws.back();return Py_BuildValue("(IIsI)",unsigned(Moo::draws.size()),Moo::copies,d.technique.c_str(),d.colour);}
    Py_RETURN_TRUE;
}
struct HookProvider:Vector4Provider {
    std::function<void()> sample,finalise;
    ~HookProvider(){if(finalise)finalise();}
    void output(Vector4& value)override{if(sample)sample();value=Vector4(1,1,1,1);}
};
void checkReentry(){
    initStockFlashBangAnimations();
    auto owner=std::make_shared<StockFlashBangSupport>();
    auto provider=std::make_shared<HookProvider>();std::weak_ptr<HookProvider> weak=provider;
    owner->add(provider);owner->add(provider);owner->remove(provider);provider.reset();assert(!weak.expired());
    owner->close();assert(weak.expired());owner->reopen();
    auto replacement=std::make_shared<Constant>(Vector4(1,1,1,1));
    provider=std::make_shared<HookProvider>();std::weak_ptr<HookProvider> selected=provider;
    provider->sample=[&]{owner->remove(selected.lock());owner->add(replacement);};
    owner->add(provider);const auto count=Moo::draws.size();owner->tick();
    assert(owner->draw()&&Moo::draws.size()==count); // Re-entered tick cannot publish its stale sample.
    owner->tick();assert(owner->draw()&&Moo::draws.size()==count+1);
    owner->close();owner->reopen();
    provider=std::make_shared<HookProvider>();
    provider->sample=[&]{owner->close();owner->add(replacement);};
    owner->add(provider);owner->tick();assert(owner->draw()&&Moo::draws.size()==count+1);
    owner->reopen();
    provider=std::make_shared<HookProvider>();
    bool finalised=false;provider->finalise=[&]{finalised=true;owner->add(replacement);};
    owner->add(provider);provider.reset();owner->close();assert(finalised);
    owner->reopen();owner->tick();assert(owner->draw()&&Moo::draws.size()==count+1);
    // A pipeline can be replaced by provider output. The caller's local pin
    // keeps the old owner alive until its generation guard observes close.
    auto old=pipeline.owner;old->reopen();provider=std::make_shared<HookProvider>();
    provider->sample=[&]{old->close();pipeline.owner=std::make_shared<StockFlashBangSupport>();};
    old->add(provider);old->tick();assert(old->draw()&&Moo::draws.size()==count+1);
    old->close();owner->close();
}
int main(int argc,char**argv){
    assert(argc==2);Py_Initialize();animationType.tp_name="Math.Vector4Animation";animationType.tp_basicsize=sizeof(Animation);animationType.tp_flags=Py_TPFLAGS_DEFAULT;animationType.tp_new=newAnimation;animationType.tp_dealloc=deleteAnimation;
    PyGetSetDef attributes[]={{const_cast<char*>("keyframes"),NULL,setFrames,NULL,NULL},{const_cast<char*>("duration"),NULL,setDuration,NULL,NULL},{NULL,NULL,NULL,NULL,NULL}};
    animationType.tp_getset=attributes;assert(PyType_Ready(&animationType)==0);
    PyMethodDef methods[]={{"flashBangAnimation",add,METH_VARARGS,NULL},{"removeFlashBangAnimation",remove,METH_VARARGS,NULL},{"fixture",fixture,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
    assert(Py_InitModule("BigWorld",methods));auto* math=Py_InitModule("Math",NULL);Py_INCREF(&animationType);assert(!PyModule_AddObject(math,"Vector4Animation",reinterpret_cast<PyObject*>(&animationType)));
    Renderer::selected=&renderer;renderer.current=&pipeline;pipeline.owner=std::make_shared<StockFlashBangSupport>();
    FILE* input=fopen(argv[1],"r");assert(input);int result=PyRun_SimpleFile(input,argv[1]);fclose(input);
    checkReentry();finiStockFlashBangAnimations();pipeline.owner.reset();Py_Finalize();return result?1:0;
}
