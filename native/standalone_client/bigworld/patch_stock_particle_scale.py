"""Restore #1513 particle wrapper scale and its explicit deferred render chain.

Run after patch_stock_battleground_elements.py. Scale is a wrapper-local float;
only drawing consumes it. Simulation, definitions, bounds and clone owners retain
SDK behavior. Point/line/mesh/visual renderers do not gain invented size factors.
"""
import argparse
from pathlib import Path

# Exact pinned SDK seams, including the already generated gas-split draw entry.
SEAMS = [('lib/particle/py_meta_particle_system.hpp', '\tfloat lod_;', '\tfloat lod_;\n\tfloat scale_;'),
 ('lib/particle/py_meta_particle_system.hpp',
  '\tPY_METHOD_DECLARE( py_clear )',
  '\tPY_RW_ATTRIBUTE_DECLARE( scale_, scale )\n\tPY_METHOD_DECLARE( py_clear )'),
 ('lib/particle/py_meta_particle_system.cpp', '\tlod_( 0.0f )', '\tlod_( 0.0f ),\n\tscale_( 1.f )'),
 ('lib/particle/py_meta_particle_system.cpp',
  'PY_BEGIN_ATTRIBUTES( PyMetaParticleSystem )',
  'PY_BEGIN_ATTRIBUTES( PyMetaParticleSystem )\n\tPY_ATTRIBUTE( scale )'),
 ('lib/particle/py_meta_particle_system.cpp',
  'pSystem_->draw( drawContext, world, lod_ );',
  'pSystem_->draw( drawContext, world, lod_, scale_ );'),
 ('lib/particle/py_meta_particle_system.cpp',
  'pSystem_->draw( tempDrawContext, Matrix::identity, 0.f );',
  'pSystem_->draw( tempDrawContext, Matrix::identity, 0.f, scale_ );'),
 ('lib/particle/py_particle_system.hpp',
  '\tPySTLSequenceHolder<ParticleSystem::Actions> actionsHolder_;',
  '\tPySTLSequenceHolder<ParticleSystem::Actions> actionsHolder_;\n\tfloat scale_;'),
 ('lib/particle/py_particle_system.hpp',
  '\tPY_RW_ATTRIBUTE_DECLARE( actionsHolder_, actions )',
  '\tPY_RW_ATTRIBUTE_DECLARE( scale_, scale )\n\tPY_RW_ATTRIBUTE_DECLARE( actionsHolder_, actions )'),
 ('lib/particle/py_particle_system.cpp',
  '\tactionsHolder_( pSystem->actionSet(), this, true )',
  '\tactionsHolder_( pSystem->actionSet(), this, true ),\n\tscale_( 1.f )'),
 ('lib/particle/py_particle_system.cpp',
  'PY_BEGIN_ATTRIBUTES( PyParticleSystem )',
  'PY_BEGIN_ATTRIBUTES( PyParticleSystem )\n\tPY_ATTRIBUTE( scale )'),
 ('lib/particle/py_particle_system.cpp',
  'pSystem_->draw( drawContext, world, lod_ );',
  'pSystem_->draw( drawContext, world, lod_, scale_ );'),
 ('lib/particle/meta_particle_system.hpp',
  'void draw( Moo::DrawContext& drawContext, const Matrix & worldTransform, float lod );',
  'void draw( Moo::DrawContext& drawContext, const Matrix & worldTransform, float lod, float scale = '
  '1.f );'),
 ('lib/particle/meta_particle_system.cpp',
  'void MetaParticleSystem::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\tconst Matrix & world, float lod )\n',
  'void MetaParticleSystem::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\tconst Matrix & world, float lod, float scale )\n'),
 ('lib/particle/meta_particle_system.cpp',
  '(*it)->draw( drawContext, world, lod );',
  '(*it)->draw( drawContext, world, lod, scale );'),
 ('lib/particle/particle_system.hpp',
  'void draw( Moo::DrawContext& drawContext, const Matrix & worldTransform, float lod );',
  'void draw( Moo::DrawContext& drawContext, const Matrix & worldTransform, float lod, float scale = '
  '1.f );'),
 ('lib/particle/particle_system.cpp',
  'void ParticleSystem::draw( Moo::DrawContext& drawContext, const Matrix &world, float lod )\n',
  'void ParticleSystem::draw( Moo::DrawContext& drawContext, const Matrix &world, float lod, float '
  'scale )\n'),
 ('lib/particle/particle_system.cpp',
  '#include "pch.hpp"',
  '#include "pch.hpp"\n#include "stock_particle_scale.hpp"'),
 ('lib/particle/particle_system.cpp',
  '\t\tMatrix w( world );\n\t\tw.preTranslateBy( localOffset_ );',
  '\t\tconst Matrix w = stockParticleScaleTransform( world, localOffset_, scale );'),
 ('lib/particle/particle_system.cpp',
  'pRenderer_->draw( drawContext, w, this->begin(), this->end(), this->boundingBox() );',
  'pRenderer_->draw( drawContext, w, this->begin(), this->end(), this->boundingBox(), scale );'),
 ('lib/particle/renderers/particle_system_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext, const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext, const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/particle_system_renderer.hpp',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/sprite_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/sprite_particle_renderer.hpp',
  'virtual void realDraw( const Matrix& worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix& worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/sprite_particle_renderer.cpp',
  'void SpriteParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '                                  const Matrix& worldTransform,\n'
  '                                  Particles::iterator beg,\n'
  '                                  Particles::iterator end,\n'
  '                                  const BoundingBox &bb )',
  'void SpriteParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '                                  const Matrix& worldTransform,\n'
  '                                  Particles::iterator beg,\n'
  '                                  Particles::iterator end,\n'
  '                                  const BoundingBox &bb, float scale )'),
 ('lib/particle/renderers/sprite_particle_renderer.cpp',
  'void SpriteParticleRenderer::realDraw( const Matrix& worldTransform, Particles::iterator beg, '
  'Particles::iterator end )',
  'void SpriteParticleRenderer::realDraw( const Matrix& worldTransform, Particles::iterator beg, '
  'Particles::iterator end, float scale )'),
 ('lib/particle/renderers/sprite_particle_renderer.cpp',
  'sortedDrawItem_.set( this, worldTransform, beg, end );',
  'sortedDrawItem_.set( this, worldTransform, beg, end, scale );'),
 ('lib/particle/renderers/sprite_particle_renderer.cpp',
  '    const Matrix * worldTransform_;',
  '    const Matrix * worldTransform_;\n    float scale_;'),
 ('lib/particle/renderers/sprite_particle_renderer.cpp',
  '        const float size = p.size();',
  '        const float size = p.size() * scale_;'),
 ('lib/particle/renderers/sprite_particle_renderer.cpp',
  '    job->worldTransform_ = &worldTransform;',
  '    job->worldTransform_ = &worldTransform;\n    job->scale_ = scale;'),
 ('lib/particle/renderers/point_sprite_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/point_sprite_particle_renderer.hpp',
  'void realDraw( const Matrix& worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'void realDraw( const Matrix& worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/point_sprite_particle_renderer.cpp',
  'void PointSpriteParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\t\t   const Matrix& worldTransform,\n'
  '\t\t\t\t\t\t\t\t\t   Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t\t\t   Particles::iterator end,\n'
  '\t\t\t\t\t\t\t\t\t   const BoundingBox &bb )',
  'void PointSpriteParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\t\t   const Matrix& worldTransform,\n'
  '\t\t\t\t\t\t\t\t\t   Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t\t\t   Particles::iterator end,\n'
  '\t\t\t\t\t\t\t\t\t   const BoundingBox &bb, float scale )'),
 ('lib/particle/renderers/point_sprite_particle_renderer.cpp',
  'void PointSpriteParticleRenderer::realDraw( const Matrix& worldTransform, Particles::iterator beg, '
  'Particles::iterator end )',
  'void PointSpriteParticleRenderer::realDraw( const Matrix& worldTransform, Particles::iterator beg, '
  'Particles::iterator end, float /*scale*/ )'),
 ('lib/particle/renderers/point_sprite_particle_renderer.cpp',
  'sortedDrawItem_.set( this, worldTransform, beg, end );',
  'sortedDrawItem_.set( this, worldTransform, beg, end, scale );'),
 ('lib/particle/renderers/mesh_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/mesh_particle_renderer.hpp',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/mesh_particle_renderer.cpp',
  'void MeshParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\tconst Matrix& worldTransform,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator beg,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator end,\n'
  '\t\t\t\t\t\t\t\tconst BoundingBox & inbb )',
  'void MeshParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\tconst Matrix& worldTransform,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator beg,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator end,\n'
  '\t\t\t\t\t\t\t\tconst BoundingBox & inbb, float /*scale*/ )'),
 ('lib/particle/renderers/visual_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/visual_particle_renderer.hpp',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/visual_particle_renderer.cpp',
  'void VisualParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\t  const Matrix& worldTransform,\n'
  '\t\t\t\t\t\t\t\t  Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t\t  Particles::iterator end,\n'
  '\t\t\t\t\t\t\t\t  const BoundingBox & inbb )',
  'void VisualParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\t  const Matrix& worldTransform,\n'
  '\t\t\t\t\t\t\t\t  Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t\t  Particles::iterator end,\n'
  '\t\t\t\t\t\t\t\t  const BoundingBox & inbb, float /*scale*/ )'),
 ('lib/particle/renderers/amp_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/amp_particle_renderer.hpp',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/amp_particle_renderer.cpp',
  'void AmpParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t   const Matrix & worldTransform,\n'
  '\t\t\t\t\t\t\t   Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t   Particles::iterator end,\n'
  '\t\t\t\t\t\t\t   const BoundingBox & inbb )',
  'void AmpParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t   const Matrix & worldTransform,\n'
  '\t\t\t\t\t\t\t   Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t   Particles::iterator end,\n'
  '\t\t\t\t\t\t\t   const BoundingBox & inbb, float scale )'),
 ('lib/particle/renderers/amp_particle_renderer.cpp',
  'void AmpParticleRenderer::realDraw( const Matrix & worldTransform,\n'
  '\tParticles::iterator beg,\n'
  '\tParticles::iterator end )',
  'void AmpParticleRenderer::realDraw( const Matrix & worldTransform,\n'
  '\tParticles::iterator beg,\n'
  '\tParticles::iterator end, float /*scale*/ )'),
 ('lib/particle/renderers/amp_particle_renderer.cpp',
  'sortedDrawItem_.set( this, worldTransform, beg, end );',
  'sortedDrawItem_.set( this, worldTransform, beg, end, scale );'),
 ('lib/particle/renderers/blur_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/blur_particle_renderer.hpp',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/blur_particle_renderer.cpp',
  'void BlurParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\tconst Matrix & worldTransform,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator beg,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator end,\n'
  '\t\t\t\t\t\t\t\tconst BoundingBox & inbb )',
  'void BlurParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\tconst Matrix & worldTransform,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator beg,\n'
  '\t\t\t\t\t\t\t\tParticles::iterator end,\n'
  '\t\t\t\t\t\t\t\tconst BoundingBox & inbb, float scale )'),
 ('lib/particle/renderers/blur_particle_renderer.cpp',
  'void BlurParticleRenderer::realDraw( const Matrix & worldTransform,\n'
  '\tParticles::iterator beg,\n'
  '\tParticles::iterator end )',
  'void BlurParticleRenderer::realDraw( const Matrix & worldTransform,\n'
  '\tParticles::iterator beg,\n'
  '\tParticles::iterator end, float /*scale*/ )'),
 ('lib/particle/renderers/blur_particle_renderer.cpp',
  'sortedDrawItem_.set( this, worldTransform, beg, end );',
  'sortedDrawItem_.set( this, worldTransform, beg, end, scale );'),
 ('lib/particle/renderers/trail_particle_renderer.hpp',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb )',
  'virtual void draw( Moo::DrawContext& drawContext,\n'
  '\t\tconst Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end,\n'
  '\t\tconst BoundingBox & bb, float scale = 1.f )'),
 ('lib/particle/renderers/trail_particle_renderer.hpp',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end )',
  'virtual void realDraw( const Matrix & worldTransform,\n'
  '\t\tParticles::iterator beg,\n'
  '\t\tParticles::iterator end, float scale = 1.f )'),
 ('lib/particle/renderers/trail_particle_renderer.cpp',
  'void TrailParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\t const Matrix & worldTransform,\n'
  '\t\t\t\t\t\t\t\t Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t\t Particles::iterator end,\n'
  '\t\t\t\t\t\t\t\t const BoundingBox & inbb )',
  'void TrailParticleRenderer::draw( Moo::DrawContext& drawContext,\n'
  '\t\t\t\t\t\t\t\t const Matrix & worldTransform,\n'
  '\t\t\t\t\t\t\t\t Particles::iterator beg,\n'
  '\t\t\t\t\t\t\t\t Particles::iterator end,\n'
  '\t\t\t\t\t\t\t\t const BoundingBox & inbb, float scale )'),
 ('lib/particle/renderers/trail_particle_renderer.cpp',
  'void TrailParticleRenderer::realDraw( const Matrix & worldTransform,\n'
  '\tParticles::iterator beg,\n'
  '\tParticles::iterator end )',
  'void TrailParticleRenderer::realDraw( const Matrix & worldTransform,\n'
  '\tParticles::iterator beg,\n'
  '\tParticles::iterator end, float /*scale*/ )'),
 ('lib/particle/renderers/trail_particle_renderer.cpp',
  'sortedDrawItem_.set( this, worldTransform, beg, end );',
  'sortedDrawItem_.set( this, worldTransform, beg, end, scale );'),
 ('lib/particle/renderers/particle_system_draw_item.hpp',
  'worldTransform_( Matrix::identity )',
  'worldTransform_( Matrix::identity ),\n\t\tscale_( 1.f )'),
 ('lib/particle/renderers/particle_system_draw_item.hpp',
  'Particles::iterator end );',
  'Particles::iterator end, float scale = 1.f );'),
 ('lib/particle/renderers/particle_system_draw_item.hpp',
  '\tParticles::iterator             end_;',
  '\tParticles::iterator             end_;\n\tfloat                          scale_;'),
 ('lib/particle/renderers/particle_system_draw_item.cpp',
  'Particles::iterator     end )',
  'Particles::iterator     end, float scale )'),
 ('lib/particle/renderers/particle_system_draw_item.cpp',
  '\tend_ = end;',
  '\tend_ = end;\n\tscale_ = scale;'),
 ('lib/particle/renderers/particle_system_draw_item.cpp',
  'void ParticleSystemDrawItem::draw()\n'
  '{\n'
  '\tBW_GUARD;\n'
  '\trenderer_->realDraw( worldTransform_, beg_, end_ );',
  'void ParticleSystemDrawItem::draw()\n'
  '{\n'
  '\tBW_GUARD;\n'
  '\trenderer_->realDraw( worldTransform_, beg_, end_, scale_ );'),
 ('lib/particle/renderers/particle_system_draw_item.cpp',
  'drawStockBattlegroundParticles( renderer_, worldTransform_, beg_, end_ )',
  'drawStockBattlegroundParticles( renderer_, worldTransform_, beg_, end_, scale_ )'),
 ('lib/particle/renderers/particle_system_draw_item.cpp',
  '\t\trenderer_->realDraw( worldTransform_, beg_, end_ );',
  '\t\trenderer_->realDraw( worldTransform_, beg_, end_, scale_ );')]


def patch(root):
    root = Path(root)
    pending = {}
    for name, before, after in SEAMS:
        path = root / name
        source = pending.get(path)
        if source is None:
            source = path.read_text()
        if after not in source:
            if source.count(before) != 1:
                raise ValueError('The pinned particle scale seam changed: ' + name + ': ' + before)
            source = source.replace(before, after, 1)
        pending[path] = source
    header = Path(__file__).with_name('stock_particle_scale.hpp').read_text()
    pending[root / 'lib/stock_particle_scale.hpp'] = header
    for path, source in pending.items():
        path.write_text(source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
