// Compile the full SDK AddFrame body. Call-frame storage/register setup are boundaries;
// the unmodified native capacity check, exception branch and push order execute here.
#include <vector>
#include <cstddef>
#define SF_INLINE inline
#define SF_DEBUG_ARG(...)
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
struct Value{};struct Traits{};using ScopeStackType=int;
namespace Abc {using MbiInd=int;struct MethodInfo{};}
struct Body {const Abc::MethodInfo&GetMethodInfo(int)const{static Abc::MethodInfo m;return m;}};
struct Bodies {Body Get(int)const{return {};}};
struct VMAbcFile {Bodies GetMethodBody()const{return {};}int GetMethods()const{return 0;}};
static bool setupFailure=false;
static int setups=0,registers=0;
struct CallFrame {
    bool ACopy=false;
    CallFrame()=default;
    CallFrame(const Value&,VMAbcFile&,int,bool,int,const Traits&){setups++;}
    void Setup(const Value&,VMAbcFile&,int,int,bool,const Traits&){setups++;}
    void SetupRegisters(const Abc::MethodInfo&,const Value&,int,const Value*){registers++;}
};
struct Stack {unsigned depth=0;unsigned GetSize()const{return depth;}template<class T>void PushBack(T){depth++;}};
struct Cache {CallFrame frame;CallFrame*MakeCallFrame(){return &frame;}};
struct VM {
    Stack CallStack;Cache CFCache;int error=0;
    enum {eStackOverflowError=1023};
    @LIMIT@
    struct Error {int code;Error(int c,VM&):code(c){}};
    void ThrowError(Error e){error=e.code;}
    bool IsException()const{return setupFailure;}
    @ADD_FRAME@
};
static VM vm;
EXPORT void reset(unsigned depth,int fail){vm.CallStack.depth=depth;vm.error=0;setupFailure=fail;setups=registers=0;}
EXPORT int enter(){Value value;VMAbcFile file;Traits traits;unsigned old=vm.CallStack.depth;vm.AddFrame(value,file,0,value,0,nullptr,false,0,traits);return vm.CallStack.depth==old+1;}
EXPORT void leave(){if(vm.CallStack.depth)vm.CallStack.depth--;}
EXPORT unsigned depth(){return vm.CallStack.depth;}
EXPORT int error(){return vm.error;}
EXPORT int setup_count(){return setups;}
EXPORT int register_count(){return registers;}
