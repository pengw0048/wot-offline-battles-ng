// Host for patch_stock_python_watchers.py: real CPython 2.7 runs the patched
// pywatcher.cpp registration block through the engine's own PyModuleMethodLink.
#include <Python.h>
#include <cstdio>
#include <vector>

#define BW_BEGIN_NAMESPACE namespace BW {
#define BW_END_NAMESPACE }
#define MF_ASSERT(condition)

BW_BEGIN_NAMESPACE
namespace Script
{
class InitTimeJob
{
public:
    explicit InitTimeJob(int) { jobs().push_back(this); }
    virtual ~InitTimeJob() {}
    virtual void init() = 0;
    static std::vector<InitTimeJob *> & jobs() { static std::vector<InitTimeJob *> all; return all; }
};
}

@LINK_CLASS@

@LINK_METHODS@

@WATCHER_BLOCK@
BW_END_NAMESPACE

int main(int argc, char ** argv)
{
    if (argc != 2) return 2;
    Py_NoSiteFlag = 1;
    Py_DontWriteBytecodeFlag = 1;
    Py_Initialize();
    // Script::init runs every InitTimeJob after the interpreter exists.
    for (BW::Script::InitTimeJob * job : BW::Script::InitTimeJob::jobs()) job->init();
    FILE * driver = std::fopen(argv[1], "r");
    if (!driver) return 3;
    const int result = PyRun_SimpleFileEx(driver, argv[1], 1);
    Py_Finalize();
    return result ? 1 : 0;
}
