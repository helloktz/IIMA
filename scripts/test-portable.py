#!/usr/bin/env python3
"""Run host-independent C++ logic tests; never builds the Windows product."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[1]
out = root / 'artifacts/tests'
out.mkdir(parents=True, exist_ok=True)
core = 'src/IIMA.Core/'
suites = {
    'playback-state': ['Playback/PlaybackState.cpp', 'Tests/PlaybackStateTests.cpp'],
    'mpv-nodes': ['Mpv/MpvNodes.cpp', 'Tests/MpvNodesTests.cpp'],
    'player-session': ['Playback/PlaybackState.cpp', 'Playback/PlayerSession.cpp', 'Tests/PlayerSessionTests.cpp'],
    'user-data': ['Tests/UserDataTests.cpp'],
    'diagnostic-log': ['Diagnostics/DiagnosticLog.cpp', 'Tests/DiagnosticLogTests.cpp'],
    'local-input': ['Input/LocalInputPolicy.cpp', '../../tests/local_input_policy_tests.cpp'],
    'instance-protocol': ['../../tests/InstanceProtocolTests.cpp'],
}
compiler = os.environ.get('CXX', 'g++')
for name, inputs in suites.items():
    binary = out / name
    subprocess.run([compiler, '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', *[core + p for p in inputs],
                    '-o', str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
    print(f'PASS: {name}', flush=True)
print('No MSVC, WinUI, DLL, device, or playback validation was performed.')
