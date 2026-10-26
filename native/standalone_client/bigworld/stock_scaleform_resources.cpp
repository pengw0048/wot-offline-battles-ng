#include "pch.hpp"
#include "scaleform/loader.hpp"
#include "scaleform/manager.hpp"
#include "stock_scaleform_resources.hpp"
#include "resmgr/bwresource.hpp"
#include "cstdmf/bw_util.hpp"
#include "resmgr/multi_file_system.hpp"
#include "Kernel/SF_SysFile.h"
#include <atomic>
#include <map>
#include <mutex>
#include <set>

BW_BEGIN_NAMESPACE
int StockScaleformResources_token = 1;
namespace ScaleformBW
{
struct StockScaleformFileState
{
    std::mutex mutex;
    std::atomic<bool> streaming{false};
    std::set<BW::string> streamFiles;
    std::map<BW::string, BinaryPtr> cache;
};

namespace
{
BinaryPtr readResource(const char* filename)
{
    const std::string first = wot::stockScaleformInitialResourcePath(filename);
    BinaryPtr result = BWResource::instance().fileSystem()->readFile(first.c_str());
    if (result) return result;
    const std::string fallback = wot::stockScaleformFallbackResourcePath(first);
    return BWResource::instance().rootSection()->readBinary(fallback.c_str());
}
}

FileOpener::FileOpener() : stockFiles_(new StockScaleformFileState()) {}
FileOpener::~FileOpener() {}

GFile* FileOpener::OpenFile(const char* filename, SInt flags, SInt mode)
{
    BW_GUARD;
    // aaccf0 uses fixed read/buffered and read-only OS modes for streaming.
    (void)flags;
    (void)mode;
    bool stream = false;
    BinaryPtr data;
    {
        std::lock_guard<std::mutex> lock(stockFiles_->mutex);
        stream = stockFiles_->streaming.load(std::memory_order_acquire) &&
            stockFiles_->streamFiles.count(filename) != 0;
        if (!stream)
        {
            const auto found = stockFiles_->cache.find(filename);
            if (found != stockFiles_->cache.end()) data = found->second;
        }
    }
    if (stream)
    {
        BW::string path(filename);
        // Packed members have no native absolute filename. Do not extract or
        // silently switch a selected stream to a whole-file memory buffer.
        if (!BWUtil::isAbsolutePath(path) &&
            BWResource::resolveToAbsolutePath(path) != IFileSystem::FT_FILE) return NULL;
        return new ::Scaleform::SysFile(::Scaleform::String(path.c_str()),
            ::Scaleform::FileConstants::Open_Read | ::Scaleform::FileConstants::Open_Buffered,
            ::Scaleform::FileConstants::Mode_Read);
    }
    if (!data) data = readResource(filename);
    return data ? new BWGMemoryFile(filename, data) : NULL;
}

void FileOpener::stockAddStreamingFile(const BW::string& filename)
{
    std::lock_guard<std::mutex> lock(stockFiles_->mutex);
    stockFiles_->streamFiles.insert(filename);
}

void FileOpener::stockEnableStreaming()
{
    stockFiles_->streaming.store(true, std::memory_order_release);
}

void FileOpener::stockDisableStreaming()
{
    stockFiles_->streaming.store(false, std::memory_order_release);
    std::lock_guard<std::mutex> lock(stockFiles_->mutex);
    stockFiles_->streamFiles.clear();
}

bool FileOpener::stockPrecache(const BW::string& filename, bool forceReload)
{
    if (!forceReload)
    {
        std::lock_guard<std::mutex> lock(stockFiles_->mutex);
        if (stockFiles_->cache.count(filename)) return true;
    }
    BinaryPtr data = readResource(filename.c_str());
    if (!data) return false; // A failed force reload retains the admitted cache.
    std::lock_guard<std::mutex> lock(stockFiles_->mutex);
    stockFiles_->cache[filename] = data;
    return true;
}

void FileOpener::stockEraseCached(const BW::string& filename)
{
    std::vector<BW::string> removed;
    {
        std::lock_guard<std::mutex> lock(stockFiles_->mutex);
        if (filename.empty())
        {
            for (const auto& entry : stockFiles_->cache) removed.push_back(entry.first);
            stockFiles_->cache.clear();
        }
        else stockFiles_->cache.erase(filename);
    }
    if (filename.empty())
        for (const auto& path : removed) BWResource::instance().purge(path, false);
    else BWResource::instance().purge(filename, false);
}
}

namespace
{
::Scaleform::Ptr<ScaleformBW::FileOpener> activeOpener()
{
    auto* manager = ScaleformBW::Manager::pInstance();
    if (!manager || !manager->pLoader()) return NULL;
    ::Scaleform::Ptr<::Scaleform::GFx::FileOpenerBase> base = manager->pLoader()->GetFileOpener();
    return dynamic_cast<ScaleformBW::FileOpener*>(base.GetPtr());
}
}

static PyObject* py_enableStreaming(PyObject* args)
{
    PyObject* filenames;
    if (!PyArg_ParseTuple(args, "O:enableStreaming", &filenames)) return NULL;
    // The original ScriptList reader uses list storage directly. Reject other
    // objects instead of interpreting an unrelated object's memory as a list.
    if (!PyList_Check(filenames))
    {
        PyErr_SetString(PyExc_TypeError, "enableStreaming expects a list");
        return NULL;
    }
    auto opener = activeOpener();
    if (!opener || PyList_GET_SIZE(filenames) == 0) Py_RETURN_FALSE;
    int admitted = 0;
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(filenames); ++i)
    {
        PyObject* value = PyList_GET_ITEM(filenames, i);
        if (value == Py_None) continue;
        BW::string filename;
        if (Script::setData(value, filename)) return NULL;
        opener->stockAddStreamingFile(filename);
        ++admitted;
    }
    if (!admitted) Py_RETURN_FALSE;
    opener->stockEnableStreaming();
    Py_RETURN_TRUE;
}
PY_MODULE_FUNCTION(enableStreaming, ScaleformFileLoader)

static PyObject* py_disableStreaming(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":disableStreaming")) return NULL;
    auto opener = activeOpener();
    if (opener) opener->stockDisableStreaming();
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(disableStreaming, ScaleformFileLoader)

static PyObject* py_wg_precacheScaleformResource(PyObject* args)
{
    PyObject* input;
    PyObject* forced = Py_False;
    BW::string filename;
    bool forceReload;
    if (!PyArg_ParseTuple(args, "O|O:wg_precacheScaleformResource", &input, &forced) ||
        Script::setData(input, filename) || Script::setData(forced, forceReload)) return NULL;
    auto opener = activeOpener();
    return PyBool_FromLong(opener && opener->stockPrecache(filename, forceReload));
}
PY_MODULE_FUNCTION(wg_precacheScaleformResource, BigWorld)

static PyObject* py_wg_eraseScaleformResFromCache(PyObject* args)
{
    PyObject* input = NULL;
    BW::string filename;
    if (!PyArg_ParseTuple(args, "|O:wg_eraseScaleformResFromCache", &input) ||
        (input && Script::setData(input, filename))) return NULL;
    auto opener = activeOpener();
    if (opener) opener->stockEraseCached(filename);
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(wg_eraseScaleformResFromCache, BigWorld)

static PyObject* py_resetScaleformResources(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":resetScaleformResources")) return NULL;
    auto* manager = ScaleformBW::Manager::pInstance();
    if (manager) manager->needReloadResources();
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(resetScaleformResources, BigWorld)
BW_END_NAMESPACE
