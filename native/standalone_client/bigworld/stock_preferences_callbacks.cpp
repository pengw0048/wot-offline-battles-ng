#include "pch.hpp"
#include "stock_preferences_callbacks.hpp"
#include "pyscript/script.hpp"
#include "pyscript/py_data_section.hpp"
#include <vector>

BW_BEGIN_NAMESPACE
int StockPreferencesCallbacks_token = 1;
namespace
{
std::vector<PyObjectPtr> saveCallbacks;
std::vector<PyObjectPtr> readCallbacks;
bool existedAtStartup = false;
bool closed = false;

void clearCallbacks()
{
    // Detach both lists before decref: a bound receiver's finalizer can
    // synchronously subscribe again. Such new admissions own their next event.
    std::vector<PyObjectPtr> saves;
    std::vector<PyObjectPtr> reads;
    saves.swap(saveCallbacks);
    reads.swap(readCallbacks);
    saves.clear();
    reads.clear();
}

bool subscribe(PyObject* object, std::vector<PyObjectPtr>& callbacks)
{
    if (closed)
    {
        PyErr_SetString(PyExc_RuntimeError, "Preference callbacks are shutting down");
        return false;
    }
    // 622a40 admits ScriptObject, including None and non-callable objects.
    // The original dispatcher checks callability, not the registration wrapper.
    callbacks.emplace_back(object);
    return true;
}

void invoke(PyObject* callback, PyObject* arguments, const char* prefix)
{
    if (!arguments)
    {
        PyErr_Print();
        return;
    }
    Py_INCREF(callback);
    // The production script helper consumes both references and reports/clears
    // callback exceptions, allowing subsequent subscribers and the save to run.
    Script::call(callback, arguments, prefix);
}

class PreferencesScriptShutdown : public Script::FiniTimeJob
{
    virtual void fini() { finiStockPreferencesCallbacks(); }
} preferencesScriptShutdown;
}

void stockRecordPreferencesLoaded(DataSectionPtr root)
{
    // 623200 records a successfully parsed, nonempty root before consumers add
    // sections. A later save must not change this startup-only snapshot.
    existedAtStartup = root && root->countChildren() > 0;
    closed = false;
}

void stockNotifySavePreferences(DataSectionPtr root)
{
    if (closed || !root) return;
    // Native dispatch captures its end at admission. Retaining that admitted
    // list also keeps it valid if a callback clears/reallocates the registry.
    const auto admitted = saveCallbacks;
    for (const auto& callback : admitted)
    {
        if (closed) break;
        if (!PyCallable_Check(callback.get())) continue;
        // 6225c0 creates a distinct wrapper per subscriber for the same tree.
        PyObject* section = new PyDataSection(root);
        invoke(callback.get(), Py_BuildValue("(N)", section),
            "savePreferences callback: ");
    }
}

void stockNotifyReadPreferences(int windowMode)
{
    if (closed) return;
    const auto admitted = readCallbacks;
    for (const auto& callback : admitted)
    {
        if (closed) break;
        if (!PyCallable_Check(callback.get())) continue;
        invoke(callback.get(), Py_BuildValue("(si)", "windowMode", windowMode),
            "readPreferences callback: ");
    }
}

void finiStockPreferencesCallbacks()
{
    closed = true;
    clearCallbacks();
}

static PyObject* py_wg_subscribeToSavePreferences(PyObject* args)
{
    PyObject* callback;
    if (!PyArg_ParseTuple(args, "O:wg_subscribeToSavePreferences", &callback) ||
        !subscribe(callback, saveCallbacks)) return NULL;
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(wg_subscribeToSavePreferences, BigWorld)

static PyObject* py_wg_subscribeToReadPreferences(PyObject* args)
{
    PyObject* callback;
    if (!PyArg_ParseTuple(args, "O:wg_subscribeToReadPreferences", &callback) ||
        !subscribe(callback, readCallbacks)) return NULL;
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(wg_subscribeToReadPreferences, BigWorld)

static PyObject* py_wg_unsubAllSavePreferences(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":wg_unsubAllSavePreferences")) return NULL;
    clearCallbacks();
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(wg_unsubAllSavePreferences, BigWorld)

static PyObject* py_wg_preferencesExistedAtStartup(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":wg_preferencesExistedAtStartup")) return NULL;
    return PyBool_FromLong(existedAtStartup);
}
PY_MODULE_FUNCTION(wg_preferencesExistedAtStartup, BigWorld)
BW_END_NAMESPACE
