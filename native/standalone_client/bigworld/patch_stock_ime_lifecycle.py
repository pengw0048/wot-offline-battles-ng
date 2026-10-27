#!/usr/bin/env python3
"""Install the #1513 LangBar input-language owner on the Scaleform loader."""
import argparse
from pathlib import Path

HELPERS = ('stock_language_bar.hpp', 'stock_language_bar_commands.hpp')
MANAGER = 'lib/scaleform/manager.cpp'
ENGINE_EDITS = {MANAGER: (
    ('#include "ime.hpp"\n', '#include "ime.hpp"\n#include "stock_language_bar.hpp"\n'),
    # #1513 DeviceApp installs its IME owner before any movie is created.
    ('\t\tpLoader_->SetAS2Support(pAS2Support);\n',
     '\t\tpLoader_->SetAS2Support(pAS2Support);\n'
     '\t\t::wot::stock_language_bar::install( *pLoader_ );\n'),
)}
ENGINE_TOKENS = {MANAGER: (('stock_language_bar', 2),)}
# The optional Win32 manager stays off, so this is the loader's only IME owner.
ENGINE_ANCHORS = (('lib/scaleform/config.hpp', '#define SCALEFORM_IME 0\n'),)

IME_MANAGER = 'Src/GFx/IME/GFx_IMEManager.h'
# Focus and unload callers reach these before any movie has been made active.
SDK_EDITS = {IME_MANAGER: (
    ('\t\treturn GetASIMEManager()->HandleFocus(pmovie, poldFocusedItem, pnewFocusingItem, ptopMostItem);\n',
     '\t\tASIMEManager* pasimemanager = GetASIMEManager();\n'
     '\t\treturn pasimemanager ? pasimemanager->HandleFocus(pmovie, poldFocusedItem, pnewFocusingItem, '
     'ptopMostItem) : pnewFocusingItem;\n'),
    ('\tbool IsTextFieldFocused() const {return pASIMEManager->IsTextFieldFocused();};\n',
     '\tbool IsTextFieldFocused() const {return pASIMEManager && pASIMEManager->IsTextFieldFocused();};\n'),
    ('\t\treturn pASIMEManager->IsTextFieldFocused(ptextfield);\n',
     '\t\treturn pASIMEManager && pASIMEManager->IsTextFieldFocused(ptextfield);\n'),
)}
SDK_TOKENS = {IME_MANAGER: (('pasimemanager ? pasimemanager->HandleFocus', 1), ('pASIMEManager && ', 2))}


def state(source, edits, tokens):
    old = [source.count(before) for before, unused in edits]
    new = [source.count(after) for unused, after in edits]
    found = [source.count(token) for token, unused in tokens]
    # An insertion keeps its anchor, so that anchor still occurs once when final.
    kept = [int(before in after) for before, after in edits]
    if old == kept and new == [1] * len(edits) and found == [count for unused, count in tokens]:
        return 'final'
    if old == [1] * len(edits) and not any(new) and not any(found):
        return 'pristine'
    return None


def patch_source(label, source, edits, tokens):
    # Pinned SDK headers use CRLF; match and keep the file's own line endings.
    newline = '\r\n' if '\r\n' in source else '\n'
    edits = [(before.replace('\n', newline), after.replace('\n', newline)) for before, after in edits]
    tokens = [(token.replace('\n', newline), count) for token, count in tokens]
    current = state(source, edits, tokens)
    if current == 'final':
        return source
    if current is None:
        raise ValueError('Pinned %s IME owner seam is partial, duplicated or changed' % label)
    for before, after in edits:
        source = source.replace(before, after, 1)
    if state(source, edits, tokens) != 'final':
        raise ValueError('%s IME owner patch did not reach its final state' % label)
    return source


def read(path):
    # latin-1 round-trips every byte; the edits themselves are ASCII.
    return path.read_bytes().decode('latin-1')


def plan(root, edits, tokens):
    changes = []
    for relative in edits:
        path = Path(root) / relative
        source = read(path)
        changes.append((path, source, patch_source(relative, source, edits[relative], tokens[relative])))
    return changes


def patch(engine_root=None, sdk_root=None):
    # Validate every requested file before writing any.
    changes = []
    if engine_root is not None:
        for relative, anchor in ENGINE_ANCHORS:
            if read(Path(engine_root) / relative).replace('\r\n', '\n').count(anchor) != 1:
                raise ValueError('Pinned %s no longer disables the optional Win32 IME manager' % relative)
        changes += plan(engine_root, ENGINE_EDITS, ENGINE_TOKENS)
    if sdk_root is not None:
        changes += plan(sdk_root, SDK_EDITS, SDK_TOKENS)
    for path, before, after in changes:
        if after != before:
            path.write_bytes(after.encode('latin-1'))
    if engine_root is not None:
        for helper in HELPERS:
            target = Path(engine_root) / 'lib/scaleform' / helper
            data = Path(__file__).with_name(helper).read_bytes()
            if not target.is_file() or target.read_bytes() != data:
                target.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-only', action='store_true')
    parser.add_argument('source_roots', type=Path, nargs='+')
    args = parser.parse_args()
    if args.sdk_only:
        if len(args.source_roots) != 1:
            parser.error('--sdk-only takes one SDK source root')
        patch(sdk_root=args.source_roots[0])
    else:
        if len(args.source_roots) != 2:
            parser.error('Pass the engine and SDK source roots')
        patch(*args.source_roots)
