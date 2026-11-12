#!/usr/bin/env python3
"""Measure the requested static-port size without counting vendored/test filler.

No compiler, SDK, test runner or network required. JSON is suitable for retaining
milestone evidence. Counts are not a proof of behavior or a completion score.
"""
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BASELINE = 'd64f404'


def describe(paths):
    paths = sorted(paths)
    records = []
    for path in paths:
        lines = path.read_text(encoding='utf-8-sig').splitlines()
        records.append({'path': path.relative_to(ROOT).as_posix(),
                        'physicalLines': len(lines),
                        'nonblankLines': sum(bool(line.strip()) for line in lines)})
    return {'files': len(records),
            'physicalLines': sum(row['physicalLines'] for row in records),
            'nonblankLines': sum(row['nonblankLines'] for row in records),
            'sources': records}


def product_file(path):
    parts = path.relative_to(ROOT / 'src').parts
    return (path.suffix in {'.cpp', '.h', '.hpp'}
            and not any(part.lower() in {'tests', 'test', 'generated', 'third_party', 'vendor'} for part in parts)
            and 'Mpv/include/mpv/' not in path.as_posix())


def census():
    baseline = subprocess.run(['git', 'ls-tree', '-r', '--name-only', BASELINE, '--', 'src'],
                              cwd=ROOT, text=True, capture_output=True, check=True)
    old_paths = set(baseline.stdout.splitlines())
    cpp = [p for p in (ROOT / 'src').rglob('*') if p.is_file() and product_file(p)]
    swift = describe((ROOT / 'references/iina').rglob('*.swift'))
    product = describe(cpp)
    new = describe(p for p in cpp if p.relative_to(ROOT).as_posix() not in old_paths)
    return {'baselineCommit': BASELINE,
            'scope': 'All reference Swift versus product C++ newly added since baseline; excludes tests, vendored mpv header and generated code.',
            'lineDefinition': 'Physical UTF-8 lines, including comments and blanks; nonblank lines reported separately. Neither is semantic code volume.',
            'swift': swift, 'productCpp': product, 'newProductCpp': new,
            'requestedThresholdMet': new['files'] >= swift['files'] and new['physicalLines'] >= swift['physicalLines'],
            'remainingFiles': max(0, swift['files'] - new['files']),
            'remainingPhysicalLines': max(0, swift['physicalLines'] - new['physicalLines'])}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', action='store_true', help='Include per-file evidence')
    args = parser.parse_args()
    result = census()
    if not args.json:
        for key in ('swift', 'productCpp', 'newProductCpp'):
            result[key].pop('sources')
    print(json.dumps(result, ensure_ascii=False, indent=2))
