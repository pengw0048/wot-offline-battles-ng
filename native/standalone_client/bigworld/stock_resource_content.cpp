#include "pch.hpp"
#include "resmgr/bwresource.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
static PyObject* py_activeContentType(PyObject* args)
{
    if (!PyArg_ParseTuple(args,":activeContentType")) return NULL;
    return PyInt_FromLong(BWResource::instance().stockActiveContentType());
}
PY_MODULE_FUNCTION(activeContentType,ResMgr)

static PyObject* py_appDirectory(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":appDirectory")) return NULL;
    // 6ae210 -> 1184510 -> 1186500 queries this same physical directory owner.
    // 68e9d0 returns its UTF-8 bytes as str, including the trailing slash.
    return Script::getData(BWResource::appDirectory());
}
PY_MODULE_FUNCTION(appDirectory,ResMgr)
BW_END_NAMESPACE
