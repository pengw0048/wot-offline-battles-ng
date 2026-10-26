#!/usr/bin/env python3
"""Install the original BWLC1 scene owner and runtime light intensity state."""
import argparse
from pathlib import Path


TRANSFORMS = {
    'omni': '''void OmniLight::stockCompiledWorldTransform(const Matrix& transform)
{
    // #1513 97b8d0 takes the first three components of mapping row zero.
    worldPosition_ = transform.applyPoint(position_);
    const Vector4& row = transform.row(0);
    const float scale = sqrtf(row.x * row.x + row.y * row.y + row.z * row.z);
    worldInnerRadius_ = innerRadius_ * scale;
    worldOuterRadius_ = outerRadius_ * scale;
    const Vector3 extent(outerRadius_, outerRadius_, outerRadius_);
    worldBounds_ = BoundingBox(position_ - extent, position_ + extent);
    worldBounds_.transformBy(transform);
}

''',
    'spot': '''void SpotLight::stockCompiledWorldTransform(const Matrix& transform)
{
    // #1513 97c490 uses the complete first row and forward direction transform.
    worldPosition_ = transform.applyPoint(position_);
    const Vector4& row = transform.row(0);
    const float scale = sqrtf(row.x * row.x + row.y * row.y + row.z * row.z + row.w * row.w);
    worldInnerRadius_ = innerRadius_ * scale;
    worldOuterRadius_ = outerRadius_ * scale;
    worldDirection_ = transform.applyVector(direction_);
    worldDirection_.normalise();
    dirty_ = true;
}

'''}


def apply(root):
    root = Path(root)
    pending = {}

    def edit(relative, old, new):
        source = pending.get(relative, (root / relative).read_text(encoding='utf-8-sig'))
        if new in source:
            return
        if source.count(old) != 1:
            raise ValueError('Original light owner anchor changed: ' + relative + ': ' + repr(old))
        pending[relative] = source.replace(old, new, 1)

    for kind in ('omni', 'spot'):
        cls = 'OmniLight' if kind == 'omni' else 'SpotLight'
        header = 'lib/moo/' + kind + '_light.hpp'
        edit(header, '\tvoid\t\t\t\tworldTransform( const Matrix& transform );',
             '\tvoid stockCompiledWorldTransform(const Matrix& transform);\n'
             '\tvoid\t\t\t\tworldTransform( const Matrix& transform );')
        edit(header, '''#ifdef EDITOR_ENABLED
	float				multiplier() const { return multiplier_; }
	void				multiplier( float m ) { multiplier_ = m; }
#else//EDITOR_ENABLED
	float				multiplier() const { return 1.0f; }
#endif''', '''// #1513 BWLC writes intensity and the source flag byte in client builds.
	float				multiplier() const { return multiplier_; }
	void				multiplier( float m ) { multiplier_ = m; }
	uint8 stockSourceFlags() const { return stockSourceFlags_; }
	void stockSourceFlags(uint8 flags) { stockSourceFlags_ = flags; }''')
        edit(header, '''#ifdef EDITOR_ENABLED
	float				multiplier_;
#endif''', '''	float				multiplier_;
	uint8 stockSourceFlags_ = 0;''')
        cpp = 'lib/moo/' + kind + '_light.cpp'
        source = pending.get(cpp, (root / cpp).read_text(encoding='utf-8-sig'))
        for old in ('''#ifdef EDITOR_ENABLED
  , multiplier_( 1.f )
#endif''', '''#ifdef EDITOR_ENABLED
  ,multiplier_(1.f)
#endif'''):
            # Both actual constructors require the same runtime state. A fully
            # patched TU no longer contains either conditional initializer.
            source = source.replace(old, '  , multiplier_( 1.f )')
        if source.count('multiplier_(') != 2:
            raise ValueError('Original light intensity constructor changed: ' + cpp)
        transform = TRANSFORMS[kind]
        anchor = 'void ' + cls + '::worldTransform( const Matrix& transform )'
        if transform not in source:
            if source.count(anchor) != 1:
                raise ValueError('Original compiled-light transform anchor changed: ' + cpp)
            source = source.replace(anchor, transform + anchor, 1)
        pending[cpp] = source
        ipp = 'lib/moo/' + kind + '_light.ipp'
        source = pending.get(ipp, (root / ipp).read_text(encoding='utf-8-sig'))
        marker = '// #1513 intensity is runtime light state, including compiled client lights.'
        if marker not in source:
            start = source.index('#ifdef EDITOR_ENABLED')
            end = source.index('#endif //-- EDITOR_ENABLED', start) + len('#endif //-- EDITOR_ENABLED')
            branch = source[start:end]
            if branch.count('gpuData.m_color') != 2 or '* multiplier_' not in branch:
                raise ValueError('Original light GPU intensity changed: ' + ipp)
            statement = branch.split('#else')[0].split('\n', 1)[1].rstrip()
            source = source[:start] + marker + '\n' + statement + source[end:]
            pending[ipp] = source

    here = Path(__file__).resolve().parent
    copied = {root / 'lib' / name: (here / name).read_text()
              for name in ('stock_space_lights.hpp', 'stock_space_lights.cpp')}
    copied[root / 'lib/wot/stock_light_binary.hpp'] = (here.parent / 'include/wot/stock_light_binary.hpp').read_text()
    for relative, text in pending.items():
        (root / relative).write_text(text)
    for path, text in copied.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    apply(parser.parse_args().engine_root)
