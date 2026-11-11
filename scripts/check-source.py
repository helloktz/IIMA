#!/usr/bin/env python3
"""Host-independent structural checks. Not a substitute for MSBuild/XAML compilation."""
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
NS = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
errors = []
checked = 0
for path in sorted(ROOT.glob('src/**/*.vcxproj')):
    tree = ET.parse(path)
    checked += 1
    included = set()
    for item in tree.getroot().iter():
        kind = item.tag.rsplit('}', 1)[-1]
        name = item.get('Include')
        if kind not in {'ClCompile', 'ClInclude', 'Midl', 'Page', 'ApplicationDefinition', 'ProjectReference'} or not name:
            continue
        if '$(' in name or '*' in name:
            continue
        target = (path.parent / name.replace('\\', '/')).resolve()
        if not target.is_file():
            errors.append(f'{path.relative_to(ROOT)}: missing {kind} input {name}')
        included.add(target)
        if ROOT / 'references' in target.parents and kind == 'ClCompile':
            errors.append(f'Reference implementation must not be compiled: {name}')
    for source in path.parent.rglob('*.cpp'):
        if 'Tests' not in source.relative_to(path.parent).parts and source.resolve() not in included:
            errors.append(f'Source not included in project: {source.relative_to(ROOT)}')
for pattern in ('src/**/*.xaml', 'build/*.props', 'build/*.targets', 'Directory.Build.props'):
    for path in ROOT.glob(pattern):
        ET.parse(path)
        checked += 1
# Verify the source references remain byte-for-byte the repository baseline.
changed = subprocess.check_output(
    ['git', 'diff', '--name-only', 'f5f649b', '--', 'references'], cwd=ROOT, text=True)
if changed.strip():
    errors.append('Reference snapshots changed: ' + changed.strip())
# The application dynamically loads symbols; it must not export mpv declarations.
upstream = (ROOT / 'references/mpv/include/mpv/client.h').read_text()
consumer = ROOT / 'src/IIMA.Core/Mpv/include/mpv/client.h'
if consumer.exists():
    expected = upstream.replace('#define MPV_EXPORT __declspec(dllexport)', '#define MPV_EXPORT /* IIMA dynamically loads exports; this header consumes the DLL. */')
    if consumer.read_text() != expected:
        errors.append('Consumer mpv header differs beyond the documented MPV_EXPORT adaptation')
if errors:
    print('\n'.join(errors), file=sys.stderr)
    sys.exit(1)
print(f'PASS: {checked} XML files parsed; project source inputs exist; reference snapshots unchanged.')
print('NOT RUN: MSVC, MSBuild, XAML generation, Windows runtime or GPU validation.')
