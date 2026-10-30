#!/usr/bin/env python3
"""Leave the ARIE full apply of draw-queue passes to the CommitChanges before each draw."""
import argparse
from pathlib import Path


EDITS = {
    'lib/moo/managed_effect.hpp': (
        (r'''	ID3DXEffect* pEffect();
	bool selectMaterialBinding(ComObjectWrap<ID3DXEffect>& binding,
		ComObjectWrap<ID3DXEffect>& definition, CompileMark& mark);

	CompileMark compileMark() const;
''',
         r'''	ID3DXEffect* pEffect();
	bool selectMaterialBinding(ComObjectWrap<ID3DXEffect>& binding,
		ComObjectWrap<ID3DXEffect>& definition, CompileMark& mark, bool commitFollows = false);

	CompileMark compileMark() const;
'''),
        (r'''	virtual bool begin( bool setAutoProps );
	virtual bool beginPass( uint32 pass );
	virtual bool endPass();
	virtual bool end();
''',
         r'''	virtual bool begin( bool setAutoProps );
	virtual bool beginPass( uint32 pass );
	// For callers that CommitChanges before every draw: ARIE applies once there.
	bool beginPassForCommit( uint32 pass );
	virtual bool endPass();
	virtual bool end();
'''),
    ),
    'lib/moo/managed_effect.cpp': (
        (r'''// its private ARIE Binding; the immutable Definition and shared context are reused.
bool ManagedEffect::selectMaterialBinding(ComObjectWrap<ID3DXEffect>& binding,
    ComObjectWrap<ID3DXEffect>& definition, CompileMark& mark)
{
    stockBindingValid_ = false;
''',
         r'''// its private ARIE Binding; the immutable Definition and shared context are reused.
bool ManagedEffect::selectMaterialBinding(ComObjectWrap<ID3DXEffect>& binding,
    ComObjectWrap<ID3DXEffect>& definition, CompileMark& mark, bool commitFollows)
{
    stockBindingValid_ = false;
'''),
        (r'''        {
            // Properties and object constants are written before CommitChanges.
            if (FAILED(pEffect()->BeginPass(pass))) return false;
            stockPass_ = pass;
        }
''',
         r'''        {
            // Properties and object constants are written before CommitChanges.
            if (FAILED(commitFollows ? wot::begin_stock_effect_pass_for_commit(pEffect(), pass) :
                pEffect()->BeginPass(pass))) return false;
            stockPass_ = pass;
        }
'''),
        (r'''    MF_ASSERT( pass < numPasses_ );
    const HRESULT result = pEffect()->BeginPass(pass);
    if (SUCCEEDED(result)) stockPass_ = pass;
    return SUCCEEDED(result);
''',
         r'''    MF_ASSERT( pass < numPasses_ );
    const HRESULT result = pEffect()->BeginPass(pass);
    if (SUCCEEDED(result)) stockPass_ = pass;
    return SUCCEEDED(result);
}

bool ManagedEffect::beginPassForCommit( uint32 pass )
{
    BW_GUARD;
    MF_ASSERT( pEffect_ != NULL );
    MF_ASSERT( this->beginCalled() );
    MF_ASSERT( pass < numPasses_ );
    if (!wot::is_stock_effect(pEffect())) return beginPass(pass);
    const HRESULT result = wot::begin_stock_effect_pass_for_commit(pEffect(), pass);
    if (SUCCEEDED(result)) stockPass_ = pass;
    return SUCCEEDED(result);
'''),
    ),
    'lib/moo/effect_material.hpp': (
        (r'''            resetBinding();
        }
        bool select(ManagedEffect& effect) const
        {
            return effect.selectMaterialBinding(binding_, definition_, compileMark_);
        }
        ID3DXEffect* existingStockBinding() const { return binding_.pComObject(); }
''',
         r'''            resetBinding();
        }
        bool select(ManagedEffect& effect, bool commitFollows = false) const
        {
            return effect.selectMaterialBinding(binding_, definition_, compileMark_, commitFollows);
        }
        ID3DXEffect* existingStockBinding() const { return binding_.pComObject(); }
'''),
    ),
    'lib/moo/draw_context.cpp': (
        (r'''			for (uint32 pass = 0; pass < rop.effect_->numPasses(); ++pass)
			{
				rop.effect_->beginPass( pass );
				// apply material properties
				setEffectMaterialProperties( rop.effect_, rop.materialProperties_ );
''',
         r'''			for (uint32 pass = 0; pass < rop.effect_->numPasses(); ++pass)
			{
				rop.effect_->beginPassForCommit( pass );
				// apply material properties
				setEffectMaterialProperties( rop.effect_, rop.materialProperties_ );
'''),
        (r'''		if (properties)
		{
			if (!properties->select(*effect)) return;
			ID3DXEffect*	pEffect = effect->pEffect();
			MaterialProperties::iterator	it	= properties->begin();
''',
         r'''		if (properties)
		{
			if (!properties->select(*effect, true)) return;
			ID3DXEffect*	pEffect = effect->pEffect();
			MaterialProperties::iterator	it	= properties->begin();
'''),
        (r'''		if (active_.curActivePass_ < active_.numActivePasses_)
		{
			active_.effect_->beginPass( active_.curActivePass_++ );
		}
	}
''',
         r'''		if (active_.curActivePass_ < active_.numActivePasses_)
		{
			active_.effect_->beginPassForCommit( active_.curActivePass_++ );
		}
	}
'''),
        (r'''			// multi pass, proceed to the next pass
			active_.effect_->endPass();
			active_.effect_->beginPass( active_.curActivePass_++ );
			// re-render all render ops from the beginning of the pass marker
			setMaterial( rop, false );
''',
         r'''			// multi pass, proceed to the next pass
			active_.effect_->endPass();
			active_.effect_->beginPassForCommit( active_.curActivePass_++ );
			// re-render all render ops from the beginning of the pass marker
			setMaterial( rop, false );
'''),
    ),
}


def replace_once(text, old, new, label):
    if new in text:
        return text
    if text.count(old) != 1:
        raise RuntimeError("Unexpected public engine source for " + label)
    return text.replace(old, new, 1)


def patch(root):
    for relative, edits in EDITS.items():
        path = Path(root) / relative
        text = path.read_text()
        for old, new in edits:
            text = replace_once(text, old, new, relative)
        path.write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("engine_root", type=Path)
    patch(parser.parse_args().engine_root)


if __name__ == "__main__":
    main()
