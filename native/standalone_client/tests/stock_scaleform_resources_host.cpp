std::atomic<int> BW::BinaryBlock::alive{0};
BW::ResourceFixture BW::BWResource::fixture;
BW::ScaleformBW::Manager* BW::ScaleformBW::Manager::current=nullptr;
int Scaleform::SysFile::created=0;
namespace BW {
static ScaleformBW::Manager manager;
static std::unique_ptr<ScaleformBW::Loader> loader;
static PyObject* start(PyObject*,PyObject*){
 if(loader)loader.reset();
 auto*p=new ScaleformBW::FileOpener();loader.reset(new ScaleformBW::Loader(p));p->Release();
 manager.loader=loader.get();ScaleformBW::Manager::current=&manager;Py_RETURN_NONE;
}
static PyObject* stop(PyObject*,PyObject*){manager.loader=nullptr;ScaleformBW::Manager::current=nullptr;loader.reset();Py_RETURN_NONE;}
static PyObject* exercise(PyObject*,PyObject*){
 auto&f=BWResource::fixture;auto*p=manager.pLoader()->opener.GetPtr();
 f.files.clear();f.sections.clear();f.absolute.clear();f.reads.clear();f.purged.clear();
 f.files["gui/a/../b.bin"]="first";f.sections["gui/b.bin"]="fallback";
 auto*a=p->OpenFile("GUI\\A\\..\\B.bin",3,0666);assert(a);
 assert(f.reads==std::vector<string>{"fs:gui/a/../b.bin"});
 unsigned char bytes[8]={};assert(a->Read(bytes,5)==5&&std::string(reinterpret_cast<char*>(bytes),5)=="first");a->Release();
 f.files.clear();f.reads.clear();a=p->OpenFile("GUI\\A\\..\\B.bin",0,0);assert(a);
 assert((f.reads==std::vector<string>{"fs:gui/a/../b.bin","ds:gui/b.bin"}));a->Release();
 assert(p->stockPrecache("gui/a/../b.bin",false));auto count=f.reads.size();
 f.sections["gui/b.bin"]="changed";assert(p->stockPrecache("gui/a/../b.bin",false));assert(f.reads.size()==count);
 a=p->OpenFile("gui/a/../b.bin",0,0);auto*b=p->OpenFile("gui/a/../b.bin",0,0);assert(a&&b);
 assert(std::string(a->GetFilePath())=="gui/a/../b.bin");assert(a->Read(bytes,2)==2&&b->Read(bytes,8)==8);
 assert(p->stockPrecache("gui/a/../b.bin",true));auto*c=p->OpenFile("gui/a/../b.bin",0,0);assert(c->Read(bytes,7)==7&&std::string(reinterpret_cast<char*>(bytes),7)=="changed");c->Release();
 f.sections.clear();assert(!p->stockPrecache("gui/a/../b.bin",true));c=p->OpenFile("gui/a/../b.bin",0,0);assert(c);c->Release();
 p->stockEraseCached("gui/a/../b.bin");assert(f.purged.back()=="gui/a/../b.bin");assert(!p->OpenFile("gui/a/../b.bin",0,0));
 assert(a->Seek(0)==0&&a->Read(bytes,8)==8&&std::string(reinterpret_cast<char*>(bytes),8)=="fallback");
 a->Release();b->Release();assert(BinaryBlock::alive==0);
 f.files["one"]="1";f.files["two"]="2";assert(p->stockPrecache("one",false)&&p->stockPrecache("two",false));
 p->stockEraseCached("");assert(BinaryBlock::alive==0&&f.purged.size()==3);
 p->stockAddStreamingFile("pak-only");p->stockEnableStreaming();f.files["pak-only"]="not-a-disk-file";
 count=f.reads.size();assert(!p->OpenFile("pak-only",0,0));assert(f.reads.size()==count);
 f.absolute["video"]="C:/fixture/video";p->stockAddStreamingFile("video");a=p->OpenFile("video",0,0);assert(a&&dynamic_cast<Scaleform::SysFile*>(a));a->Release();
 p->stockDisableStreaming();a=p->OpenFile("pak-only",0,0);assert(a&&dynamic_cast<Scaleform::MemoryFile*>(a));a->Release();
 p->stockEnableStreaming();a=p->OpenFile("pak-only",0,0);assert(a&&dynamic_cast<Scaleform::MemoryFile*>(a));a->Release();
 f.sections["gui/maps/actual.png"]="original";assert(p->stockPrecache("gui/maps/actual.png",false));
 a=p->OpenFile("gui/maps/actual.png",0,0);assert(a);loader.reset();manager.loader=nullptr;
 assert(a->Read(bytes,8)==8&&std::string(reinterpret_cast<char*>(bytes),8)=="original");a->Release();assert(BinaryBlock::alive==0);
 Py_RETURN_NONE;
}
#define EXPORT(name) static PyObject* wrap_##name(PyObject*,PyObject*args){return py_##name(args);}
EXPORT(enableStreaming) EXPORT(disableStreaming) EXPORT(wg_precacheScaleformResource) EXPORT(wg_eraseScaleformResFromCache) EXPORT(resetScaleformResources)
static PyObject*reloads(PyObject*,PyObject*){return PyInt_FromLong(manager.reloads);}
static PyMethodDef exports[]={
 {"enableStreaming",wrap_enableStreaming,METH_VARARGS,NULL},{"disableStreaming",wrap_disableStreaming,METH_VARARGS,NULL},
 {"wg_precacheScaleformResource",wrap_wg_precacheScaleformResource,METH_VARARGS,NULL},
 {"wg_eraseScaleformResFromCache",wrap_wg_eraseScaleformResFromCache,METH_VARARGS,NULL},
 {"resetScaleformResources",wrap_resetScaleformResources,METH_VARARGS,NULL},
 {"_start",start,METH_NOARGS,NULL},{"_stop",stop,METH_NOARGS,NULL},{"_exercise",exercise,METH_NOARGS,NULL},{"_reloads",reloads,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
}
PyMODINIT_FUNC initresource_owner(){Py_InitModule("resource_owner",BW::exports);}
