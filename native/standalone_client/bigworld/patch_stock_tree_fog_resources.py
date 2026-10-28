#!/usr/bin/env python3
"""Route legacy world-position/depth fog calls to the actual public fog owner.

The pinned r10 resources retain two-argument vertexFog calls in both billboard
reflection paths. Their shared header exposes that contract as bw_vertexFog;
vertexFog itself is the separate scalar/start/end helper. Scan the complete
source tree so every retained old two-argument call is handled together.
"""
import argparse
from pathlib import Path
import re


TOKENS = re.compile(r'//[^\r\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', re.DOTALL)
CALL = re.compile(r'\bvertexFog\s*\(')
OWNER = re.compile(r'\bfloat\s+bw_vertexFog\s*\(\s*in\s+float4\s+wPos\s*,\s*in\s+float\s+linearZ\s*\)')


def legacy_calls(source):
    """Return old two-argument call spans, preserving comments and nesting."""
    code = TOKENS.sub(lambda match: ' ' * len(match.group()), source)
    calls = []
    for match in CALL.finditer(code):
        start = match.end()
        depth = 1
        commas = 0
        end = start
        while end < len(code) and depth:
            char = code[end]
            if char == '(':
                depth += 1
            elif char == ')':
                depth -= 1
            elif char == ',' and depth == 1:
                commas += 1
            end += 1
        if depth:
            raise ValueError('Unclosed vertexFog call')
        if commas == 1 and code[start:end - 1].strip():
            calls.append((match.start(), match.start() + len('vertexFog')))
    return calls


def patch_resources(root):
    root = Path(root)
    helper = root / 'shaders/std_effects/fog_helpers.fxh'
    if not OWNER.search(helper.read_bytes().decode('latin1')):
        raise ValueError('Pinned world-position/depth fog owner changed: ' + str(helper))
    pending = []
    for path in sorted((root / 'shaders').rglob('*')):
        if path.suffix not in ('.fx', '.fxh'):
            continue
        text = path.read_bytes().decode('latin1')
        calls = legacy_calls(text)
        if not calls:
            continue
        for begin, end in reversed(calls):
            text = text[:begin] + 'bw_vertexFog' + text[end:]
        pending.append((path, text.encode('latin1')))
    # Validate every source before publishing any edit. Existing source cache
    # directories use the same path; no compiled shader cache is deleted.
    for path, data in pending:
        path.write_bytes(data)
    return [path.relative_to(root).as_posix() for path, unused in pending]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('resources', type=Path)
    args = parser.parse_args()
    changed = patch_resources(args.resources)
    print('World-position/depth fog calls restored: ' + ', '.join(changed or ['already current']))
