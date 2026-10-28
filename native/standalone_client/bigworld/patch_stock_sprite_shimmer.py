"""Restore #1513 sprite materialFX shader selection and deferred reload.

Run after patch_stock_particle_scale.py. Both shaders are read from the original
resources.xml AutoConfig paths; the original resource packages remain unchanged.
"""
import argparse
from pathlib import Path

SETTER = '''void SpriteParticleRenderer::materialFX( MaterialFX newMaterialFX )
{
    // #1513 a023c0: every assignment restores the configured shader, including
    // repeated modes and transitions away from a custom effectName.
    materialFX_ = newMaterialFX;
    effectName_ = (newMaterialFX == FX_SHIMMER ? s_shimmerEffectFileName :
        s_defaultEffectFileName).value().c_str();
    materialSettingsChanged_ = true;
}
'''
OLD_SETTER = '''INLINE void SpriteParticleRenderer::materialFX(
\tSpriteParticleRenderer::MaterialFX newMaterialFX )
{
\tmaterialFX_ = newMaterialFX;
\tmaterialSettingsChanged_ = true;
}'''


def replace(text, before, after, name):
    if after in text:
        return text
    if text.count(before) != 1:
        raise ValueError('The pinned sprite shimmer seam changed: ' + name)
    return text.replace(before, after, 1)


def patch(root):
    root = Path(root)
    cpp = root / 'lib/particle/renderers/sprite_particle_renderer.cpp'
    ipp = cpp.with_suffix('.ipp')
    source = cpp.read_text()
    default = '''    const AutoConfigString s_defaultEffectFileName(
        "system/spriteParticles/default" );'''
    source = replace(source, default, default + '''
    const AutoConfigString s_shimmerEffectFileName(
        "system/spriteParticles/shimmer" );''', 'configured shaders')
    seam = 'using namespace SpriteParticleRendererDefaults;'
    source = replace(source, seam, seam + '\n\n' + SETTER, 'out-of-line setter')
    before = '''    if (materialSettingsChanged_)
    {
        setMaterialProperties();
    }'''
    after = '''    if (materialSettingsChanged_)
    {
        // #1513 a0146a -> vtable+0x48 (a039f0) reloads the selected shader.
        updateMaterial();
    }'''
    source = replace(source, before, after, 'draw material reload')
    inline = replace(ipp.read_text(), OLD_SETTER,
                     '// materialFX is out-of-line: it selects configured shaders.', 'inline setter')
    # Validate every seam before writing either file.
    cpp.write_text(source)
    ipp.write_text(inline)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
