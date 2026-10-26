namespace BW {
PyTypeObject dataType={PyObject_HEAD_INIT(NULL) 0};
DataSectionPtr s_scriptsPreferences;
CanvasApp CanvasApp::instance;
bool DataResource::fail=false;DataSectionPtr DataResource::lastSaved;int DataResource::writes=0;
DeviceApp fixtureDevice;
bool BWResource::watched=true;
static void deallocData(PyObject*p){delete static_cast<PyDataSection*>(p);}
static PyObject* subscriptData(PyObject*p,PyObject*key){if(!PyString_Check(key)){PyErr_SetString(PyExc_TypeError,"string key expected");return NULL;}auto section=static_cast<PyDataSection*>(p)->section->openSection(PyString_AS_STRING(key));if(!section){PyErr_SetObject(PyExc_KeyError,key);return NULL;}return new PyDataSection(section);}
static PyObject*writeIntData(PyObject*p,PyObject*args){char*key;int v;if(!PyArg_ParseTuple(args,"si",&key,&v))return NULL;static_cast<PyDataSection*>(p)->section->writeInt(key,v);Py_RETURN_NONE;}
static PyObject*readIntData(PyObject*p,PyObject*args){char*key;int fallback=0;if(!PyArg_ParseTuple(args,"s|i",&key,&fallback))return NULL;PyObject*v=PyDict_GetItemString(static_cast<PyDataSection*>(p)->section->values,key);return PyInt_FromLong(v?PyInt_AsLong(v):fallback);}
static PyMethodDef dataMethods[]={{"writeInt",writeIntData,METH_VARARGS,NULL},{"readInt",readIntData,METH_VARARGS,NULL},{NULL,NULL,0,NULL}};
static PyMappingMethods mapping={NULL,subscriptData,NULL};
static PyObject*load(PyObject*,PyObject*args){int n;if(!PyArg_ParseTuple(args,"i",&n))return NULL;DataSectionPtr root;if(n>=0){root=std::make_shared<DataSection>();if(n)root->writeInt("existing",1);}stockRecordPreferencesLoaded(root);if(!s_scriptsPreferences){s_scriptsPreferences=std::make_shared<DataSection>();s_scriptsPreferences->writeInt("userValue",71);}Py_RETURN_NONE;}
static PyObject*save(PyObject*,PyObject*){return PyBool_FromLong(savePreferences());}
static PyObject*readMode(PyObject*,PyObject*args){int mode;if(!PyArg_ParseTuple(args,"i",&mode))return NULL;stockNotifyReadPreferences(mode);Py_RETURN_NONE;}
static PyObject*root(PyObject*,PyObject*){if(!DataResource::lastSaved)Py_RETURN_NONE;return new PyDataSection(DataResource::lastSaved);}
static PyObject*watch(PyObject*,PyObject*){return PyBool_FromLong(BWResource::watched);}
static PyObject*failSave(PyObject*,PyObject*args){int b;if(!PyArg_ParseTuple(args,"i",&b))return NULL;DataResource::fail=b!=0;Py_RETURN_NONE;}
static PyObject*emptyPath(PyObject*,PyObject*args){int b;if(!PyArg_ParseTuple(args,"i",&b))return NULL;fixtureDevice.preferencesFilename_.path=b?"":"preferences.xml";Py_RETURN_NONE;}
static PyObject*fini(PyObject*,PyObject*){finiStockPreferencesCallbacks();DataResource::lastSaved.reset();s_scriptsPreferences.reset();Py_RETURN_NONE;}
#define EXPORT(name) static PyObject*wrap_##name(PyObject*,PyObject*args){return py_##name(args);}
EXPORT(wg_subscribeToSavePreferences) EXPORT(wg_subscribeToReadPreferences) EXPORT(wg_unsubAllSavePreferences) EXPORT(wg_preferencesExistedAtStartup)
static PyMethodDef exports[]={
 {"wg_subscribeToSavePreferences",wrap_wg_subscribeToSavePreferences,METH_VARARGS,NULL},
 {"wg_subscribeToReadPreferences",wrap_wg_subscribeToReadPreferences,METH_VARARGS,NULL},
 {"wg_unsubAllSavePreferences",wrap_wg_unsubAllSavePreferences,METH_VARARGS,NULL},
 {"wg_preferencesExistedAtStartup",wrap_wg_preferencesExistedAtStartup,METH_VARARGS,NULL},
 {"savePreferences",save,METH_NOARGS,NULL},{"_read",readMode,METH_VARARGS,NULL},{"_load",load,METH_VARARGS,NULL},
 {"_watch",watch,METH_NOARGS,NULL},{"_root",root,METH_NOARGS,NULL},{"_fail",failSave,METH_VARARGS,NULL},{"_emptyPath",emptyPath,METH_VARARGS,NULL},{"_fini",fini,METH_NOARGS,NULL},{NULL,NULL,0,NULL}};
}
PyMODINIT_FUNC initpreferences_owner(){
 BW::dataType.tp_name=const_cast<char*>("preferences_owner.DataSection");BW::dataType.tp_basicsize=sizeof(BW::PyDataSection);BW::dataType.tp_flags=Py_TPFLAGS_DEFAULT;
 BW::dataType.tp_dealloc=BW::deallocData;BW::dataType.tp_as_mapping=&BW::mapping;BW::dataType.tp_methods=BW::dataMethods;
 if(PyType_Ready(&BW::dataType)<0)return;Py_InitModule("preferences_owner",BW::exports);
}
