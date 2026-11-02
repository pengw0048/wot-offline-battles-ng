#!/usr/bin/env python3
"""Repeat the reviewed native/renderer census or its focused contract tests.

A clean review gate means no unreviewed census regressions. It does not mean
that retained gaps, native behavior, images, crashes or retail parity passed.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REVIEW = ROOT / 'tools/evidence/native_baseline_0922/reviewed.json'
PRESENT = {'runtime-export-present', 'runtime-member-present',
           'source-registration-present', 'source-member-present'}
RUNTIME_PRESENT = {'runtime-export-present', 'runtime-member-present'}


def load(path):
    return json.loads(Path(path).read_text())


def scaleform_schema(surface):
    return {name.removeprefix('public-sdk/'): sorted({
        (row['macro'], tuple(''.join(arg.split()) for arg in row['arguments']))
        for row in rows}) for name, rows in surface.items() if rows}


def review_reports(native, renderer, reviewed):
    expected = {name: status for status, names in reviewed['native_statuses'].items()
                for name in names}
    current = {row['native_path']: row for row in native['groups']}
    failures, improvements, retained = [], [], []
    if len(current) != len(native['groups']):
        failures.append(dict(reason='conflicting evidence groups for the same native path'))
    for name in sorted(set(expected) | set(current)):
        old, row = expected.get(name), current.get(name)
        status = row['status'] if row else None
        reason = None
        if row is None:
            reason = 'reviewed exact-client demand disappeared; verify input/census coverage'
        elif old is None and status not in PRESENT:
            reason = 'new missing or unresolved native consumer'
        elif old in RUNTIME_PRESENT and status not in RUNTIME_PRESENT:
            reason = 'lost runtime name/schema evidence'
        elif old in PRESENT and status not in PRESENT:
            reason = 'previous source/runtime name is now absent or unresolved'
        elif old is not None and old not in PRESENT and status not in PRESENT and status != old:
            reason = 'unresolved evidence classification changed; review required'
        if reason:
            failures.append(dict(native_path=name, previous=old, current=status, reason=reason))
        elif old not in PRESENT and status in PRESENT:
            improvements.append(dict(native_path=name, previous=old, current=status,
                behavior_proved=False))
        elif status not in PRESENT:
            retained.append(dict(native_path=name, status=status,
                difference_status=row['difference_status']))
    if not native.get('runtime_snapshot'):
        failures.append(dict(reason='runtime snapshot is missing; source cannot prove exports'))
    for error in renderer['decode_errors']:
        failures.append(dict(reason='exact compiled effect decode failed', detail=error))
    if not renderer['effects']:
        failures.append(dict(reason='empty exact compiled effect census'))
    current_semantics = {row['semantic'] for row in renderer['unmatched_used_semantics']}
    for semantic in sorted(current_semantics - set(reviewed['renderer_unmatched_semantics'])):
        failures.append(dict(reason='unreviewed used semantic without literal provider', semantic=semantic))
    observed = scaleform_schema(renderer['public_scaleform_surface'])
    baseline = {name: [(row[0], tuple(row[1])) for row in rows]
        for name, rows in reviewed['scaleform_source_schema'].items()}
    for name in sorted(set(observed) | set(baseline)):
        if observed.get(name) != baseline.get(name):
            failures.append(dict(reason='public Scaleform declaration schema changed', source=name))
    return dict(format=1, outcome='unreviewed-regressions' if failures else 'reviewed-census-only',
        failures=failures, native_name_improvements=improvements,
        retained_native_gaps_or_unresolved=retained,
        retained_renderer_semantic_candidates=sorted(current_semantics),
        renderer_review=reviewed['renderer_reviews'], gui_review=reviewed['gui_reviews'],
        native_scene_review=reviewed['native_scene_reviews'],
        native_summary=native.get('summary'), renderer_counts=renderer.get('counts'),
        source_revision=native.get('source_revision'),
        source_worktree_status=native.get('source_worktree_status'),
        runtime_snapshot=native.get('runtime_snapshot'),
        limitations=reviewed['limitations'])


def contract_statistics(result):
    return dict(tests=result.testsRun, failures=len(result.failures),
        errors=len(result.errors), skipped=len(result.skipped),
        expected_failures=len(result.expectedFailures),
        unexpected_successes=len(result.unexpectedSuccesses),
        skip_details=[dict(test=test.id(), reason=reason) for test, reason in result.skipped])


def contract_child(group, reviewed, output):
    import unittest
    if group == 'audit':
        suite = unittest.defaultTestLoader.discover(str(ROOT / 'tests'),
            pattern='test_audit_native*.py')
    else:
        sys.path.insert(0, str(ROOT / 'native/standalone_client/tests'))
        suite = unittest.defaultTestLoader.loadTestsFromNames(reviewed['contract_tests'])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    (output / ('contracts-' + group + '-statistics.json')).write_text(
        json.dumps(contract_statistics(result), indent=2, sort_keys=True) + '\n')
    return int(not result.wasSuccessful())


def run_contracts(reviewed, output):
    results = []
    for group in ('audit', 'renderer'):
        command = [sys.executable, '-B', str(Path(__file__).resolve()),
            '--contract-child', group, '--output-dir', str(output.resolve())]
        path = output / ('contracts-' + group + '.log')
        statistics_path = output / ('contracts-' + group + '-statistics.json')
        if statistics_path.exists():
            statistics_path.unlink()
        with path.open('w') as stream:
            result = subprocess.run(command, cwd=ROOT, stdout=stream,
                stderr=subprocess.STDOUT, env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'))
        results.append(dict(command=command, cwd=str(ROOT), returncode=result.returncode,
            log=str(path), statistics=load(statistics_path) if statistics_path.exists() else None,
            evidence='host/optional exact-data contracts; skips are not native success'))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('contracts', 'capture', 'review'))
    parser.add_argument('--contract-child', choices=('audit', 'renderer'), help=argparse.SUPPRESS)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--client', type=Path, default=os.environ.get('WOT_0922_CLIENT'))
    parser.add_argument('--sdk-archive', type=Path)
    parser.add_argument('--original-api', type=Path)
    parser.add_argument('--python27', default=os.environ.get('WOT_PYTHON27'))
    parser.add_argument('--runtime-snapshot', type=Path)
    parser.add_argument('--native-report', type=Path)
    parser.add_argument('--renderer-report', type=Path)
    args = parser.parse_args()
    reviewed = load(REVIEW)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.contract_child:
        return contract_child(args.contract_child, reviewed, args.output_dir)
    if not args.mode:
        parser.error('--mode is required')
    if args.mode == 'contracts':
        results = run_contracts(reviewed, args.output_dir)
        totals = {name: sum((row['statistics'] or {}).get(name, 0) for row in results)
            for name in ('tests', 'failures', 'errors', 'skipped', 'expected_failures', 'unexpected_successes')}
        report = dict(outcome='contract-commands-failed' if any(row['returncode'] for row in results)
            else 'contract-commands-completed-with-skips' if totals['skipped']
            else 'contract-commands-completed', commands=results, statistics=totals,
            exact_census='not-run; use capture with explicit original data and runtime snapshot',
            windows_behavior='not-proved')
        failed = any(row['returncode'] for row in results)
    else:
        if args.mode == 'capture':
            required = ('client', 'sdk_archive', 'original_api', 'python27', 'runtime_snapshot')
            missing = [name for name in required if not getattr(args, name)]
            if missing:
                parser.error('capture requires explicit ' + ', '.join('--' + name.replace('_', '-') for name in missing))
            args.native_report = args.output_dir / 'native/native-interface-audit.json'
            args.renderer_report = args.output_dir / 'renderer-abi-inventory.json'
            subprocess.run([sys.executable, '-B', str(ROOT / 'tools/audit_native_interfaces.py'),
                '--repo', str(ROOT), '--client', str(args.client), '--sdk-archive', str(args.sdk_archive),
                '--original-api', str(args.original_api), '--python27', args.python27,
                '--runtime-snapshot', str(args.runtime_snapshot), '--modules', 'all',
                '--output-dir', str(args.native_report.parent)], check=True)
            subprocess.run([sys.executable, '-B', str(ROOT / 'tools/audit_renderer_abi.py'),
                '--repo', str(ROOT), '--client', str(args.client), '--sdk-archive', str(args.sdk_archive),
                '--output', str(args.renderer_report)], check=True)
        elif not args.native_report or not args.renderer_report:
            parser.error('review requires --native-report and --renderer-report')
        report = review_reports(load(args.native_report), load(args.renderer_report), reviewed)
        report['input_reports'] = dict(native=str(args.native_report), renderer=str(args.renderer_report))
        if args.mode == 'capture':
            snapshot = load(args.runtime_snapshot)
            report['runtime_snapshot_metadata'] = {key: snapshot.get(key) for key in ('binary', 'stage', 'screen')}
            report['snapshot_freshness'] = 'caller-supplied; label is not a matching-build attestation'
        failed = bool(report['failures'])
    path = args.output_dir / 'baseline-review.json'
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')
    print(json.dumps(dict(outcome=report['outcome'], report=str(path),
        failure_count=(sum(bool(row['returncode']) for row in report['commands'])
            if args.mode == 'contracts' else len(report['failures']))), sort_keys=True))
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
