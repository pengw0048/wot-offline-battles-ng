#!/usr/bin/env python3
"""Inventory exact #1513 effects and source owner seams, without live-draw claims."""
import argparse
import collections
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import zipfile

from audit_native_interfaces import macro_calls, stripped


def source_inventory(sources):
    registrations, manual, formats = [], [], []
    effects, scaleform = {}, {}
    for file, source in sorted(sources.items()):
        source = stripped(source)
        for pattern in (r'DEFINE_AUTO_CONST\(\s*\w+\s*,\s*"([^"]+)"',
                        r'(?<!un)registerAutoConstant\(\s*[^,]+,\s*"([^"]+)"',
                        r'getMapping\(\s*"([^"]+)"'):
            for match in re.finditer(pattern, source):
                registrations.append(dict(semantic=match[1], file=file,
                    line=source.count('\n', 0, match.start()) + 1,
                    evidence='source candidate; patch scripts retain old/new literals'))
        for match in re.finditer(r'\b(Set(?:Bool|Int|Float|Vector|Matrix|Texture|Value)(?:Array)?)\(\s*"([^"]+)"', source):
            manual.append(dict(setter=match[1], parameter=match[2], file=file,
                line=source.count('\n', 0, match.start()) + 1))
        for match in re.finditer(r'["\'](shaders/[^"\']+\.fx)["\']', source):
            effects.setdefault(match[1], []).append(dict(file=file,
                line=source.count('\n', 0, match.start()) + 1))
        for match in re.finditer(r'\bD3DFMT_\w+', source):
            formats.append(dict(format=match[0], file=file,
                line=source.count('\n', 0, match.start()) + 1))
        if file.startswith('public-sdk/lib/scaleform/'):
            scaleform[file] = [dict(macro=macro, arguments=args,
                line=source.count('\n', 0, at) + 1)
                for macro, args, at in macro_calls(source)
                if macro not in ('PY_BEGIN_METHODS', 'PY_END_METHODS',
                    'PY_BEGIN_ATTRIBUTES', 'PY_END_ATTRIBUTES')]
    return dict(semantic_source_registrations=registrations, manual_setters=manual,
        source_effect_mount_candidates=[dict(resource=name, source_owners=owners,
            live_verified=False) for name, owners in sorted(effects.items())],
        source_target_format_sites=formats, public_scaleform_surface=scaleform)


def unmatched_semantics(effects, registrations):
    registered = {row['semantic'] for row in registrations}
    # The existing owner generates these names from its fixed algorithm array.
    registered.update(('ShadowAlgo', 'ShadowAlgoParams', 'ShadowColorWriteMask'))
    used = collections.defaultdict(set)
    for name, effect in effects.items():
        for uniform in effect['uniforms']:
            if uniform['semantic'] and uniform['used_by_shaders']:
                used[uniform['semantic']].add(name)
    return [dict(semantic=semantic, effects=sorted(names),
        status='unconfirmed: may be manual, material-local, or inactive')
        for semantic, names in sorted(used.items()) if semantic not in registered]


def inventory(repo, client, sdk, output):
    from inspect_client import inspect_client
    inspect_client(client)
    sources = {str(path.relative_to(repo)): path.read_text(errors='replace')
        for path in (repo / 'native/standalone_client/bigworld').rglob('*')
        if path.suffix in ('.cpp', '.hpp', '.py')}
    with tarfile.open(sdk) as archive:
        for member in archive:
            relative = member.name.split('/programming/bigworld/')[-1]
            if (member.isfile() and relative != member.name and
                    relative.startswith(('lib/', 'client/')) and
                    Path(relative).suffix in ('.cpp', '.hpp', '.ipp')):
                sources['public-sdk/' + relative] = archive.extractfile(member).read().decode('utf-8-sig', errors='replace')
    report = source_inventory(sources)
    decoded, errors = {}, []
    with tempfile.TemporaryDirectory(prefix='renderer-abi-') as directory:
        temporary = Path(directory)
        binary = temporary / 'audit-renderer-effects'
        subprocess.run(['c++', '-std=c++17', '-O2', '-I',
            str(repo / 'native/standalone_client/include'),
            str(Path(__file__).with_name('audit_renderer_effects.cpp')),
            *[str(repo / 'native/standalone_client/src' / name) for name in
                ('stock_shader_binary.cpp', 'stock_shader_runtime.cpp', 'stock_effect_binding.cpp')],
            '-o', str(binary)], check=True, capture_output=True)
        with zipfile.ZipFile(client / 'res/packages/shaders.pkg') as archive:
            for name in sorted(archive.namelist()):
                if not name.endswith(('.10.dx9.fxo', '.11.dx9.fxo')):
                    continue
                try:
                    with zipfile.ZipFile(io.BytesIO(archive.read(name))) as effect:
                        data = effect.read('effect')
                    path = temporary / 'effect'
                    path.write_bytes(data)
                    result = subprocess.run([str(binary), str(path)], check=True,
                        capture_output=True, text=True)
                    decoded[name] = json.loads(result.stdout)
                except (ValueError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
                    errors.append(dict(effect=name, error=str(error),
                        decoder_stderr=error.stderr if isinstance(error, subprocess.CalledProcessError) else None))
    report.update(format=1, repo=str(repo), client=str(client), effects=decoded,
        decode_errors=errors, unmatched_used_semantics=unmatched_semantics(
            decoded, report['semantic_source_registrations']),
        source_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
        source_worktree_status=subprocess.check_output(['git', 'status', '--short'], cwd=repo, text=True).splitlines(),
        limitations=['Static inventory, not proof of selected techniques or live draw completeness.',
            'All compiled shader records are scanned with default selector binding; alternative selector combinations and live techniques are not exhaustively enumerated.',
            'Pristine SDK and old/new patch literals are source candidates, not final prepared code.',
            'MFM/XML/configuration/dynamic resource mounts require separate reachability review.',
            'Public Scaleform declarations are not an original-client signature or behavior certificate.'])
    report['counts'] = dict(effects_decoded=len(decoded), decode_errors=len(errors),
        unmatched_used_semantics=len(report['unmatched_used_semantics']),
        scaleform_source_files=sum(bool(rows) for rows in report['public_scaleform_surface'].values()))
    output.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--client', type=Path, required=True)
    parser.add_argument('--sdk-archive', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = inventory(args.repo, args.client, args.sdk_archive, args.output)
    print(json.dumps(report['counts'], sort_keys=True))
    return int(bool(report['decode_errors']))


if __name__ == '__main__':
    raise SystemExit(main())
