"""Restore #1513 particle background-task weak callbacks and failure delivery."""
import argparse
from pathlib import Path


CONSTRUCTOR_BEFORE = '''\tpCallback_( callback ),
\tpSystem_( NULL )
{
}'''
CONSTRUCTOR_AFTER = '''    pSystem_( NULL ),
    // 009ea5f0 holds a weak callback. None and objects that cannot be
    // weak-referenced still admit the resource load, without a callback.
    pCallback_( callback ? PyWeakref_NewRef( callback, NULL ) : NULL, true )
{
    if (!pCallback_) PyErr_Clear();
}'''
LOAD_BEFORE = '''\tpSystem_->load( filename_, "", &errorMsg );
\tif (errorMsg.length() != 0)'''
LOAD_AFTER = '''    // 009ea850 rejects both a failed load and reported resource errors.
    // A live callback receives None instead of an empty native wrapper.
    const bool loaded = pSystem_->load( filename_, "", &errorMsg );
    if (!loaded || errorMsg.length() != 0) pSystem_ = NULL;
    if (errorMsg.length() != 0)'''
COMPLETE_BEFORE = '''void MPSBackgroundTask::doMainThreadTask( TaskManager & mgr )
{
\tBW_GUARD;
\tPyMetaParticleSystem* pMPS = new PyMetaParticleSystem( pSystem_ );
\tPy_INCREF( pCallback_.getObject() );\t\t
\tScript::call( &*(pCallback_),
\t\tPy_BuildValue("(O)", (PyObject *)pMPS),
\t\t"MPSBackgroundTask callback: " );\t\t
\tPy_DECREF( pMPS );
}'''
COMPLETE_AFTER = '''void MPSBackgroundTask::doMainThreadTask( TaskManager & mgr )
{
    BW_GUARD;
    // 009eabd0 constructs the loaded native result even for a retired callback.
    PyObjectPtr result;
    if (pSystem_) result = PyObjectPtr( new PyMetaParticleSystem( pSystem_ ), true );
    else result = Py_None;

    PyObjectPtr callback;
    if (pCallback_)
    {
        PyObject * referent = PyWeakref_GetObject( pCallback_.getObject() );
        if (referent != Py_None) callback = referent;
    }
    // Keep a strong snapshot across Python re-entry, and retire this completion
    // before invoking it. The result wrapper retains its actual native owner.
    pCallback_ = NULL;
    pSystem_ = NULL;
    if (!callback) return;
    Py_INCREF( callback.getObject() ); // Script::call steals this reference.
    Script::call( callback.getObject(), Py_BuildValue( "(O)", result.getObject() ),
        "MPSBackgroundTask callback: " );
}'''


def patch(root):
    path = Path(root) / 'lib/particle/py_meta_particle_system.cpp'
    source = path.read_text()
    for before, after in ((CONSTRUCTOR_BEFORE, CONSTRUCTOR_AFTER),
                          (LOAD_BEFORE, LOAD_AFTER),
                          (COMPLETE_BEFORE, COMPLETE_AFTER)):
        if after in source:
            continue
        if source.count(before) != 1:
            raise ValueError('The pinned particle background seam changed: ' + path.name)
        source = source.replace(before, after, 1)
    path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
