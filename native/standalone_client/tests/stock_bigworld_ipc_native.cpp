// Actual production binding, CPython 2.7.7 and Win32 kernel acceptance probe.
// Build against the production client headers/libraries with MSVC Win32.
#include "pch.hpp"
#include "stock_bigworld_ipc.cpp"
#include "cstdmf/allocator.hpp"
#include "cstdmf/log_msg.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace
{
void require(bool value,const char* detail)
{
    if (!value) throw std::runtime_error(detail);
}
void python(const char* code)
{
    require(PyRun_SimpleString(code)==0,"Production Python Mutex contract failed");
}
struct WaitFixture
{
    const char* name;
    HANDLE ready,release,attempting,entered;
    bool ownerSucceeded=false;
};
DWORD WINAPI kernelOwner(void* argument)
{
    auto& fixture=*static_cast<WaitFixture*>(argument);
    wot::StockIpcMutex mutex;std::uint32_t error=0;
    fixture.ownerSucceeded=mutex.create(fixture.name,error)&&
        mutex.acquire(0)==wot::StockIpcMutex::WAIT_RESULT_ACQUIRED;
    SetEvent(fixture.ready);
    WaitForSingleObject(fixture.release,10000);
    if (fixture.ownerSucceeded) fixture.ownerSucceeded=mutex.release(error);
    return 0;
}
DWORD WINAPI pythonContender(void* argument)
{
    auto& fixture=*static_cast<WaitFixture*>(argument);
    SetEvent(fixture.attempting);
    const PyGILState_STATE state=PyGILState_Ensure();
    SetEvent(fixture.entered);
    PyGILState_Release(state);
    return 0;
}
void actualGilWait(const char* name)
{
    WaitFixture fixture={name,CreateEventA(NULL,TRUE,FALSE,NULL),
        CreateEventA(NULL,TRUE,FALSE,NULL),CreateEventA(NULL,TRUE,FALSE,NULL),
        CreateEventA(NULL,TRUE,FALSE,NULL)};
    require(fixture.ready&&fixture.release&&fixture.attempting&&fixture.entered,"Cannot create GIL events");
    HANDLE owner=CreateThread(NULL,0,kernelOwner,&fixture,0,NULL);
    require(owner&&WaitForSingleObject(fixture.ready,10000)==WAIT_OBJECT_0&&fixture.ownerSucceeded,
        "Native helper did not own mutex");
    HANDLE contender=CreateThread(NULL,0,pythonContender,&fixture,0,NULL);
    require(contender&&WaitForSingleObject(fixture.attempting,10000)==WAIT_OBJECT_0,
        "Native contender did not reach GIL boundary");
    const bool timedOut=PyRun_SimpleString("assert mutex.acquire(0.025) is False\n")==0;
    const bool held=WaitForSingleObject(fixture.entered,0)==WAIT_TIMEOUT;
    PyThreadState* saved=PyEval_SaveThread();
    const bool admitted=WaitForSingleObject(fixture.entered,10000)==WAIT_OBJECT_0;
    const bool finished=WaitForSingleObject(contender,10000)==WAIT_OBJECT_0;
    PyEval_RestoreThread(saved);
    SetEvent(fixture.release);
    const bool released=WaitForSingleObject(owner,10000)==WAIT_OBJECT_0;
    for(HANDLE handle:{owner,contender,fixture.ready,fixture.release,fixture.attempting,fixture.entered})
        CloseHandle(handle);
    require(timedOut&&held&&admitted&&finished&&released&&fixture.ownerSucceeded,
        "Mutex wait did not preserve the original held-GIL contract");
    std::puts("PASS actual production acquire holds GIL until kernel timeout");
}
}

int main()
{
    BW_SYSTEMSTAGE_MAIN();
    BW::LogMsg::automatedTest(true);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    Py_NoSiteFlag=1;Py_Initialize();PyEval_InitThreads();
    int result=1;
    try
    {
        require(std::strncmp(Py_GetVersion(),"2.7.7",5)==0,"Wrong production Python runtime");
        BW::PyMutex::s_link_pyNew.init();
        char name[96];::_snprintf(name,sizeof(name),"wot_ipc_native_%lu",GetCurrentProcessId());
        PyObject* scope=PyModule_GetDict(PyImport_AddModule("__main__"));
        PyObject* prefix=PyString_FromString(name);
        require(prefix&&PyDict_SetItemString(scope,"prefix",prefix)==0,"Cannot publish private mutex name");
        Py_DECREF(prefix);
        python(R"PY(
import BigWorldIPC,gc
def expect(kind,action,prefix=None):
    try:action()
    except kind as error:
        if prefix is not None:assert str(error).startswith(prefix),str(error)
    else:raise AssertionError('Expected '+kind.__name__)
assert [n for n in dir(BigWorldIPC) if not n.startswith('__')]==['Mutex']
assert BigWorldIPC.Mutex.__module__=='BigWorldIPC'
expect(TypeError,lambda:BigWorldIPC.Mutex(1))
mutex=BigWorldIPC.Mutex(ignored=True)
expect(SystemError,lambda:mutex.acquire(0),"PyMutex.acquire: Can't acquire mutex")
expect(SystemError,lambda:mutex.release(),'PyMutex.release: ')
for invalid in (None,1,[]):expect(TypeError,lambda:mutex.create(invalid))
assert mutex.create(prefix) is None
assert mutex.acquire(0) is True and mutex.acquire(0) is True
assert mutex.release() is None and mutex.release() is None
expect(SystemError,lambda:mutex.release(),'PyMutex.release: ')
assert mutex.close() is None and mutex.close() is None
assert mutex.create(prefix+'_old') is None
assert mutex.create(prefix+'_new') is None
assert mutex.acquire(float('nan')) is True
assert mutex.release() is None
assert mutex.acquire(float('inf')) is True
assert mutex.release() is None
assert mutex.acquire(-float('inf')) is True
assert mutex.release() is None
assert mutex.create(prefix+'_nul\0ignored') is None
)PY");
        const std::string oldName=std::string(name)+"_old";
        HANDLE old=OpenMutexA(SYNCHRONIZE,FALSE,oldName.c_str());
        require(!old&&GetLastError()==ERROR_FILE_NOT_FOUND,"Python recreate leaked prior handle");
        const std::string nulName=std::string(name)+"_nul";
        HANDLE raw=OpenMutexA(SYNCHRONIZE,FALSE,nulName.c_str());
        require(raw!=NULL,"Embedded NUL did not follow CreateMutexA c_str semantics");CloseHandle(raw);
        python("assert mutex.create(prefix+u'_\\u6c34') is None\n");
        const std::string utf8Name=std::string(name)+"_\xe6\xb0\xb4";
        raw=OpenMutexA(SYNCHRONIZE,FALSE,utf8Name.c_str());
        require(raw!=NULL,"Unicode binding did not pass UTF-8 bytes to ANSI kernel API");CloseHandle(raw);
        wchar_t wideName[160];
        require(MultiByteToWideChar(CP_ACP,0,utf8Name.c_str(),-1,wideName,160)>0,"Cannot convert current ANSI name");
        raw=OpenMutexW(SYNCHRONIZE,FALSE,wideName);
        require(raw!=NULL,"ANSI and explicit process-codepage kernel names differ");CloseHandle(raw);
        std::printf("PASS Unicode->UTF8->CreateMutexA, actual process ACP=%u\n",GetACP());
        python("assert mutex.create(prefix) is None\n");
        actualGilWait(name);
        python("assert mutex.close() is None\ndel mutex\ngc.collect()\n");
        PyObject* doomed=PyObject_CallObject(reinterpret_cast<PyObject*>(&BW::PyMutex::s_type_),NULL);
        require(doomed!=NULL,"Cannot construct production cleanup owner");
        PyObject* created=PyObject_CallMethod(doomed,const_cast<char*>("create"),const_cast<char*>("s"),name);
        require(created!=NULL,"Cannot initialise production cleanup owner");Py_DECREF(created);
        // The actual Python tp_dealloc and then even a failing core CloseHandle
        // must preserve an exception already pending in their caller.
        PyErr_SetString(PyExc_ValueError,"pending-before-mutex-cleanup");
        Py_DECREF(doomed);
        require(PyErr_ExceptionMatches(PyExc_ValueError)!=0,"Python destructor replaced pending exception");
        // Even a failing destructor CloseHandle must preserve an exception
        // that was already pending in its caller, rather than formatting anew.
        {
            wot::StockIpcMutex invalid;std::uint32_t error=0;
            require(invalid.create(name,error),"Cannot create cleanup owner");
            require(CloseHandle(invalid.handle())!=FALSE,"Cannot invalidate test handle");
        }
        require(PyErr_ExceptionMatches(PyExc_ValueError)!=0,"Cleanup replaced pending exception");
        PyObject *type=NULL,*value=NULL,*trace=NULL;PyErr_Fetch(&type,&value,&trace);
        require(value&&PyString_Check(value)&&std::strcmp(PyString_AsString(value),"pending-before-mutex-cleanup")==0,
            "Cleanup changed pending exception text");
        Py_XDECREF(type);Py_XDECREF(value);Py_XDECREF(trace);
        std::puts("PASS pending exception preserved through failed native cleanup");
        std::puts("PASS production CPython 2.7.7 BigWorldIPC.Mutex, real Win32 API and GIL");
        result=0;
    }
    catch(const std::exception& error)
    {
        if(PyErr_Occurred())PyErr_Print();
        std::fprintf(stderr,"FAIL %s\n",error.what());
    }
    Py_Finalize();
    return result;
}
