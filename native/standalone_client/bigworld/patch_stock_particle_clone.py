"""Expose #1513 PyMetaParticleSystem.clone through the existing native owner."""
import argparse
from pathlib import Path


CLONE = '''
// #1513 009dba40 ignores positional and keyword arguments. The native
// MetaParticleSystem owner retains its inverse-render-group rejection and
// clones definitions/actions/renderers without copying live particles.
PyObject * PyMetaParticleSystem::py_clone( PyObject * /*args*/ )
{
    BW_GUARD;
    MetaParticleSystemPtr copy = pSystem_->clone();
    if (!copy) Py_RETURN_NONE;
    return new PyMetaParticleSystem( copy );
}
'''


def replace(source, before, after, name):
    if after in source:
        return source
    if source.count(before) != 1:
        raise ValueError('The pinned particle clone seam changed: ' + name)
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = []
    path = root / 'lib/particle/py_meta_particle_system.hpp'
    before = '\tPY_METHOD_DECLARE( py_clear )'
    source = replace(path.read_text(), before,
                     before + '\n\tPY_METHOD_DECLARE( py_clone )', path.name)
    pending.append((path, source))
    path = root / 'lib/particle/py_meta_particle_system.cpp'
    source = replace(path.read_text(), '\tPY_METHOD( clear )',
                     '\tPY_METHOD( clear )\n\tPY_METHOD( clone )', path.name)
    before = '''float PyMetaParticleSystem::duration() const
{
\tBW_GUARD;
\treturn pSystem_->duration();
}'''
    source = replace(source, before, before + '\n' + CLONE, path.name)
    pending.append((path, source))
    for path, source in pending:
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
