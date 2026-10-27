"""Start the independent offline entry between game import and game.init."""
import argparse
from pathlib import Path

ANCHOR = '    Personality::callOnInit( /* isReload */ false );\n'
HOOK = ANCHOR + '''
    // The independent product has no retail intro or login owner. Bind the
    // generated offline mod to the original guiModsInit seam before game.init.
    {
        PyObject * pEntry = PyImport_ImportModule( "OfflineGarageEntry" );
        PyObject * pInstalled = pEntry ? PyObject_CallMethod( pEntry,
            const_cast< char * >( "install" ), NULL ) : NULL;
        const bool installed = pInstalled && PyObject_IsTrue( pInstalled ) == 1;
        if (!installed && PyErr_Occurred())
        {
            PyErr_Print();
        }
        Py_XDECREF( pInstalled );
        Py_XDECREF( pEntry );
        if (!installed)
        {
            criticalInitError(
                "App::initPersonality: Could not install the offline garage entry" );
            return false;
        }
    }
'''


def patch(root):
    path = Path(root) / 'client/finale_app.cpp'
    source = path.read_text(encoding='utf-8-sig')
    if HOOK in source:
        return
    if source.count(ANCHOR) != 1:
        raise ValueError('The pinned personality import/init boundary changed')
    path.write_text(source.replace(ANCHOR, HOOK, 1), encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
