#!/usr/bin/env python3
"""Install the #1513 Scaleform string translator and WG.getFormattedLocalizedString owner."""
import argparse
from pathlib import Path

INCLUDE = '#include "stock_scaleform_localization.hpp"\n'
MANAGER = (
    ('#include "manager.hpp"\n', '#include "manager.hpp"\n' + INCLUDE),
    # 00621af0: the loader translator is installed once after Manager init.
    ('\t\tpLoader_->SetFontLib(pFontLib_);\n',
     '\t\tpLoader_->SetFontLib(pFontLib_);\n'
     '\t\tinstallStockTranslator( *pLoader_ );\n'),
)
MOVIE_VIEW = (
    ('#include "py_movie_view.hpp"\n', '#include "py_movie_view.hpp"\n' + INCLUDE),
    # 00aa4cc5: WGExtensions is registered before the external interface.
    ('\t\tgameDelegate_ =  *new FxDelegate;\n',
     '\t\tgameDelegate_ =  *new FxDelegate;\n'
     '\t\tregisterStockLocalization( *gameDelegate_ );\n'),
)
FILES = (('lib/scaleform/manager.cpp', MANAGER), ('lib/scaleform/py_movie_view.cpp', MOVIE_VIEW))
# Each token occurs only in the lines this patch adds.
TOKENS = {'lib/scaleform/manager.cpp': (('stock_scaleform_localization', 1), ('installStockTranslator', 1),
                                        ('registerStockLocalization', 0)),
          'lib/scaleform/py_movie_view.cpp': (('stock_scaleform_localization', 1),
                                              ('installStockTranslator', 0),
                                              ('registerStockLocalization', 1))}
HELPER = 'stock_scaleform_localization.hpp'


def state(source, edits, tokens):
    old = [source.count(before) for before, unused in edits]
    new = [source.count(after) for unused, after in edits]
    found = [source.count(token) for token, unused in tokens]
    # Each insertion retains its anchor, so a final anchor also occurs exactly once.
    if old == new == [1] * len(edits) and found == [count for unused, count in tokens]:
        return 'final'
    if old == [1] * len(edits) and not any(found):
        return 'pristine'
    return None


def patch_source(relative, source):
    edits, tokens = dict(FILES)[relative], TOKENS[relative]
    current = state(source, edits, tokens)
    if current == 'final':
        return source
    if current is None:
        raise ValueError('Pinned %s localization seam is partial, duplicated or changed' % relative)
    for before, after in edits:
        source = source.replace(before, after, 1)
    if state(source, edits, tokens) != 'final':
        raise ValueError('%s localization patch did not reach its final state' % relative)
    return source


def patch(root):
    # Validate both files before writing either.
    changes = []
    for relative, unused in FILES:
        path = Path(root) / relative
        source = path.read_text(encoding='utf-8')
        changes.append((path, source, patch_source(relative, source)))
    for path, before, after in changes:
        if after != before:
            path.write_text(after, encoding='utf-8')
    helper = Path(__file__).with_name(HELPER).read_bytes()
    (Path(root) / 'lib' / HELPER).write_bytes(helper)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
