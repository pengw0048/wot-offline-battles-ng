// Isolated executable using the production embedded Python static library.
// The fixed numerical source is linked as a builtin, never a second Python DLL.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Python.h>
#include <cstdio>
#include <cstring>
#include <iostream>

extern "C" void __cdecl initoffline_math_batch_native(void);
// The upstream C++ fixture uses PyMODINIT_FUNC; this static Py_BUILD_CORE
// configuration declares it with C++ linkage, unlike the product module.
void __cdecl initnative_chunk_vector(void);

int main(int argc,char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc!=6)return 2;
    Py_NoSiteFlag=1;
    Py_DontWriteBytecodeFlag=1;
    Py_SetPythonHome(argv[1]);
    if(PyImport_AppendInittab("offline_math_batch_native",initoffline_math_batch_native)!=0)return 3;
    if(PyImport_AppendInittab("native_chunk_vector",initnative_chunk_vector)!=0)return 3;
    Py_Initialize();
    if(std::strncmp(Py_GetVersion(),"2.7.7",5)!=0) {
        std::cerr<<"Probe requires actual embedded CPython 2.7.7, got "<<Py_GetVersion()<<std::endl;
        Py_Finalize();return 4;
    }
    PySys_SetArgv(argc-2,argv+2);
    // Ensure user32 is really loaded; queries are passive and do not activate
    // a window or alter the current Compound/client diagnostic foreground.
    const HWND foreground=GetForegroundWindow();
    (void)foreground;
    FILE* input=NULL;
    if(fopen_s(&input,argv[2],"rb")!=0 || !input) { Py_Finalize();return 5; }
    const int result=PyRun_SimpleFileEx(input,argv[2],1);
    if(PyErr_Occurred())PyErr_Print();
    Py_Finalize();
    return result==0?0:1;
}
