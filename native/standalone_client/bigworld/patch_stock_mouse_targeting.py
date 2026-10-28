"""Restore #1513 MouseTargetingMatrix detach and focused in-window sampling.

Apply after patch_stock_hangar_presentation.py: MouseCursor::s_inFocus_ is the
existing focus owner, updated by App's real focus dispatch. Detach freezes only
the provider's sampled screen coordinate, not the current camera transform.
"""
import argparse
from pathlib import Path

HEADER_BEFORE='''\tPY_AUTO_FACTORY_DECLARE( MouseTargetingMatrix, END )
};'''
HEADER_AFTER='''\tPY_AUTO_FACTORY_DECLARE( MouseTargetingMatrix, END )

\tvoid detach( bool value );
\tPY_AUTO_METHOD_DECLARE( RETVOID, detach, ARG( bool, END ) )

private:
\tmutable Vector2 mousePosition_;
\tbool detached_;
};'''
CTOR_BEFORE='''MouseTargetingMatrix::MouseTargetingMatrix( PyTypeObject * pType ):
\tMatrixProvider( false, pType )'''
CTOR_AFTER='''MouseTargetingMatrix::MouseTargetingMatrix( PyTypeObject * pType ):
\tMatrixProvider( false, pType ),
\tmousePosition_( SimpleGUI::instance().mouseCursor().position() ),
\tdetached_( false )'''
POSITION_BEFORE='''\tVector2 mpos = SimpleGUI::instance().mouseCursor().position();'''
POSITION_AFTER='''\t// #1513 00668f63..00668fbb: focus, inclusive screen bounds, then
\t// detach. Preserve the last valid screen coordinate while the GUI owns
\t// the cursor; the ray below still uses the current camera on every read.
\tMouseCursor & cursor = SimpleGUI::instance().mouseCursor();
\tif (MouseCursor::s_inFocus_)
\t{
\t\tconst Vector2 position = cursor.position();
\t\tif (position.x >= -1.f && position.x <= 1.f &&
\t\t\tposition.y >= -1.f && position.y <= 1.f && !detached_)
\t\t{
\t\t\tmousePosition_ = cursor.position();
\t\t}
\t}
\tconst Vector2 mpos = mousePosition_;'''
DETACH='''
// #1513 00667610: the bool is provider-local; there is no input registration
// to remove, and toggling it does not itself take another cursor sample.
void MouseTargetingMatrix::detach( bool value )
{
\tdetached_ = value;
}

'''


def patch(root):
    root=Path(root)
    header=root/'client/entity_picker.hpp';cpp=root/'client/entity_picker.cpp'
    pending={header:header.read_text(),cpp:cpp.read_text()}
    edits=((header,HEADER_BEFORE,HEADER_AFTER),
           (cpp,'PY_BEGIN_METHODS( MouseTargetingMatrix )',
            'PY_BEGIN_METHODS( MouseTargetingMatrix )\n\tPY_METHOD( detach )'),
           (cpp,CTOR_BEFORE,CTOR_AFTER),(cpp,POSITION_BEFORE,POSITION_AFTER),
           (cpp,'PY_FACTORY( MouseTargetingMatrix, BigWorld )',
            DETACH+'PY_FACTORY( MouseTargetingMatrix, BigWorld )'))
    for path,before,after in edits:
        source=pending[path]
        if after not in source:
            if source.count(before)!=1:
                raise ValueError('The pinned mouse targeting seam changed: '+path.name+': '+before)
            source=source.replace(before,after,1)
        pending[path]=source
    for path,source in pending.items():path.write_text(source)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root',type=Path)
    patch(parser.parse_args().engine_root)
