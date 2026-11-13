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
    'playback-state': ['Playback/PlaybackState.cpp', 'Media/MediaInformation.cpp', 'Tests/PlaybackStateTests.cpp'],
    'mpv-nodes': ['Mpv/MpvNodes.cpp', 'Filters/FilterChain.cpp', 'Filters/MPVFilter.cpp',
                  'Media/TrackPresentation.cpp', 'Tests/MpvNodesTests.cpp'],
    'player-session': [
        'Playback/PlaybackActions.cpp', 'Playback/PlaybackState.cpp', 'Playback/PlayerSession.cpp',
        'Playback/PlayerSession.AudioDevice.cpp', 'Playback/PlayerSession.ExternalVideo.cpp',
        'Playback/PlayerSession.Filters.cpp', 'Playback/PlayerSession.NetworkOpen.cpp',
        'Playback/PlayerSession.Playlist.cpp', 'Playback/PlayerSession.Preferences.cpp', 'Playback/PlayerSession.Scrub.cpp',
        'Playback/PlayerSession.SpeedHold.cpp', 'Playback/PlayerSession.SubtitleLoad.cpp',
        'Playback/PlayerSession.Subtitles.cpp', 'Playback/PlayerSession.TrackReload.cpp',
        'Playback/PlayerSession.Tracks.cpp', 'Playback/PlayerSession.WindowPolicy.cpp',
        'Media/MediaInformation.cpp', 'Media/TrackPresentation.cpp', 'Network/MediaUrl.cpp',
        'Playback/SpeedHoldPolicy.cpp', 'Playlist/PlaylistPlan.cpp', 'Screenshots/ScreenshotOptions.cpp',
        'Filters/FilterChain.cpp', 'Filters/MPVFilter.cpp', 'Input/BindingLibrary.cpp', 'Input/KeyMapping.cpp',
        'Settings/ControlBarToolbar.cpp',
        'Settings/PreferenceSchema.cpp',
        'Settings/WindowPlaybackPolicy.cpp', 'Subtitles/SubtitleStyle.cpp', 'Windows/MusicModePolicy.cpp',
        'Tests/PlayerSessionTests.cpp',
    ],
    'user-data': ['Input/BindingLibrary.cpp', 'Input/KeyMapping.cpp', 'Tests/UserDataTests.cpp'],
    'diagnostic-log': ['Diagnostics/DiagnosticLog.cpp', 'Tests/DiagnosticLogTests.cpp'],
    'local-input': ['Input/LocalInputPolicy.cpp', '../../tests/local_input_policy_tests.cpp'],
    'instance-protocol': ['../../tests/InstanceProtocolTests.cpp'],
}
compiler = os.environ.get('CXX', 'g++')
for name, inputs in suites.items():
    binary = out / (name + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([compiler, '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-Wno-missing-field-initializers', *[core + p for p in inputs],
                    '-o', str(binary)], cwd=root, check=True)
    subprocess.run([str(binary)], cwd=root, check=True)
    print(f'PASS: {name}', flush=True)
print('No MSVC, WinUI, DLL, device, or playback validation was performed.')
