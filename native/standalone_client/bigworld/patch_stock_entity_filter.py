#!/usr/bin/env python3
"""Restore the pinned Filter lag interface and reentrant filter publication."""
import argparse
from pathlib import Path


def replace_one(source, before, after, path):
    if source.count(after) == 1:
        return source
    if source.count(before) != 1:
        raise ValueError('Unrecognized pinned filter seam: ' + str(path))
    return source.replace(before, after)


def patch_engine(root):
    root = Path(root)
    updates = []
    path = root / 'client/py_filter.hpp'
    source = path.read_text(encoding='utf-8')
    before = '\t// Python Interface\n'
    after = ('\t// #1513 0065d1b0 delegates to the concrete filter owner.\n'
             '\tvirtual bool isLaggingNow() const { return false; }\n'
             '\tPY_RO_ATTRIBUTE_DECLARE( this->isLaggingNow(), isLaggingNow )\n'
             '\tvoid enableLagDetection( bool enabled );\n'
             '\tPY_AUTO_METHOD_DECLARE( RETVOID, enableLagDetection, ARG( bool, END ) )\n\n' + before)
    source = replace_one(source, before, after, path)
    source = replace_one(source, '\tbool isAttached_;',
                         '\tbool isAttached_;\n\tbool stockCreatingFilter_ = false;', path)
    updates.append((path, source))
    path = root / 'client/py_filter.cpp'
    source = path.read_text(encoding='utf-8')
    for before, after in [(
            '#include "py_filter.hpp"', '#include "py_filter.hpp"\n#include "stock_vehicle_filter.hpp"'), (
            '\tPY_METHOD( reset )', '\tPY_METHOD( reset )\n\tPY_METHOD( enableLagDetection )'), (
            'PY_BEGIN_ATTRIBUTES( PyFilter )\n',
            'PY_BEGIN_ATTRIBUTES( PyFilter )\n\tPY_ATTRIBUTE( isLaggingNow )\n'), (
            'PyFilter::~PyFilter()\n{\n}',
            'PyFilter::~PyFilter()\n{\n\tregisterStockFilterLagDetection( this, false );\n}\n\n'
            'void PyFilter::enableLagDetection( bool enabled )\n{\n'
            '\tregisterStockFilterLagDetection( this, enabled );\n}')]:
        source = replace_one(source, before, after, path)
    source = replace_one(source,
                         'void PyFilter::onFilterDestroyed( Filter * pDestroyedFilter )\n{',
                         'void PyFilter::onFilterDestroyed( Filter * pDestroyedFilter )\n{\n'
                         '\t// WG native owners retire before releasing Python callback members.\n'
                         '\tif (pAttachedFilter_ == NULL) return;', path)
    source = replace_one(source,
                         'Filter * PyFilter::getFilter()\n{\n\tpAttachedFilter_ = this->getNewFilter();\n\treturn pAttachedFilter_;\n}',
                         'Filter * PyFilter::getFilter()\n{\n'
                         '\tif (stockCreatingFilter_)\n\t{\n'
                         '\t\tPyErr_SetString( PyExc_RuntimeError, "Filter construction is already active" );\n'
                         '\t\treturn NULL;\n\t}\n'
                         '\tstruct CreationScope\n\t{\n\t\tbool & active;\n'
                         '\t\texplicit CreationScope( bool & value ) : active( value ) { active = true; }\n'
                         '\t\t~CreationScope() { active = false; }\n\t} creationScope( stockCreatingFilter_ );\n'
                         '\tFilter * candidate = this->getNewFilter();\n'
                         '\tif (candidate) pAttachedFilter_ = candidate;\n\treturn candidate;\n}', path)
    updates.append((path, source))
    path = root / 'client/py_entity.cpp'
    source = path.read_text(encoding='utf-8')
    # bindStockVehicleFilterOwner is inserted by the existing vehicle adapter.
    before = ('\tbindStockVehicleFilterOwner( pPyFilter_.get(), this );\n'
              '\tpEntity_->pFilter( pPyFilter_->getFilter() );')
    after = ('\t// A pending WGEntityFilter transfer samples a Python MatrixProvider.\n'
             '\t// Pin both owners and reject a stale candidate after that callback.\n'
             '\tPyObjectPtr ownerPin( this );\n'
             '\tEntityPtr entityPin = pEntity_;\n'
             '\tPyFilterPtr expected = pPyFilter_;\n'
             '\tbindStockVehicleFilterOwner( expected.get(), this );\n'
             '\tFilter * candidate = expected->getFilter();\n'
             '\tif (!candidate) return;\n'
             '\tif (pEntity_ != entityPin || entityPin->isDestroyed() ||\n'
             '\t\t!entityPin->isInAoI() || pPyFilter_ != expected || !expected->isAttached())\n'
             '\t{\n'
             '\t\tif (candidate && expected->pAttachedFilter() == candidate) delete candidate;\n'
             '\t\treturn;\n'
             '\t}\n'
             '\tpEntity_->pFilter( candidate );')
    source = replace_one(source, before, after, path)
    source = replace_one(source,
                         '\tif (pPyFilter_ != NULL)\n\t{\n\t\tbindStockVehicleFilterOwner( pPyFilter_.get(), NULL );',
                         '\t// Keep the previous published owner until candidate creation completes.\n'
                         '\tPyFilterPtr previousFilter = pPyFilter_;\n'
                         '\tEntityPtr previousEntity = pEntity_;\n'
                         '\tMovementFilter * previousNative = pEntity_->pFilter();\n'
                         '\tconst bool previousAttached = previousFilter && previousFilter->isAttached();\n'
                         '\tif (pPyFilter_ != NULL)\n\t{\n\t\tbindStockVehicleFilterOwner( pPyFilter_.get(), NULL );', path)
    source = replace_one(source,
                         '\tif (pEntity_->isInAoI())\n\t{\n\t\tthis->setFilterOnEntity();\n\t}',
                         '\tif (pEntity_->isInAoI())\n\t{\n\t\tthis->setFilterOnEntity();\n'
                          '\t\tif (PyErr_Occurred())\n\t\t{\n'
                         '\t\t\t// Roll back only this failed creation, never a callback-selected owner.\n'
                         '\t\t\tif (pEntity_ == previousEntity && !previousEntity->isDestroyed() &&\n'
                         '\t\t\t\tpPyFilter_ == pNewFilter && previousEntity->pFilter() == previousNative &&\n'
                         '\t\t\t\t(!previousFilter || previousFilter->pAttachedFilter() == previousNative))\n'
                         '\t\t\t{\n'
                         '\t\t\t\tbindStockVehicleFilterOwner( pNewFilter.get(), NULL );\n'
                         '\t\t\t\tpNewFilter->isAttached( false );\n'
                         '\t\t\t\tpPyFilter_ = previousFilter;\n'
                         '\t\t\t\tif (previousFilter)\n\t\t\t\t{\n'
                         '\t\t\t\t\tpreviousFilter->isAttached( previousAttached );\n'
                         '\t\t\t\t\tbindStockVehicleFilterOwner( previousFilter.get(), this );\n'
                         '\t\t\t\t}\n\t\t\t}\n\t\t\treturn -1;\n\t\t}\n\t}', path)
    updates.append((path, source))
    path = root / 'lib/connection/avatar_filter_helper.hpp'
    source = path.read_text(encoding='utf-8')
    source = replace_one(source, '\tfloat latency() const { return latency_; }',
                         '\tbool stockIsLaggingNow() const { return stockExtrapolating_; }\n'
                         '\tfloat latency() const { return latency_; }', path)
    source = replace_one(source, '\tbool\t\t\treset_;',
                         '\tbool\t\t\treset_;\n\tbool stockExtrapolating_ = false;', path)
    updates.append((path, source))
    path = root / 'lib/connection/avatar_filter_helper.cpp'
    source = path.read_text(encoding='utf-8')
    source = replace_one(source, 'void AvatarFilterHelper::init( const AvatarFilterHelper * pHelper )\n{',
                         'void AvatarFilterHelper::init( const AvatarFilterHelper * pHelper )\n{\n'
                         '\tstockExtrapolating_ = pHelper ? pHelper->stockExtrapolating_ : false;', path)
    source = replace_one(source, '\tif (this->getStoredInput( 0 ).time_ < time)\n',
                         '\t// 007ea080 enters lag at equality too; retain the public interpolation below.\n'
                         '\tstockExtrapolating_ = this->getStoredInput( 0 ).time_ <= time;\n\n'
                         '\tif (this->getStoredInput( 0 ).time_ < time)\n', path)
    updates.append((path, source))
    path = root / 'client/avatar_filter.hpp'
    source = path.read_text(encoding='utf-8')
    source = replace_one(source, '\tfloat latency() const { return helper_.latency(); }',
                         '\tbool stockIsLaggingNow() const { return helper_.stockIsLaggingNow(); }\n'
                         '\tfloat latency() const { return helper_.latency(); }', path)
    updates.append((path, source))
    path = root / 'client/py_avatar_filter.hpp'
    source = path.read_text(encoding='utf-8')
    source = replace_one(source, '\tfloat latency() const;',
                         '\tvirtual bool isLaggingNow() const\n\t{\n'
                         '\t\tconst AvatarFilter * filter = this->pAttachedFilter();\n'
                         '\t\treturn !filter || filter->stockIsLaggingNow();\n\t}\n\n'
                         '\tfloat latency() const;', path)
    updates.append((path, source))
    for path, source in updates:
        path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch_engine(parser.parse_args().engine_root)
