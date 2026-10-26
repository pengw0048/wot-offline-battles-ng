#include "pch.hpp"
#include "stock_space_data.hpp"
#include "entitydef/base_user_data_object_description.hpp"
#include "entitydef/data_type.hpp"
#include "pyscript/script.hpp"
#include "resmgr/bwresource.hpp"
#include "space/client_space.hpp"
#include "space/space_manager.hpp"
#include <cstddef>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

BW_BEGIN_NAMESPACE
namespace
{
class SpaceDescription : public BaseUserDataObjectDescription
{
public:
    bool clientServerProperty( const char * name ) const
    {
        DataDescription * property = findProperty( name );
        return property && clientServerProperty( static_cast<unsigned>( property->index() ) );
    }
    bool clientServerProperty( unsigned index ) const
    {
        return index < exposed_.size() && exposed_[index];
    }

protected:
    virtual const BW::string getDefsDir() const { return "scripts/space_defs"; }
    virtual bool parseProperties( DataSectionPtr properties, const BW::string & component )
    {
        if (!properties) return true;
        for (DataSectionIterator item = properties->begin(); item != properties->end(); ++item)
        {
            DataDescription property;
            if (!property.parse( *item, name_, component, NULL, DataDescription::PARSE_IGNORE_FLAGS )) return false;
            PropertyMap & names = getComponentProperties( component.c_str() );
            PropertyMap::const_iterator previous = names.find( property.name().c_str() );
            const unsigned index = previous == names.end() ? static_cast<unsigned>( properties_.size() ) : previous->second;
            property.index( static_cast<int>( index ) );
            names[property.name().c_str()] = index;
            const bool exposed = (*item)->openSection( "Exposed" ) != NULL;
            // #1513 7ff040 ignores Flags here, then ORs OWN_CLIENT only when
            // the Exposed element is present, including an empty element.
            if (index == properties_.size())
            {
                properties_.push_back( property );
                exposed_.push_back( exposed );
            }
            else
            {
                properties_[index] = property;
                exposed_[index] = exposed;
            }
        }
        int clientIndex = 0;
        for (unsigned index = 0; index < properties_.size(); ++index)
            properties_[index].clientServerFullIndex( exposed_[index] ? clientIndex++ : -1 );
        return true;
    }
private:
    std::vector<bool> exposed_;
};

struct PyStockSpace
{
    PyObject_HEAD
    PyObject * dictionary;
    SpaceDescription * description;
};

struct SpaceEntry
{
    PyObject * object;
    const ClientSpace * nativeOwner;
};
using SpaceMap = std::map<SpaceID, SpaceEntry>;

struct PyStockSpaces
{
    PyObject_HEAD
    PyObject * dictionary;
    SpaceMap * entries;
};

PyTypeObject spaceType = { PyObject_HEAD_INIT( NULL ) 0 };
PyTypeObject spacesType = { PyObject_HEAD_INIT( NULL ) 0 };
std::vector<std::unique_ptr<SpaceDescription>> definitions;
std::set<PyStockSpace *> liveSpaces;
PyStockSpaces * activeSpaces = NULL;
bool active = false;
bool typesReady = false;

PyObject * nativeTypeRepr( PyObject * object )
{
#ifdef _WIN32
    return PyString_FromFormat( "%s at 0x%p", Py_TYPE( object )->tp_name, object );
#else
    return PyString_FromFormat( "%s at %p", Py_TYPE( object )->tp_name, object );
#endif
}

PyObject * cannotConstruct( PyTypeObject * type, PyObject *, PyObject * )
{
    PyErr_Format( PyExc_TypeError, "%s() Cannot directly construct objects of this type", type->tp_name );
    return NULL;
}

int spaceTraverse( PyStockSpace * self, visitproc visit, void * arg )
{
    Py_VISIT( self->dictionary );
    return 0;
}

int spaceClear( PyStockSpace * self )
{
    Py_CLEAR( self->dictionary );
    return 0;
}

void spaceDeallocate( PyStockSpace * self )
{
    PyObject_GC_UnTrack( self );
    liveSpaces.erase( self );
    self->description = NULL;
    spaceClear( self );
    Py_TYPE( self )->tp_free( reinterpret_cast<PyObject *>( self ) );
}

int spaceSetAttribute( PyObject * object, PyObject * name, PyObject * value )
{
    auto * self = reinterpret_cast<PyStockSpace *>( object );
    // Native 680030 routes deletion directly to the generic attribute path.
    if (!value) return PyObject_GenericSetAttr( object, name, NULL );
    if (!self->description)
    {
        PyErr_SetString( PyExc_RuntimeError, "The native space-data owner has been finalised" );
        return -1;
    }
    if (PyString_Check( name ))
    {
        const char * key = PyString_AS_STRING( name );
        DataDescription * property = self->description->findProperty( key );
        if (property && self->description->clientServerProperty( key ) &&
            !property->dataType()->isSameType( ScriptObject( value, ScriptObject::FROM_BORROWED_REFERENCE ) ))
        {
            PyErr_Format( PyExc_TypeError, "Attempted to set attribute %s on %s to an invalid value.",
                key, self->description->name().c_str() );
            return -1;
        }
    }
    return PyObject_GenericSetAttr( object, name, value );
}

void clearEntries( PyStockSpaces * self )
{
    if (!self->entries) return;
    SpaceMap retired;
    retired.swap( *self->entries );
    // Refcount callbacks observe an already-empty mapping and cannot invalidate
    // this iteration or erase a newly published replacement with the same ID.
    for (const auto & item : retired) Py_DECREF( item.second.object );
}

int spacesTraverse( PyStockSpaces * self, visitproc visit, void * arg )
{
    Py_VISIT( self->dictionary );
    if (self->entries)
        for (const auto & item : *self->entries) Py_VISIT( item.second.object );
    return 0;
}

int spacesClear( PyStockSpaces * self )
{
    clearEntries( self );
    Py_CLEAR( self->dictionary );
    return 0;
}

void spacesDeallocate( PyStockSpaces * self )
{
    PyObject_GC_UnTrack( self );
    spacesClear( self );
    delete self->entries;
    self->entries = NULL;
    Py_TYPE( self )->tp_free( reinterpret_cast<PyObject *>( self ) );
}

bool spaceKey( PyObject * value, SpaceID & id )
{
    return Script::setData( value, id, "spaceID" ) == 0;
}

Py_ssize_t spacesLength( PyStockSpaces * self )
{
    return self->entries ? static_cast<Py_ssize_t>( self->entries->size() ) : 0;
}

PyObject * spacesSubscript( PyStockSpaces * self, PyObject * key )
{
    SpaceID id;
    if (!spaceKey( key, id )) return NULL;
    if (self->entries)
    {
        auto found = self->entries->find( id );
        if (found != self->entries->end())
        {
            Py_INCREF( found->second.object );
            return found->second.object;
        }
    }
    PyErr_Format( PyExc_KeyError, "No space(%d) exists.", id );
    return NULL;
}

PyObject * spacesHasKey( PyStockSpaces * self, PyObject * args )
{
    PyObject * key;
    if (!PyArg_ParseTuple( args, "O:has_key", &key )) return NULL;
    SpaceID id;
    if (!spaceKey( key, id )) return NULL;
    return PyBool_FromLong( self->entries && self->entries->count( id ) != 0 );
}

PyObject * spacesGet( PyStockSpaces * self, PyObject * args )
{
    PyObject * key, * fallback = Py_None;
    if (!PyArg_ParseTuple( args, "O|O:get", &key, &fallback )) return NULL;
    SpaceID id;
    if (!spaceKey( key, id )) return NULL;
    if (self->entries)
    {
        auto found = self->entries->find( id );
        if (found != self->entries->end()) fallback = found->second.object;
    }
    Py_INCREF( fallback );
    return fallback;
}

PyObject * spacesKeys( PyStockSpaces * self, PyObject * )
{
    PyObject * result = PyList_New( spacesLength( self ) );
    if (!result) return NULL;
    Py_ssize_t index = 0;
    if (self->entries)
        for (const auto & item : *self->entries)
        {
            PyObject * key = Script::getData( item.first );
            if (!key) { Py_DECREF( result ); return NULL; }
            PyList_SET_ITEM( result, index++, key );
        }
    return result;
}

PyObject * spacesValues( PyStockSpaces * self, PyObject * )
{
    PyObject * result = PyList_New( spacesLength( self ) );
    if (!result) return NULL;
    Py_ssize_t index = 0;
    if (self->entries)
        for (const auto & item : *self->entries)
        {
            Py_INCREF( item.second.object );
            PyList_SET_ITEM( result, index++, item.second.object );
        }
    return result;
}

PyObject * spacesItems( PyStockSpaces * self, PyObject * )
{
    PyObject * result = PyList_New( spacesLength( self ) );
    if (!result) return NULL;
    Py_ssize_t index = 0;
    if (self->entries)
        for (const auto & item : *self->entries)
        {
            PyObject * key = Script::getData( item.first );
            PyObject * pair = key ? PyTuple_Pack( 2, key, item.second.object ) : NULL;
            Py_XDECREF( key );
            if (!pair) { Py_DECREF( result ); return NULL; }
            PyList_SET_ITEM( result, index++, pair );
        }
    return result;
}

PyMethodDef spacesMethods[] = {
    { "has_key", reinterpret_cast<PyCFunction>( spacesHasKey ), METH_VARARGS, NULL },
    { "get", reinterpret_cast<PyCFunction>( spacesGet ), METH_VARARGS, NULL },
    { "keys", reinterpret_cast<PyCFunction>( spacesKeys ), METH_NOARGS, NULL },
    { "items", reinterpret_cast<PyCFunction>( spacesItems ), METH_NOARGS, NULL },
    { "values", reinterpret_cast<PyCFunction>( spacesValues ), METH_NOARGS, NULL },
    { NULL, NULL, 0, NULL }
};
PyMappingMethods spacesMapping = { reinterpret_cast<lenfunc>( spacesLength ),
    reinterpret_cast<binaryfunc>( spacesSubscript ), NULL };

PyObject * getSpaceItemsVisibilityMask( PyObject *, PyObject * args )
{
    PyObject * argument;
    SpaceID id;
    if (!PyArg_ParseTuple( args, "O:wg_getSpaceItemsVisibilityMask", &argument ) ||
        Script::setData( argument, id, "() argument 1" ) != 0) return NULL;
    // f631f0 validates SpaceID and returns the literal zero, even if the space
    // does not exist. It is not the network mapping's typed-mask producer.
    return PyInt_FromLong( 0 );
}

PyObject * setSpaceItemsVisibilityMask( PyObject *, PyObject * args )
{
    PyObject * first, * second;
    SpaceID id;
    uint32 mask;
    if (!PyArg_ParseTuple( args, "OO:wg_setSpaceItemsVisibilityMask", &first, &second ) ||
        Script::setData( first, id, "() argument 1" ) != 0 ||
        Script::setData( second, mask, "() argument 2" ) != 0) return NULL;
    // f63260's reviewed native callee is genuinely empty. In particular it
    // does not mutate PySpace.itemsVisibilityMask or any existing mapping.
    Py_RETURN_NONE;
}

PyMethodDef visibilityMethods[] = {
    { "wg_getSpaceItemsVisibilityMask", getSpaceItemsVisibilityMask, METH_VARARGS, NULL },
    { "wg_setSpaceItemsVisibilityMask", setSpaceItemsVisibilityMask, METH_VARARGS, NULL },
    { NULL, NULL, 0, NULL }
};

PyObject * instanceDictionary( PyObject * object, void * )
{
    PyObject ** dictionary = _PyObject_GetDictPtr( object );
    if (!dictionary)
    {
        PyErr_SetString( PyExc_AttributeError, "This object has no instance dictionary" );
        return NULL;
    }
    if (!*dictionary && !(*dictionary = PyDict_New())) return NULL;
    Py_INCREF( *dictionary );
    return *dictionary;
}
PyGetSetDef dictionaryAttribute[] = { { const_cast<char *>( "__dict__" ), instanceDictionary, NULL, NULL, NULL },
    { NULL, NULL, NULL, NULL, NULL } };

bool initialiseTypes()
{
    if (typesReady) return true;
    spaceType.tp_name = const_cast<char *>( "PySpace" );
    spaceType.tp_basicsize = sizeof( PyStockSpace );
    spaceType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC;
    spaceType.tp_dealloc = reinterpret_cast<destructor>( spaceDeallocate );
    spaceType.tp_repr = nativeTypeRepr;
    spaceType.tp_getattro = PyObject_GenericGetAttr;
    spaceType.tp_setattro = spaceSetAttribute;
    spaceType.tp_dictoffset = offsetof( PyStockSpace, dictionary );
    spaceType.tp_traverse = reinterpret_cast<traverseproc>( spaceTraverse );
    spaceType.tp_clear = reinterpret_cast<inquiry>( spaceClear );
    spaceType.tp_getset = dictionaryAttribute;
    spaceType.tp_new = cannotConstruct;
    if (PyType_Ready( &spaceType ) < 0) return false;
    spacesType.tp_name = const_cast<char *>( "PySpaces" );
    spacesType.tp_basicsize = sizeof( PyStockSpaces );
    spacesType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC;
    spacesType.tp_dealloc = reinterpret_cast<destructor>( spacesDeallocate );
    spacesType.tp_repr = nativeTypeRepr;
    spacesType.tp_getattro = PyObject_GenericGetAttr;
    spacesType.tp_setattro = PyObject_GenericSetAttr;
    spacesType.tp_dictoffset = offsetof( PyStockSpaces, dictionary );
    spacesType.tp_traverse = reinterpret_cast<traverseproc>( spacesTraverse );
    spacesType.tp_clear = reinterpret_cast<inquiry>( spacesClear );
    spacesType.tp_getset = dictionaryAttribute;
    spacesType.tp_methods = spacesMethods;
    spacesType.tp_as_mapping = &spacesMapping;
    spacesType.tp_new = cannotConstruct;
    if (PyType_Ready( &spacesType ) < 0) return false;
    typesReady = true;
    return true;
}

PyObject * createSpaceObject( SpaceDescription & description )
{
    auto * object = reinterpret_cast<PyStockSpace *>( spaceType.tp_alloc( &spaceType, 0 ) );
    if (!object) return NULL;
    object->description = &description;
    object->dictionary = PyDict_New();
    try { liveSpaces.insert( object ); }
    catch (const std::bad_alloc &)
    {
        Py_DECREF( reinterpret_cast<PyObject *>( object ) );
        return PyErr_NoMemory();
    }
    if (!object->dictionary)
    {
        Py_DECREF( reinterpret_cast<PyObject *>( object ) );
        return NULL;
    }
    for (unsigned index = 0; index < description.propertyCount(); ++index)
    {
        if (!description.clientServerProperty( index )) continue;
        DataDescription * property = description.property( index );
        ScriptObject value = property->pInitialValue();
        if (!value || PyObject_SetAttrString( reinterpret_cast<PyObject *>( object ), property->name().c_str(), value.get() ) != 0)
        {
            Py_DECREF( reinterpret_cast<PyObject *>( object ) );
            if (!PyErr_Occurred()) PyErr_SetString( PyExc_RuntimeError, "Cannot initialise the original space property" );
            return NULL;
        }
    }
    return reinterpret_cast<PyObject *>( object );
}

bool loadDefinitions( std::vector<std::unique_ptr<SpaceDescription>> & result )
{
    DataSectionPtr spaces = BWResource::openSection( "scripts/spaces.xml" );
    if (!spaces)
    {
        PyErr_SetString( PyExc_IOError, "Cannot read scripts/spaces.xml" );
        return false;
    }
    for (DataSectionIterator item = spaces->begin(); item != spaces->end(); ++item)
    {
        if (result.size() >= 0xffff)
        {
            PyErr_SetString( PyExc_ValueError, "Space definitions exceed the native 16-bit type table" );
            return false;
        }
        std::unique_ptr<SpaceDescription> description( new SpaceDescription );
        if (!description->parse( (*item)->sectionName() ))
        {
            if (!PyErr_Occurred()) PyErr_Format( PyExc_RuntimeError, "Cannot parse space definition %s", (*item)->sectionName().c_str() );
            return false;
        }
        result.push_back( std::move( description ) );
    }
    return true;
}
}

bool initialiseStockSpaceData()
{
    if (active) return true;
    if (!initialiseTypes()) return false;
    std::vector<std::unique_ptr<SpaceDescription>> loaded;
    try
    {
        if (!loadDefinitions( loaded )) return false;
    }
    catch (const std::bad_alloc &) { PyErr_NoMemory(); return false; }
    auto * mapping = reinterpret_cast<PyStockSpaces *>( spacesType.tp_alloc( &spacesType, 0 ) );
    if (!mapping) return false;
    mapping->dictionary = PyDict_New();
    try { mapping->entries = new SpaceMap; }
    catch (const std::bad_alloc &)
    {
        Py_DECREF( reinterpret_cast<PyObject *>( mapping ) );
        PyErr_NoMemory();
        return false;
    }
    if (!mapping->dictionary)
    {
        Py_DECREF( reinterpret_cast<PyObject *>( mapping ) );
        return false;
    }
    // Install callback-visible owner state before replacing a module attribute
    // whose previous value could run a synchronous Python finaliser.
    definitions.swap( loaded );
    activeSpaces = mapping;
    active = true;
    PyObject * module = PyImport_AddModule( "BigWorld" );
    bool registered = module != NULL;
    for (PyMethodDef * method = visibilityMethods; registered && method->ml_name; ++method)
    {
        ScriptObject function( PyCFunction_New( method, NULL ), ScriptObject::FROM_NEW_REFERENCE );
        registered = function && PyObject_SetAttrString( module, method->ml_name, function.get() ) == 0;
    }
    if (!registered || PyObject_SetAttrString( module, "spaces", reinterpret_cast<PyObject *>( mapping ) ) != 0)
    {
        finaliseStockSpaceData();
        return false;
    }
    return true;
}

void finaliseStockSpaceData()
{
    active = false;
    PyStockSpaces * retired = activeSpaces;
    activeSpaces = NULL;
    if (retired) clearEntries( retired );
    // External Python references may outlive their native space. Detach every
    // surviving object's C++ schema before Script/DataType teardown, while its
    // ordinary Python dictionary remains owned by that object.
    for (auto * space : liveSpaces) space->description = NULL;
    definitions.clear();
    Py_XDECREF( reinterpret_cast<PyObject *>( retired ) );
}

bool publishStockSpaceData( SpaceID id, uint16 type )
{
    if (!active || !activeSpaces)
    {
        PyErr_SetString( PyExc_RuntimeError, "The native space-data owner is unavailable" );
        return false;
    }
    // Original factory 67b320 reserves -1 for a local space with no schema;
    // Space::init 684640 does not publish a PySpace for that branch.
    if (type == 0xffff) return true;
    if (type >= definitions.size())
    {
        PyErr_Format( PyExc_ValueError, "No space definition type %u exists.", static_cast<unsigned>( type ) );
        return false;
    }
    ClientSpacePtr native = SpaceManager::instance().space( id );
    if (!native || native->cleared())
    {
        PyErr_Format( PyExc_ValueError, "No active native space(%d) exists.", id );
        return false;
    }
    PyStockSpaces * destination = activeSpaces;
    ScriptObject mappingPin( reinterpret_cast<PyObject *>( destination ), ScriptObject::FROM_BORROWED_REFERENCE );
    ScriptObject created;
    try { created = ScriptObject( createSpaceObject( *definitions[type] ), ScriptObject::FROM_NEW_REFERENCE ); }
    catch (const std::bad_alloc &) { PyErr_NoMemory(); return false; }
    if (!created) return false;
    if (!active || activeSpaces != destination || native->cleared() || SpaceManager::instance().space( id ) != native.get())
    {
        PyErr_SetString( PyExc_RuntimeError, "The native space changed during typed-object construction" );
        return false;
    }
    PyObject * old = NULL;
    try
    {
        auto found = destination->entries->find( id );
        if (found == destination->entries->end())
            destination->entries->emplace( id, SpaceEntry{ created.get(), native.get() } );
        else
        {
            old = found->second.object;
            found->second = SpaceEntry{ created.get(), native.get() };
        }
        Py_INCREF( created.get() );
    }
    catch (const std::bad_alloc &) { PyErr_NoMemory(); return false; }
    // created pins its own independent reference across the replaced object's
    // finaliser; a reentrant retirement cannot destroy the constructor's value.
    Py_XDECREF( old );
    if (!active || activeSpaces != destination || native->cleared() || SpaceManager::instance().space( id ) != native.get())
    {
        PyErr_SetString( PyExc_RuntimeError, "The native space retired during typed-object publication" );
        return false;
    }
    auto published = destination->entries->find( id );
    if (published == destination->entries->end() || published->second.object != created.get())
    {
        PyErr_SetString( PyExc_RuntimeError, "The typed space was replaced during publication" );
        return false;
    }
    return true;
}

void retireStockSpaceData( SpaceID id, const ClientSpace * expectedOwner )
{
    if (!active || !activeSpaces) return;
    auto found = activeSpaces->entries->find( id );
    if (found == activeSpaces->entries->end() || found->second.nativeOwner != expectedOwner) return;
    PyObject * retired = found->second.object;
    activeSpaces->entries->erase( found );
    Py_DECREF( retired );
}

bool stockSpaceItemsVisibilityMask( SpaceID id, uint32 & mask )
{
    if (!active || !activeSpaces)
    {
        PyErr_SetString( PyExc_RuntimeError, "The native space-data owner is unavailable" );
        return false;
    }
    auto found = activeSpaces->entries->find( id );
    if (found == activeSpaces->entries->end())
    {
        PyErr_Format( PyExc_KeyError, "No space(%d) exists.", id );
        return false;
    }
    ScriptObject value( PyObject_GetAttrString( found->second.object, "itemsVisibilityMask" ), ScriptObject::FROM_NEW_REFERENCE );
    uint32 actual;
    if (!value || Script::setData( value.get(), actual, "itemsVisibilityMask" ) != 0) return false;
    mask = actual ? actual : 0xffffffff;
    return true;
}
BW_END_NAMESPACE
