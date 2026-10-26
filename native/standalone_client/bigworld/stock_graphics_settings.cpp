#include "pch.hpp"
#include "stock_graphics_settings.hpp"
#include "stock_graphics_presets.hpp"
#include "wot/stock_graphics_settings.hpp"
#include "moo/graphics_settings_picker.hpp"
#include "pyscript/script.hpp"
#include "resmgr/bwresource.hpp"
#include <algorithm>

BW_BEGIN_NAMESPACE
int StockGraphicsSettings_token = 1;

namespace
{
wot::StockGraphicsPreferencesVersion preferencesVersion;

Moo::GraphicsSetting::GraphicsSettingVector orderedSettings()
{
    auto settings = Moo::GraphicsSetting::settings();
    std::sort(settings.begin(), settings.end(), [](const Moo::GraphicsSetting::GraphicsSettingPtr& a,
        const Moo::GraphicsSetting::GraphicsSettingPtr& b) { return a->label() < b->label(); });
    return settings;
}

PyObject* settingView(const Moo::GraphicsSetting::GraphicsSettingPtr& setting)
{
    PyObjectPtr options(NULL, PyObjectPtr::STEAL_REFERENCE);
    if (setting->stockArrayOptions())
    {
        // 91f290 is the original presets-specific SettingsVector serializer.
        options = PyObjectPtr(stockGraphicsPresetOptions(), PyObjectPtr::STEAL_REFERENCE);
    }
    else
    {
        const auto& entries = setting->options();
        options = PyObjectPtr(PyList_New(entries.size()), PyObjectPtr::STEAL_REFERENCE);
        if (!options) return NULL;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries[i];
            // 91d110: label, index, advanced, supported (a list, not a tuple).
            PyObjectPtr label(PyString_FromStringAndSize(entry.m_label.data(),entry.m_label.size()),
                PyObjectPtr::STEAL_REFERENCE);
            if (!label) return NULL;
            PyObject* option = Py_BuildValue("[OiOO]", label.get(), static_cast<int>(i),
                entry.m_advanced ? Py_True : Py_False, entry.m_supported ? Py_True : Py_False);
            if (!option) return NULL;
            PyList_SET_ITEM(options.get(), i, option);
        }
    }
    if (!options) return NULL;
    PyObjectPtr label(PyString_FromStringAndSize(setting->label().data(),setting->label().size()),
        PyObjectPtr::STEAL_REFERENCE);
    PyObjectPtr hint(PyString_FromStringAndSize(setting->desc().data(),setting->desc().size()),
        PyObjectPtr::STEAL_REFERENCE);
    if (!label || !hint) return NULL;
    // a18c00 returns the active native index and always reports delayed=False.
    return Py_BuildValue("(OiOOOOOO)", label.get(), setting->activeOption(), options.get(),
        hint.get(), setting->advanced() ? Py_True : Py_False,
        setting->requiresRestart() ? Py_True : Py_False,
        setting->stockArrayOptions() ? Py_True : Py_False, Py_False);
}
}

bool prepareStockGraphicsPreferences(DataSectionPtr section,
    Moo::GraphicsSetting::SettingsVector& settings, int overrideGroup)
{
    wot::StockGraphicsPreferencesVersion saved;
    if (section)
    {
        saved.major = section->readInt("graphicsSettingsVersion", 0);
        saved.minor = section->readInt("graphicsSettingsVersionMinor", 0);
        saved.maintenance = section->readInt("graphicsSettingsVersionMaintainance", 0);
        saved.status = section->readInt("graphicsSettingsStatus", 0);
    }
    const auto migration = wot::stockGraphicsPreferencesMigration(section.exists(), saved);
    preferencesVersion = migration.result;
    if (migration.migrateShadows)
        for (auto& setting : settings)
            if (setting.first == "SHADOWS_QUALITY")
                setting.second = wot::stockMigrateShadowOption(setting.second);

    // The public detector owns actual hardware queries and authored quality
    // profiles. Its scoring algorithm remains a separate #1513 audit boundary.
    if (migration.refreshDetector)
        Moo::GraphicsSettingsAutoDetector::invalidateStockDetection();
    if (migration.detect)
    {
        Moo::GraphicsSettingsAutoDetector detector;
        if (!detector.init() || !detector.getSettings(settings, overrideGroup)) return false;
    }

    // 91d670 resets texture quality when a saved non-HD distribution becomes
    // HD. The physical application metadata is the real content-type owner.
    const int content = BWResource::instance().stockActiveContentType();
    if (content == 2 && (!section || section->readInt("distributionLevel", 0) != 2))
    {
        auto found = std::find_if(settings.begin(), settings.end(),
            [](const Moo::GraphicsSetting::Setting& setting) { return setting.first == "TEXTURE_QUALITY"; });
        if (found == settings.end()) settings.emplace_back("TEXTURE_QUALITY", 0);
        else found->second = 0;
    }
    return true;
}

void writeStockGraphicsPreferencesMetadata(DataSectionPtr section)
{
    section->writeInt("graphicsSettingsVersion", preferencesVersion.major);
    section->writeInt("graphicsSettingsVersionMinor", preferencesVersion.minor);
    section->writeInt("graphicsSettingsVersionMaintainance", preferencesVersion.maintenance);
    section->writeInt("graphicsSettingsStatus", preferencesVersion.status);
    section->writeInt("distributionLevel", BWResource::instance().stockActiveContentType());
}

PyObject* stockGraphicsSettingsView()
{
    const auto settings = orderedSettings();
    std::size_t count = 0;
    for (const auto& setting : settings) if (setting->stockImportant()) ++count;
    PyObjectPtr result(PyList_New(count), PyObjectPtr::STEAL_REFERENCE);
    if (!result) return NULL;
    Py_ssize_t index = 0;
    for (const auto& setting : settings)
    {
        if (!setting->stockImportant()) continue;
        PyObject* value = settingView(setting);
        if (!value) return NULL;
        PyList_SET_ITEM(result.get(), index++, value);
    }
    Py_INCREF(result.get());
    return result.get();
}

static PyObject* py_generateGfxSettings(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":generateGfxSettings")) return NULL;
    prepareStockGraphicsSettingsPublication();
    PyObjectPtr constants(PyImport_ImportModule("account_helpers.settings_core.settings_constants"),
        PyObjectPtr::STEAL_REFERENCE);
    if (!constants) return NULL;
    PyObjectPtr utilities(PyImport_ImportModule("gui.shared.utils"), PyObjectPtr::STEAL_REFERENCE);
    if (!utilities) return NULL;
    PyObjectPtr graphics(PyObject_GetAttrString(constants.get(), "GRAPHICS"), PyObjectPtr::STEAL_REFERENCE);
    if (!graphics) return NULL;
    PyObjectPtr important(PyObject_GetAttrString(utilities.get(), "GRAPHICS_SETTINGS"), PyObjectPtr::STEAL_REFERENCE);
    if (!important) return NULL;
    const auto settings = orderedSettings();
    PyObjectPtr result(PyTuple_New(settings.size()), PyObjectPtr::STEAL_REFERENCE);
    if (!result) return NULL;
    for (std::size_t i = 0; i < settings.size(); ++i)
    {
        const auto& setting = settings[i];
        PyObjectPtr label(PyString_FromStringAndSize(setting->label().data(), setting->label().size()),
            PyObjectPtr::STEAL_REFERENCE);
        if (!label || PyObject_SetAttrString(graphics.get(), setting->label().c_str(), label.get()) < 0 ||
            (setting->stockImportant() && PyObject_SetAttrString(important.get(), setting->label().c_str(), label.get()) < 0))
            return NULL;
        Py_INCREF(label.get());
        PyTuple_SET_ITEM(result.get(), i, label.get());
    }
    Py_INCREF(result.get());
    return result.get();
}
PY_MODULE_FUNCTION(generateGfxSettings, BigWorld)

static PyObject* py_graphicSetting(PyObject* args)
{
    PyObject* input;
    BW::string label;
    if (!PyArg_ParseTuple(args, "O:graphicSetting", &input) || Script::setData(input, label)) return NULL;
    auto setting = Moo::GraphicsSetting::getFromLabel(label);
    if (!setting) Py_RETURN_NONE;
    return settingView(setting);
}
PY_MODULE_FUNCTION(graphicSetting, BigWorld)

static PyObject* py_graphicSettingIsSupported(PyObject* args)
{
    PyObject* input;
    BW::string label;
    int index;
    if (!PyArg_ParseTuple(args, "Oi:graphicSettingIsSupported", &input, &index) || Script::setData(input, label)) return NULL;
    auto setting = Moo::GraphicsSetting::getFromLabel(label);
    return PyBool_FromLong(setting && index >= 0 && static_cast<std::size_t>(index) < setting->options().size());
}
PY_MODULE_FUNCTION(graphicSettingIsSupported, BigWorld)

static PyObject* py_getGraphicsSettingApplyMethod(PyObject* args)
{
    PyObject* input;
    BW::string label;
    int index;
    if (!PyArg_ParseTuple(args, "Oi:getGraphicsSettingApplyMethod", &input, &index) || Script::setData(input, label)) return NULL;
    auto setting = Moo::GraphicsSetting::getFromLabel(label);
    const bool restart = setting && setting->requiresRestart() && setting->activeOption() != index;
    return PyString_FromString(restart ? "restart" : "normal");
}
PY_MODULE_FUNCTION(getGraphicsSettingApplyMethod, BigWorld)

static PyObject* py_graphicsSettingsStatus(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":graphicsSettingsStatus")) return NULL;
    return PyInt_FromLong(preferencesVersion.status);
}
PY_MODULE_FUNCTION(graphicsSettingsStatus, BigWorld)

static PyObject* py_resetGraphicsSettingsStatus(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":resetGraphicsSettingsStatus")) return NULL;
    preferencesVersion.status = 0;
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(resetGraphicsSettingsStatus, BigWorld)

static PyObject* py_importantGraphicSettings(PyObject* args)
{
    if (!PyArg_ParseTuple(args, ":importantGraphicSettings")) return NULL;
    // a180a0 intentionally returns a fresh empty dict in the original binary.
    return PyDict_New();
}
PY_MODULE_FUNCTION(importantGraphicSettings, BigWorld)
BW_END_NAMESPACE
