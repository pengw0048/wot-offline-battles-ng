#ifndef WOT_STOCK_DAMAGE_STICKER_REGISTRATION_PORTABLE
#include "pch.hpp"
#include "stock_damage_sticker_registration.hpp"
#include "stock_vehicle_stickers.hpp"
#include "pyscript/script.hpp"
#endif

BW_BEGIN_NAMESPACE
int StockDamageStickerRegistration_token = 1;

static PyObject* py_wg_registerDamageSticker(PyObject* args)
{
    PyObject* values[6];
    if (!PyArg_ParseTuple(args, "OOOOOO:wg_registerDamageSticker", &values[0], &values[1],
        &values[2], &values[3], &values[4], &values[5])) return NULL;
    BW::string name, texture, bump;
    Vector2 size;
    float variation = 0.f;
    bool randomYaw = false;
    // 653f40 converts all six values before it calls the actual style owner.
    // A failed argument must not append even a partial variant to the cache.
    if (Script::setData(values[0], name, "() argument 1") ||
        Script::setData(values[1], texture, "() argument 2") ||
        Script::setData(values[2], bump, "() argument 3") ||
        Script::setData(values[3], size, "() argument 4") ||
        Script::setData(values[4], variation, "() argument 5") ||
        Script::setData(values[5], randomYaw, "() argument 6")) return NULL;
    // Preserve 95ae90's asymmetric count/new-name versus index/append return.
    // The same registry is read by the real WGStickerModel geometry owner.
    return PyInt_FromLong(registerStockDamageSticker(name, texture, bump, size, variation, randomYaw));
}
PY_MODULE_FUNCTION(wg_registerDamageSticker, BigWorld)
BW_END_NAMESPACE
