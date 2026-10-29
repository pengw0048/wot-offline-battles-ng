// The scene traversal and identity fixture is a private generated copy of the
// existing scene host. Production bridge, skin, and scene code execute below.
#include "stock_offline_query_bridge_host.hpp"
namespace BW { PyObject * hostIdentityException = NULL; }
#define main originalSceneHostMain
#include "stock_offline_query_scene_host.inc"
#undef main
#include "stock_offline_query_bridge.cpp"

extern "C" void initoffline_math_batch_native(void);
extern "C" PyObject * stockTestMotionWorld(PyObject *, PyObject *);

namespace
{
using stock_offline_query::Context;
Context * context(PyObject * object)
{
    return static_cast<Context *>(PyCapsule_GetPointer(object, "NativeQueryFixture"));
}
void destroy(PyObject * object) { stock_offline_query::end(context(object)); }
PyObject * openQuery(PyObject *, PyObject * capabilities)
{
    auto * value = stock_offline_query::begin(capabilities);
    if (!value) Py_RETURN_NONE;
    return PyCapsule_New(value, "NativeQueryFixture", destroy);
}
PyObject * rayQuery(PyObject *, PyObject * args)
{
    PyObject * owner, * filter, * a, * b;
    int ground, nativeGround = 0;
    if (!PyArg_ParseTuple(args, "OOiOO|i", &owner, &filter, &ground, &a, &b, &nativeGround)) return NULL;
    auto * value = context(owner); if (!value) return NULL;
    BW::Vector3 start, end;
    if (!BW::vector3(a, start, 4) || !BW::vector3(b, end, 5)) return NULL;
    const double first[3] = {start.x, start.y, start.z}, last[3] = {end.x, end.y, end.z};
    stock_offline_query::Hit hit{};
    const auto result = stock_offline_query::ray(value, nativeGround ? NULL : filter,
        ground != 0, first, last, hit);
    PyObject * output = result == stock_offline_query::Found ? Py_BuildValue("((ddd)(ddd)I(ddd)II)",
        hit.point[0], hit.point[1], hit.point[2], hit.normal[0], hit.normal[1], hit.normal[2],
        hit.material, hit.translation[0], hit.translation[1], hit.translation[2], hit.item, hit.chunk) : Py_None;
    if (output == Py_None) Py_INCREF(output);
    if (!output) return NULL;
    return Py_BuildValue("(iN)", int(result), output);
}
PyObject * beforeEffect(PyObject *, PyObject * owner)
{
    auto * value = context(owner); if (!value) return NULL;
    return PyBool_FromLong(stock_offline_query::beforeEffect(value));
}
PyObject * afterEffect(PyObject *, PyObject * owner)
{
    auto * value = context(owner); if (!value) return NULL;
    return PyBool_FromLong(stock_offline_query::afterEffect(value));
}
PyObject * groundAvailable(PyObject *, PyObject * owner)
{
    auto * value = context(owner); if (!value) return NULL;
    return PyBool_FromLong(stock_offline_query::groundAvailable(value));
}
PyObject * identityError(PyObject *, PyObject * exception)
{
    if (exception != Py_None && !PyExceptionClass_Check(exception)) {
        PyErr_SetString(PyExc_TypeError, "Expected an exception class"); return NULL;
    }
    BW::hostIdentityException = exception == Py_None ? NULL : exception;
    Py_RETURN_NONE;
}
// Exercise the real preparation owner independently of the fixture BSP's
// triangle geometry, and expose allocation cardinality only in this host.
PyObject * preparedSkin(PyObject *, PyObject * args)
{
    PyObject * owner, * p, * a, * b, * n, * surfaces;
    if (!PyArg_ParseTuple(args, "OOOOOO", &owner, &p, &a, &b, &n, &surfaces)) return NULL;
    auto * value = context(owner); if (!value) return NULL;
    using namespace stock_offline_query;
    try {
        const auto hit = point(vector(point(p))), start = point(vector(point(a))), end = point(vector(point(b)));
        Point normal{}; if (n != Py_None) normal = point(vector(point(n)));
        std::vector<Surface> candidates;
        each(surfaces, [&](PyObject * row) {
            tuple(row, 4); candidates.push_back({u32(at(row,0)),u32(at(row,1)),u32(at(row,2)),u32(at(row,3))});
        });
        value->live();
        // Alias exclusions run inside traversal, before geometry is loaded.
        // A real wire identity must not inherit an anonymous material wildcard.
        Ref keeps(required(PyList_New(0))); Filter filter;
        for (const auto & surface : candidates) {
            Surface other = surface; other.item ^= 0x80000000u;
            const std::set<Surface> excluded{other}; std::vector<Surface> observed;
            const bool accepted = value->keep(filter, surface, excluded, observed, true);
            Ref keep(required(PyBool_FromLong(accepted)));
            if (PyList_Append(keeps.get(), keep.get()) < 0) throw Error();
        }
        value->geometry(hit, start, end, candidates);
        const auto result = motionSkin(value->view, hit, start, end, n == Py_None ? NULL : &normal,
            candidates, value->tuning, SkinMath{NULL,distance,bound});
        Ref excluded(required(PyList_New(0)));
        for (const auto & surface : result.excluded) {
            auto row = surfaceTuple(surface);
            if (PyList_Append(excluded.get(), row.get()) < 0) throw Error();
        }
        return Py_BuildValue("(idN(kkkk)N)", int(result.found), result.distance, excluded.release(),
            static_cast<unsigned long>(value->instanceRows.size()), static_cast<unsigned long>(value->bakedRows.size()),
            static_cast<unsigned long>(value->view.instances.size()), static_cast<unsigned long>(value->view.baked_instances.size()), keeps.release());
    } catch (...) {
        if (!PyErr_Occurred()) PyErr_SetString(PyExc_RuntimeError, "Host skin preparation failed");
        return NULL;
    }
}
PyMethodDef methods[] = {
    {"open", openQuery, METH_O, NULL}, {"ray", rayQuery, METH_VARARGS, NULL},
    {"before", beforeEffect, METH_O, NULL}, {"after", afterEffect, METH_O, NULL},
    {"groundAvailable", groundAvailable, METH_O, NULL},
    {"motionWorld", stockTestMotionWorld, METH_VARARGS, NULL},
    {"identityError", identityError, METH_O, NULL},
    {"preparedSkin", preparedSkin, METH_VARARGS, NULL}, {NULL, NULL, 0, NULL}};
}
int main(int argc, char ** argv)
{
    assert(argc == 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    assert(Py_InitModule("_BigWorld", BW::bigWorld) && Py_InitModule("Host", BW::hostMethods));
    assert(Py_InitModule("Bridge", methods));
    initoffline_math_batch_native();
    FILE * script = std::fopen(argv[1], "r"); assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    BW::DeprecatedSpaceHelpers::camera = BW::ClientSpacePtr();
    BW::SpaceManager::instance().spaces.clear();
    Py_Finalize();
    if (failed) return 1;
    assert(BW::hostLiveSpaces == 0);
    return 0;
}
