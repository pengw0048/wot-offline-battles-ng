"""Restore #1513 effect-light properties on the existing native light owner."""
import argparse
from pathlib import Path


def patch(root):
    root = Path(root)
    pending = {}

    def edit(relative, before, after):
        path = root / relative
        source = pending.get(path, path.read_text(encoding='utf-8-sig'))
        if source.count(after) == 1:
            return
        if source.count(before) != 1:
            raise ValueError('Pinned effect light seam changed: ' + relative + ': ' + repr(before))
        pending[path] = source.replace(before, after, 1)

    header = 'lib/romp/py_omni_light.hpp'
    edit(header, '\tvirtual void recalc() = 0;;',
         '\tvirtual bool castShadows() const = 0;\n'
         '\tvirtual void castShadows( bool value ) = 0;\n\n'
         '\tvirtual void recalc() = 0;;')
    edit(header, '\tPY_RW_ATTRIBUTE_DECLARE( pShader_, shader )',
         '\tPY_RW_ATTRIBUTE_DECLARE( pShader_, shader )\n'
         '\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, castShadows, castShadows )\n'
         '\tPY_RW_ATTRIBUTE_DECLARE( pColorAnimator_, colorAnimator )\n'
         '\tPY_RW_ATTRIBUTE_DECLARE( pMultiplierAnimator_, multiplierAnimator )')
    edit(header, '\tvoid recalc();',
         '\tbool castShadows() const;\n\tvoid castShadows( bool value );\n\n\tvoid recalc();')
    edit(header, '\tVector4ProviderPtr pShader_;',
         '\tVector4ProviderPtr pShader_;\n'
         '\t// #1513 retains these providers, including None/replacement, until\n'
         '\t// destruction. Its compiled-space doTick (006cad60) samples only\n'
         '\t// bounds and shader; it does not consume either animator.\n'
         '\tVector4ProviderPtr pColorAnimator_;\n'
         '\tVector4ProviderPtr pMultiplierAnimator_;')
    source = 'lib/romp/py_omni_light.cpp'
    edit(source, '\tPY_ATTRIBUTE( shader )',
         '\tPY_ATTRIBUTE( shader )\n\tPY_ATTRIBUTE( castShadows )\n'
         '\tPY_ATTRIBUTE( colorAnimator )\n\tPY_ATTRIBUTE( multiplierAnimator )')
    edit(source, 'void PyOmniLight::recalc()',
         'bool PyOmniLight::castShadows() const\n{\n'
         '\treturn pLightEmbodiment_->castShadows();\n}\n\n'
         'void PyOmniLight::castShadows( bool value )\n{\n'
         '\tpLightEmbodiment_->castShadows( value );\n}\n\n'
         'void PyOmniLight::recalc()')

    # Both SDK implementations must satisfy IOmniLight. The supported client
    # selects CompiledSpaceFactory; the old chunk implementation shares the
    # same Moo::OmniLight flag rather than adding a second property owner.
    for directory, stem, name in (
            ('compiled_space', 'omni_light_embodiment', 'OmniLightEmbodiment'),
            ('romp', 'chunk_omni_light_embodiment', 'ChunkOmniLightEmbodiment')):
        edit('lib/' + directory + '/' + stem + '.hpp', '\tvoid outerRadius( float v );',
             '\tvoid outerRadius( float v );\n\n'
             '\tbool castShadows() const;\n\tvoid castShadows( bool value );')
        edit('lib/' + directory + '/' + stem + '.cpp',
             'bool ' + name + '::visible() const',
             'bool ' + name + '::castShadows() const\n{\n'
             '\treturn pLight_->stockSourceFlags() != 0;\n}\n\n'
             'void ' + name + '::castShadows( bool value )\n{\n'
             '\t// #1513 006cab70 writes the native light shadow flag directly.\n'
             '\tpLight_->stockSourceFlags( value ? 1 : 0 );\n}\n\n'
             'bool ' + name + '::visible() const')

    for path, source in pending.items():
        path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
