// Real CPython 2.7 executes the production scene-query sources plus exact
// #1513 consumer bytecode. Space traversal, Water and identity are fixtures.
#include "stock_battle_scene_queries_host.hpp"
#include "stock_scene_identity.hpp"

#include "stock_ground_water_query.inc"
#include "stock_battle_scene_queries.cpp"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>

namespace BW {
std::vector<HostVisit> hostVisits;
int hostLiveSpaces = 0;
ClientSpacePtr DeprecatedSpaceHelpers::camera;

bool readStockObstacleIdentity(const CollisionObstacle & obstacle, StockObstacleIdentity & output) {
    if (obstacle.identity.raises) {
        PyErr_SetString(PyExc_RuntimeError, "host identity owner failed");
        return false;
    }
    if (!obstacle.identity.known) return false;
    output.chunkID = obstacle.identity.chunkID;
    output.itemIndex = obstacle.identity.itemIndex;
    return true;
}

namespace {
std::vector<std::unique_ptr<Water>> waters;

bool vector3(PyObject * o, Vector3 & out, int index) {
    if (!PySequence_Check(o) || PySequence_Size(o) != 3) {
        PyErr_Clear();
        PyErr_Format(PyExc_TypeError, "() argument %d must be a Vector3", index);
        return false;
    }
    float v[3];
    for (Py_ssize_t i = 0; i < 3; ++i) {
        PyObject * item = PySequence_GetItem(o, i);
        v[i] = item ? float(PyFloat_AsDouble(item)) : 0.f;
        Py_XDECREF(item);
        if (PyErr_Occurred()) return false;
    }
    out = Vector3(v[0], v[1], v[2]);
    return true;
}

bool floats(PyObject * o, float * out, Py_ssize_t count) {
    if (!PySequence_Check(o) || PySequence_Size(o) != count) {
        PyErr_SetString(PyExc_TypeError, "host float sequence has the wrong size");
        return false;
    }
    for (Py_ssize_t i = 0; i < count; ++i) {
        PyObject * item = PySequence_GetItem(o, i);
        out[i] = item ? float(PyFloat_AsDouble(item)) : 0.f;
        Py_XDECREF(item);
        if (PyErr_Occurred()) return false;
    }
    return true;
}

bool matrix(PyObject * o, Matrix & out) {
    if (o == Py_None) return true;
    return floats(o, &out.m[0][0], 16);
}

// The engine's PY_AUTO wrapper: arity and converters only.
PyObject * pyCollideSegment(PyObject *, PyObject * args) {
    const Py_ssize_t count = PyTuple_GET_SIZE(args);
    if (count < 4 || count > 5) {
        PyErr_Format(PyExc_TypeError, "wg_collideSegment() takes 4 to 5 arguments (%d given)", int(count));
        return NULL;
    }
    const long space = PyInt_AsLong(PyTuple_GET_ITEM(args, 0));
    if (PyErr_Occurred()) return NULL;
    Vector3 start, end;
    if (!vector3(PyTuple_GET_ITEM(args, 1), start, 2) || !vector3(PyTuple_GET_ITEM(args, 2), end, 3))
        return NULL;
    const long flags = PyInt_AsLong(PyTuple_GET_ITEM(args, 3));
    if (PyErr_Occurred()) return NULL;
    if (flags != long(uint16(flags))) {
        PyErr_SetString(PyExc_TypeError, "Integer is out of range");
        return NULL;
    }
    PyObjectPtr filter(count == 5 ? PyTuple_GET_ITEM(args, 4) : NULL);
    return wg_collideSegment(SpaceID(space), start, end, uint16(flags), filter);
}

PyObject * pyCollideWater(PyObject *, PyObject * args) {
    const Py_ssize_t count = PyTuple_GET_SIZE(args);
    if (count < 2 || count > 3) {
        PyErr_Format(PyExc_TypeError, "wg_collideWater() takes 2 to 3 arguments (%d given)", int(count));
        return NULL;
    }
    Vector3 start, end;
    if (!vector3(PyTuple_GET_ITEM(args, 0), start, 1) || !vector3(PyTuple_GET_ITEM(args, 1), end, 2))
        return NULL;
    bool alpha = true;
    if (count == 3) {
        const int value = PyObject_IsTrue(PyTuple_GET_ITEM(args, 2));
        if (value < 0) return NULL;
        alpha = value != 0;
    }
    return PyFloat_FromDouble(wg_collideWater(start, end, alpha));
}

PyObject * pySpaceBounds(PyObject *, PyObject * args) {
    if (PyTuple_GET_SIZE(args) != 0) {
        PyErr_SetString(PyExc_TypeError, "wg_getSpaceBounds() takes no arguments");
        return NULL;
    }
    return Script::getData(wg_getSpaceBounds());
}

ClientSpace * hostSpace(PyObject * args, Py_ssize_t index = 0) {
    const long id = PyInt_AsLong(PyTuple_GET_ITEM(args, index));
    ClientSpace * space = SpaceManager::instance().space(SpaceID(id));
    if (!space && !PyErr_Occurred()) PyErr_SetString(PyExc_KeyError, "host space");
    return space;
}

PyObject * addSpace(PyObject *, PyObject * args) {
    int id; if (!PyArg_ParseTuple(args, "i", &id)) return NULL;
    SpaceManager::instance().spaces[id] = ClientSpacePtr(new ClientSpace(id));
    Py_RETURN_NONE;
}
PyObject * removeSpace(PyObject *, PyObject * args) {
    int id; if (!PyArg_ParseTuple(args, "i", &id)) return NULL;
    SpaceManager::instance().spaces.erase(id);
    Py_RETURN_NONE;
}
PyObject * clearSpace(PyObject *, PyObject * args) {
    ClientSpace * space = hostSpace(args); if (!space) return NULL;
    space->clear();
    Py_RETURN_NONE;
}
PyObject * setPrune(PyObject *, PyObject * args) {
    int id, prune; if (!PyArg_ParseTuple(args, "ii", &id, &prune)) return NULL;
    ClientSpace * space = hostSpace(args); if (!space) return NULL;
    space->prune = prune != 0;
    Py_RETURN_NONE;
}
PyObject * setBounds(PyObject *, PyObject * args) {
    int id; float a, b, c, d, e, f;
    if (!PyArg_ParseTuple(args, "iffffff", &id, &a, &b, &c, &d, &e, &f)) return NULL;
    ClientSpace * space = hostSpace(args); if (!space) return NULL;
    space->box.minimum = Vector3(a, b, c);
    space->box.maximum = Vector3(d, e, f);
    Py_RETURN_NONE;
}
PyObject * setCamera(PyObject *, PyObject * args) {
    PyObject * id; if (!PyArg_ParseTuple(args, "O", &id)) return NULL;
    if (id == Py_None) { DeprecatedSpaceHelpers::camera = ClientSpacePtr(); Py_RETURN_NONE; }
    ClientSpace * space = hostSpace(args); if (!space) return NULL;
    DeprecatedSpaceHelpers::camera = ClientSpacePtr(space);
    Py_RETURN_NONE;
}
// addSurface(space, flags, distance, (v0, v1, v2), matrix16|None, identity)
// identity: None, 'raise' or (chunkID, itemIndex).
PyObject * addSurface(PyObject *, PyObject * args) {
    int id, flags; float distance; PyObject * vertices, * transform, * identity;
    if (!PyArg_ParseTuple(args, "iifOOO", &id, &flags, &distance, &vertices, &transform, &identity)) return NULL;
    ClientSpace * space = hostSpace(args); if (!space) return NULL;
    HostSurface surface;
    surface.distance = distance;
    surface.triangle.flags_ = uint16(flags);
    for (Py_ssize_t i = 0; i < 3; ++i) {
        PyObject * vertex = PySequence_GetItem(vertices, i);
        const bool ok = vertex && floats(vertex, &surface.triangle.v[i].x, 3);
        Py_XDECREF(vertex);
        if (!ok) return NULL;
    }
    if (!matrix(transform, surface.obstacle.matrix)) return NULL;
    if (PyString_Check(identity)) surface.obstacle.identity.raises = true;
    else if (identity != Py_None) {
        unsigned long chunk, item;
        if (!PyArg_ParseTuple(identity, "kk", &chunk, &item)) return NULL;
        surface.obstacle.identity.known = true;
        surface.obstacle.identity.chunkID = uint32(chunk);
        surface.obstacle.identity.itemIndex = uint32(item);
    }
    space->surfaces.push_back(surface);
    Py_RETURN_NONE;
}
PyObject * visits(PyObject *, PyObject *) {
    PyObject * list = PyList_New(0);
    for (const HostVisit & v : hostVisits) {
        PyObject * item = Py_BuildValue("(iifi)", v.space, int(v.flags), v.distance, v.verdict);
        PyList_Append(list, item); Py_DECREF(item);
    }
    hostVisits.clear();
    return list;
}
PyObject * liveSpaces(PyObject *, PyObject *) { return PyInt_FromLong(hostLiveSpaces); }

// addWater(transform16, size(x,z), tessellation, useEdgeAlpha, grid(x,z), alpha, bb(minx,minz,maxx,maxz), flags)
PyObject * addWater(PyObject *, PyObject * args) {
    PyObject * transform, * alpha; float sx, sz, tess, x0, z0, x1, z1; int edge, gx, gz, initialised, visible;
    if (!PyArg_ParseTuple(args, "O(ff)fi(ii)O(ffff)(ii)", &transform, &sx, &sz, &tess, &edge, &gx, &gz,
                          &alpha, &x0, &z0, &x1, &z1, &initialised, &visible)) return NULL;
    std::unique_ptr<Water> water(new Water);
    if (!matrix(transform, water->transform_)) return NULL;
    // Fixtures are translation-only; the inverse negates the origin.
    water->invTransform_.m[3][0] = -water->transform_.m[3][0];
    water->invTransform_.m[3][1] = -water->transform_.m[3][1];
    water->invTransform_.m[3][2] = -water->transform_.m[3][2];
    water->state_.size_.x = sx; water->state_.size_.y = sz;
    water->state_.tessellation_ = tess; water->state_.useEdgeAlpha_ = edge != 0;
    water->gridSizeX_ = uint32(gx); water->gridSizeZ_ = uint32(gz);
    const Py_ssize_t n = PySequence_Size(alpha);
    for (Py_ssize_t i = 0; i < n; ++i) {
        PyObject * item = PySequence_GetItem(alpha, i);
        water->wAlpha_.push_back(uint32(PyInt_AsUnsignedLongMask(item)));
        Py_XDECREF(item);
    }
    water->bbActual_.minimum = Vector3(x0, -1.f, z0);
    water->bbActual_.maximum = Vector3(x1, 1.f, z1);
    water->initialised_ = initialised != 0; water->visible_ = visible != 0;
    Waters::instance().push_back(water.get());
    waters.push_back(std::move(water));
    Py_RETURN_NONE;
}
PyObject * clearWaters(PyObject *, PyObject *) {
    Waters::instance().clear(); waters.clear();
    Py_RETURN_NONE;
}
// Production a302e0 port on one water for the instruction oracle.
PyObject * waterDistance(PyObject *, PyObject * args) {
    int index, alpha; PyObject * a, * b;
    if (!PyArg_ParseTuple(args, "iOOi", &index, &a, &b, &alpha)) return NULL;
    Vector3 start, end;
    if (!vector3(a, start, 1) || !vector3(b, end, 2)) return NULL;
    return PyFloat_FromDouble(waters.at(size_t(index))->stockWaterRayDistance(start, end, alpha != 0));
}

PyMethodDef bigWorld[] = {
    {"wg_collideSegment", pyCollideSegment, METH_VARARGS, NULL},
    {"wg_collideWater", pyCollideWater, METH_VARARGS, NULL},
    {"wg_getSpaceBounds", pySpaceBounds, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
PyMethodDef hostMethods[] = {
    {"addSpace", addSpace, METH_VARARGS, NULL}, {"removeSpace", removeSpace, METH_VARARGS, NULL},
    {"clearSpace", clearSpace, METH_VARARGS, NULL}, {"setPrune", setPrune, METH_VARARGS, NULL},
    {"setBounds", setBounds, METH_VARARGS, NULL}, {"setCamera", setCamera, METH_VARARGS, NULL},
    {"addSurface", addSurface, METH_VARARGS, NULL}, {"visits", visits, METH_NOARGS, NULL},
    {"liveSpaces", liveSpaces, METH_NOARGS, NULL}, {"addWater", addWater, METH_VARARGS, NULL},
    {"clearWaters", clearWaters, METH_NOARGS, NULL}, {"waterDistance", waterDistance, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};
}
}

int main(int argc, char ** argv) {
    assert(argc == 2);
    Py_NoSiteFlag = 1;
    Py_Initialize();
    assert(Py_InitModule("_BigWorld", BW::bigWorld) && Py_InitModule("Host", BW::hostMethods));
    FILE * script = std::fopen(argv[1], "r");
    assert(script);
    const int failed = PyRun_SimpleFileEx(script, argv[1], 1);
    BW::DeprecatedSpaceHelpers::camera = BW::ClientSpacePtr();
    BW::SpaceManager::instance().spaces.clear();
    Py_Finalize();
    if (failed) return 1;
    assert(BW::hostLiveSpaces == 0);
    std::cout << "Real CPython 2.7 scene-query binding and consumer contract passed" << std::endl;
    return 0;
}
