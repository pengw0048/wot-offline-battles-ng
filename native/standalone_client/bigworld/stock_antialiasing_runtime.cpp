#ifndef WOT_STOCK_ANTIALIASING_PORTABLE
#include "pch.hpp"
#include "stock_antialiasing.hpp"
#include "stock_deferred_buffers.hpp"
#include "moo/effect_visual_context.hpp"
#include "moo/render_context.hpp"
#include "pyscript/script.hpp"
#endif

BW_BEGIN_NAMESPACE
int StockAntialiasing_token = 1;
namespace
{
bool constantsRegistered = false;

class MipMapLodBiasConstant : public Moo::EffectConstantValue
{
public:
    virtual bool operator()(ID3DXEffect* effect,D3DXHANDLE handle)
    {
        const auto* owner=stockDeferredBuffers();
        // 99e590 writes GraphicsSettings+5c; 9b01c0 is registered under
        // MipMapLodBias at 9aed41. The original initial/off enum is zero.
        return SUCCEEDED(effect->SetInt(handle,owner?owner->temporalAA().mipMapLodBias():0));
    }
};

bool deviceReady()
{
    // b2a810 receives the CreationThreadInterface subobject (primary+8).
    // b2ac2a publishes its +14 ready flag only after device creation succeeds.
    return Moo::rc().device() && Moo::rc().deviceIndex()<Moo::rc().nDevices();
}
}

bool initialiseStockAntialiasing()
{
    if(!constantsRegistered)
    {
        Moo::rc().effectVisualContext().registerAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME,"MipMapLodBias",
            new MipMapLodBiasConstant);
        constantsRegistered=true;
    }
    return true;
}

void finaliseStockAntialiasing()
{
    if(constantsRegistered)
    {
        Moo::rc().effectVisualContext().unregisterAutoConstant(
            Moo::EffectVisualContext::AUTO_CONSTANT_TYPE_PER_FRAME,"MipMapLodBias");
        constantsRegistered=false;
    }
}

static PyObject* py_getSupportedMultisamplingTypes(PyObject* args)
{
    if(!PyArg_ParseTuple(args,":getSupportedMultisamplingTypes"))return NULL;
    if(!deviceReady())Py_RETURN_NONE;
    ComObjectWrap<IDirect3D9> direct3d;
    D3DDEVICE_CREATION_PARAMETERS creation={};
    if(FAILED(Moo::rc().device()->GetDirect3D(&direct3d)) ||
        FAILED(Moo::rc().device()->GetCreationParameters(&creation)))
        return PyErr_Format(PyExc_RuntimeError,"Cannot query the native antialiasing device");
    const auto& info=Moo::rc().deviceInfo(Moo::rc().deviceIndex());
    D3DFORMAT format=info.windowedDisplayMode_.Format;
    if(!Moo::rc().windowed())
    {
        if(Moo::rc().modeIndex()>=info.displayModes_.size())
            return PyErr_Format(PyExc_RuntimeError,"The native display mode is unavailable");
        format=info.displayModes_[Moo::rc().modeIndex()].Format;
    }
    const bool nvidia=info.identifier_.VendorId==0x10de;
    PyObject* result=PyList_New(0);
    if(!result)return NULL;
    for(const uint32 candidate:wot::stockMultisamplingCandidates())
    {
        const bool supported=wot::stockD3D9SupportsMultisampling(candidate,nvidia,
            [&](uint32 sample)
            {
                // 929420(candidate,1) checks fullscreen capability even while
                // the current WindowController is windowed (922950 format).
                return SUCCEEDED(direct3d->CheckDeviceMultiSampleType(creation.AdapterOrdinal,
                    creation.DeviceType,format,FALSE,static_cast<D3DMULTISAMPLE_TYPE>(sample),NULL));
            });
        if(!supported)continue;
        PyObject* value=PyInt_FromLong(candidate);
        if(!value || PyList_Append(result,value)<0)
        {
            Py_XDECREF(value);Py_DECREF(result);return NULL;
        }
        Py_DECREF(value);
    }
    return result;
}
PY_MODULE_FUNCTION(getSupportedMultisamplingTypes,BigWorld)

static PyObject* py_getMultisamplingType(PyObject* args)
{
    if(!PyArg_ParseTuple(args,":getMultisamplingType"))return NULL;
    return Script::getData(static_cast<uint>(deviceReady()?Moo::rc().stockMultisamplingType():0));
}
PY_MODULE_FUNCTION(getMultisamplingType,BigWorld)

static PyObject* py_setMultisamplingType(PyObject* args)
{
    PyObject* value=NULL;
    uint32 desired=0;
    if(!PyArg_ParseTuple(args,"O:setMultisamplingType",&value) ||
        Script::setData(value,desired,"BigWorld.setMultisamplingType argument 1")!=0)return NULL;
    if(deviceReady())Moo::rc().requestStockMultisampling(desired);
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(setMultisamplingType,BigWorld)

static PyObject* py_getSupportedCustomAAModes(PyObject* args)
{
    if(!PyArg_ParseTuple(args,":getSupportedCustomAAModes"))return NULL;
    // 951a20 = 2+2*99def0(). The optional original AFR service is absent in
    // this one-device backend, so its native NULL guard accepts temporal AA.
    // ForwardPipeline's virtual+9c returns NULL and exposes only index zero.
    const uint32 count=stockDeferredBuffers()?4:1;
    PyObject* result=PyList_New(count);
    if(!result)return NULL;
    for(uint32 i=0;i<count;++i)
    {
        PyObject* value=PyInt_FromLong(i);
        if(!value){Py_DECREF(result);return NULL;}
        PyList_SET_ITEM(result,i,value);
    }
    return result;
}
PY_MODULE_FUNCTION(getSupportedCustomAAModes,BigWorld)

static PyObject* py_getCustomAAMode(PyObject* args)
{
    if(!PyArg_ParseTuple(args,":getCustomAAMode"))return NULL;
    const auto* owner=stockDeferredBuffers();
    return Script::getData(static_cast<uint>(owner?owner->customAAMode():0));
}
PY_MODULE_FUNCTION(getCustomAAMode,BigWorld)

static PyObject* py_setCustomAAMode(PyObject* args)
{
    PyObject* value=NULL;
    uint32 desired=0;
    if(!PyArg_ParseTuple(args,"O:setCustomAAMode",&value) ||
        Script::setData(value,desired,"BigWorld.setCustomAAMode argument 1")!=0)return NULL;
    auto* owner=stockDeferredBuffers();
    if(owner && owner->customAAMode()!=desired)owner->customAAMode(desired);
    Py_RETURN_NONE;
}
PY_MODULE_FUNCTION(setCustomAAMode,BigWorld)
BW_END_NAMESPACE
