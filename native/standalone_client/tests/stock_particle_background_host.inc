// Scheduler and resource I/O boundaries. Background loading runs on a joined
// worker; completion and Python ownership execute on the submitting thread.
#include <algorithm>
#include <thread>
unsigned backgroundLoads=0,backgroundScriptErrors=0;
class StringBuilder {
    std::string text_;
public:
    StringBuilder(char*,unsigned){}
    unsigned length()const{return unsigned(text_.size());}
    const char* string()const{return text_.c_str();}
    void message(const char* text){text_=text;}
};
bool MetaParticleSystem::load(const BW::string& filename,const char*,StringBuilder* error){
    ++backgroundLoads;
    if(filename=="missing")return false;
    if(filename=="warning")error->message("intentional resource error");
    auto* system=new ParticleSystem(128);system->name_=filename;
    system->actions_.push_back(new Action());system->pRenderer(new Renderer());addSystem(system);return true;
}
class TaskManager;
class BackgroundTask {
public:
    explicit BackgroundTask(const char*){}
    virtual ~BackgroundTask()=default;
    virtual void doBackgroundTask(TaskManager&)=0;
    virtual void doMainThreadTask(TaskManager&)=0;
};
class TaskManager {
public:
    std::vector<SmartPointer<BackgroundTask>> owners;
    std::vector<BackgroundTask*> pending,ready;
    void addBackgroundTask(SmartPointer<BackgroundTask> task){owners.push_back(task);pending.push_back(task.getObject());}
    void addMainThreadTask(BackgroundTask* task){ready.push_back(task);}
    void background(){
        std::vector<BackgroundTask*> work;work.swap(pending);
        std::thread worker([this,&work](){for(auto* task:work)task->doBackgroundTask(*this);});
        worker.join();
    }
    void complete(){
        while(!ready.empty()){
            auto* selected=ready.front();ready.erase(ready.begin());SmartPointer<BackgroundTask> held;
            for(auto& owner:owners)if(owner.getObject()==selected){held=owner;break;}
            assert(held);selected->doMainThreadTask(*this);
            owners.erase(std::remove_if(owners.begin(),owners.end(),[selected](const SmartPointer<BackgroundTask>& value){
                return value.getObject()==selected;}),owners.end());
        }
    }
    void cancel(){pending.clear();ready.clear();owners.clear();}
};
struct FileIOTaskManager {static TaskManager& instance(){static TaskManager value;return value;}};
namespace Script {
bool isFinalised(){return false;}
PyObject* ask(PyObject*,PyObject*,const char*,bool=false,bool=true);
bool call(PyObject*,PyObject*,const char* = "",bool=false);
}
#define MEMTRACKER_SCOPED(value)
#undef ERROR_MSG
#define ERROR_MSG(...) (++backgroundScriptErrors,std::fprintf(stderr,__VA_ARGS__))
@SCRIPT_ASK@
namespace Script {
@SCRIPT_CALL@
}
#undef ERROR_MSG
#define ERROR_MSG(...) std::fprintf(stderr,__VA_ARGS__)
@BACKGROUND_CLASS@
using MPSBackgroundTaskPtr=SmartPointer<MPSBackgroundTask>;
@BACKGROUND_CONSTRUCTOR@
@BACKGROUND_LOAD@
@BACKGROUND_COMPLETE@
@BACKGROUND_FACTORY@
static PyObject* submitBackground(PyObject*,PyObject* args){
    const char* name;PyObject* callback;if(!PyArg_ParseTuple(args,"sO",&name,&callback))return NULL;
    return PyMetaParticleSystem::createBG(name,PyObjectPtr(callback));
}
static PyObject* loadBackground(PyObject*,PyObject*){FileIOTaskManager::instance().background();Py_RETURN_NONE;}
static PyObject* completeBackground(PyObject*,PyObject*){FileIOTaskManager::instance().complete();Py_RETURN_NONE;}
static PyObject* cancelBackground(PyObject*,PyObject*){FileIOTaskManager::instance().cancel();Py_RETURN_NONE;}
static PyObject* backgroundStatus(PyObject*,PyObject*){
    auto& manager=FileIOTaskManager::instance();return Py_BuildValue("(IIIII)",
        backgroundLoads,unsigned(manager.owners.size()),unsigned(manager.pending.size()),unsigned(manager.ready.size()),backgroundScriptErrors);
}
