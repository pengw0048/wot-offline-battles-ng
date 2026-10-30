#!/usr/bin/env python3
"""Add one identity record for each multiply-stale SDK model-node instance."""
import argparse
import os
from pathlib import Path
import stat
import tempfile

HEADER = 'lib/duplo/pymodelnode.hpp'
SOURCE = 'lib/duplo/pymodelnode.cpp'
FIELDS = '\tuint32 frameTimestamp_;\n\tuint32 lastLatchTimestamp_;'
NEW_FIELDS = FIELDS + '''
\tmutable bool reportedStaleIdentity_;
\tvoid reportStaleIdentity( const char * reader, uint32 current ) const;'''
INIT = '\t, lastLatchTimestamp_( 0 )'
NEW_INIT = INIT + '\n\t, reportedStaleIdentity_( false )'
ANCHOR = '\n/**\n *\tConstructor\n */\nPyModelNode::PyModelNode('
HELPER = '''
#if ENABLE_TRANSFORM_VALIDATION
/**
 * Record identity once without evaluating any matrix provider. The owner clears
 * pOwner_ through detach() before releasing a retained node during destruction.
 */
void PyModelNode::reportStaleIdentity( const char * reader, uint32 current ) const
{
\tconst uint32 age = current - lastLatchTimestamp_;
\tif (age <= 1 || reportedStaleIdentity_)
\t\treturn;
\treportedStaleIdentity_ = true;

\tconst PyModel * owner = pOwner_;
\tconst BW::string model = owner ? owner->name() : "<detached>";
\tconst BW::string node = pNode_->identifier();
\tconst bool inWorld = owner && owner->isInWorld();
\tconst bool attached = owner && owner->isAttached();
\tWARNING_MSG( "PyModelNode::staleIdentity: reader=%s instance=%p owner=%p "
\t\t"model=\\"%s\\" node=%p name=\\"%s\\" current=%u lastLatch=%u "
\t\t"lastAnimation=%u age=%u inWorld=%u attached=%u lodded=%u\\n",
\t\treader, static_cast<const void *>( this ),
\t\tstatic_cast<const void *>( owner ), model.c_str(),
\t\tstatic_cast<const void *>( pNode_.getObject() ), node.c_str(),
\t\tstatic_cast<unsigned>( current ),
\t\tstatic_cast<unsigned>( lastLatchTimestamp_ ),
\t\tstatic_cast<unsigned>( frameTimestamp_ ), static_cast<unsigned>( age ),
\t\tstatic_cast<unsigned>( inWorld ), static_cast<unsigned>( attached ),
\t\tstatic_cast<unsigned>( lodded_ ) );
}
#endif // ENABLE_TRANSFORM_VALIDATION

'''


def replace_once(text, before, after):
    if text.count(after) == 1 and before not in text.replace(after, '', 1):
        return text
    if text.count(before) != 1 or after in text:
        raise ValueError('Model-node identity seam is partial or changed')
    return text.replace(before, after, 1)


def patch_text(text, path):
    if path == HEADER:
        if 'reportedStaleIdentity_' in text and NEW_FIELDS not in text:
            raise ValueError('Model-node identity declaration changed')
        return replace_once(text, FIELDS, NEW_FIELDS)
    if path != SOURCE:
        raise ValueError('Unexpected SDK model-node path')
    if 'reportedStaleIdentity_(' in text and NEW_INIT not in text:
        raise ValueError('Model-node identity initialization changed')
    if 'void PyModelNode::reportStaleIdentity(' in text and HELPER not in text:
        raise ValueError('Model-node identity helper changed')
    text = replace_once(text, INIT, NEW_INIT)
    text = replace_once(text, ANCHOR, HELPER + ANCHOR)
    for reader in ('matrix', 'getMatrix'):
        before = '\t\tWARNING_MSG( "PyModelNode::%s: "' % reader
        after = ('\t\tthis->reportStaleIdentity( "%s", Moo::rc().frameTimestamp() );\n' % reader) + before
        if ('this->reportStaleIdentity( "%s"' % reader) in text and after not in text:
            raise ValueError('Model-node identity reader changed')
        text = replace_once(text, before, after)
    return text


def patch(root):
    updates = []
    for name in (HEADER, SOURCE):
        path = Path(root) / name
        data = path.read_bytes()
        bom = b'\xef\xbb\xbf' if data.startswith(b'\xef\xbb\xbf') else b''
        text = data[len(bom):].decode('utf-8')
        crlf = text.count('\r\n')
        if (crlf and (crlf != text.count('\n') or crlf != text.count('\r'))) or (not crlf and '\r' in text):
            raise ValueError('Unsupported line endings in ' + name)
        newline = '\r\n' if crlf else '\n'
        original = text.replace('\r\n', '\n')
        updated = patch_text(original, name)
        if updated != original:
            updates.append((path, bom + updated.replace('\n', newline).encode('utf-8')))
    # Validate both files before writing either one.
    for path, data in updates:
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + '.', delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(data)
            temporary.chmod(stat.S_IMODE(path.stat().st_mode))
            os.replace(temporary, path)
        finally:
            if temporary is not None and temporary.exists():
                temporary.unlink()
    return bool(updates)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
