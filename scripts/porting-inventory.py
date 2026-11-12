#!/usr/bin/env python3
"""Emit a conservative source-file coverage inventory; never infer completion.

Mappings identify code to review, not proof of equivalence. Every mapped file is
still partial until a symbol-by-symbol review establishes full static coverage.
"""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
MAPPING = {
    'JavascriptPluginInstance.swift': ['src/IIMA.App/Plugins/PluginRuntime.cpp', 'src/IIMA.App/MainWindow.PluginRuntime.cpp'],
    'JavascriptPolyfill.swift': ['src/IIMA.App/Plugins/PluginRuntime.cpp', 'src/IIMA.App/Plugins/PluginBootstrap.h', 'src/IIMA.Core/Plugins/PluginModulePath.cpp'],
    'JavascriptAPICore.swift': ['src/IIMA.App/Plugins/PluginBootstrap.h', 'src/IIMA.App/Plugins/PluginPlaybackProjection.cpp', 'src/IIMA.App/MainWindow.PluginRuntime.cpp'],
    'JavascriptAPIConsole.swift': ['src/IIMA.App/Plugins/PluginBootstrap.h', 'src/IIMA.App/Plugins/PluginRuntime.cpp'],
    'JavascriptAPIMenu.swift': ['src/IIMA.App/Plugins/PluginRuntime.cpp', 'src/IIMA.App/Plugins/PluginBootstrap.h', 'src/IIMA.App/MainWindow.PluginMenu.cpp'],
    'JavascriptPluginMenuItem.swift': ['src/IIMA.App/Plugins/PluginBootstrap.h', 'src/IIMA.App/MainWindow.PluginMenu.cpp'],
    'JavascriptAPIPreferences.swift': ['src/IIMA.App/Plugins/PluginRuntime.cpp', 'src/IIMA.App/Plugins/PluginBootstrap.h', 'src/IIMA.App/Platform/PluginPreferenceStore.cpp', 'src/IIMA.App/MainWindow.PluginRuntime.cpp'],
    'JavascriptAPIEvent.swift': ['src/IIMA.App/Plugins/PluginRuntime.cpp', 'src/IIMA.App/Plugins/PluginPlaybackProjection.cpp'],
    'JavascriptPlugin.swift': ['src/IIMA.Core/Plugins/PluginManifest.cpp', 'src/IIMA.App/Platform/PluginInspection.cpp'],
    'MiniPlayerWindowController.swift': ['src/IIMA.Core/Windows/MusicModePolicy.cpp', 'src/IIMA.App/MainWindow.Music.cpp'],
    'OpenSubClient.swift': ['src/IIMA.App/Platform/OpenSubClient.cpp'],
    'OpenSubSubtitle.swift': ['src/IIMA.Core/OnlineSubtitles/MediaHash.cpp', 'src/IIMA.App/Platform/SubtitleMediaHash.cpp', 'src/IIMA.App/MainWindow.OnlineSubtitles.cpp'],
    'ShooterSubtitle.swift': ['src/IIMA.Core/OnlineSubtitles/MediaHash.cpp', 'src/IIMA.App/Platform/SubtitleMediaHash.cpp'],
    'OnlineSubtitle.swift': ['src/IIMA.Core/OnlineSubtitles/SearchTransaction.cpp', 'src/IIMA.App/MainWindow.OnlineSubtitles.cpp'],
    'AssrtSubtitle.swift': ['src/IIMA.App/Platform/AssrtClient.cpp', 'src/IIMA.App/Platform/SubtitleStaging.cpp'],
    'SubChooseViewController.swift': ['src/IIMA.App/MainWindow.OnlineSubtitles.cpp'],
    'AppDelegate.swift': ['src/IIMA.App/App.xaml.cpp', 'src/IIMA.App/Platform/InstanceRouter.cpp'],
    'PlayerCore.swift': ['src/IIMA.Core/Playback/PlayerSession.cpp', 'src/IIMA.Core/Windows/PlayerWindowRegistry.cpp', 'src/IIMA.App/App.xaml.cpp'],
    'PlaybackInfo.swift': ['src/IIMA.Core/Playback/PlaybackState.h'],
    'MPVCommand.swift': ['src/IIMA.Core/Mpv/Catalog/MPVCommand.h'],
    'MPVOption.swift': ['src/IIMA.Core/Mpv/Catalog/MPVOption.h'],
    'MPVProperty.swift': ['src/IIMA.Core/Mpv/Catalog/MPVProperty.h'],
    'ThumbnailCache.swift': ['src/IIMA.Core/Thumbnails/ThumbnailCache.cpp'],
    'CacheManager.swift': ['src/IIMA.Core/Thumbnails/ThumbnailCacheManager.cpp'],
    'MPVController.swift': ['src/IIMA.Core/Mpv/MpvClient.cpp'],
    'MPVNode.swift': ['src/IIMA.Core/Mpv/MpvNodes.cpp'],
    'MainWindowController.swift': ['src/IIMA.App/MainWindow.xaml.cpp', 'src/IIMA.Core/Geometry/WindowGeometry.cpp', 'src/IIMA.App/Platform/WindowPlacement.cpp', 'src/IIMA.App/MainWindow.Placement.cpp'],
    'PlayerWindowController.swift': ['src/IIMA.App/MainWindow.Commands.cpp', 'src/IIMA.App/MainWindow.Keyboard.cpp'],
    'VideoView.swift': ['src/IIMA.App/Presentation/VideoPresenter.cpp'],
    'MPVFilter.swift': ['src/IIMA.Core/Filters/MPVFilter.cpp'],
    'FilterPresets.swift': ['src/IIMA.Core/Filters/FilterPresets.cpp'],
    'SavedFilter.swift': ['src/IIMA.Core/Filters/SavedFilter.cpp'],
    'KeyMapping.swift': ['src/IIMA.Core/Input/KeyMapping.cpp'],
    'KeyCodeHelper.swift': ['src/IIMA.Core/Input/KeyMapping.cpp', 'src/IIMA.App/Platform/KeyboardInput.cpp'],
    'SettingsPageKeyBindings.swift': ['src/IIMA.Core/Input/BindingConfiguration.cpp', 'src/IIMA.App/MainWindow.Keyboard.cpp'],
    'KeyBindingTranslator.swift': ['src/IIMA.Core/Commands/KeyBindingTranslator.cpp'],
    'IINACommand.swift': ['src/IIMA.Core/Commands/ApplicationCommand.cpp'],
    'MPVCommandWrappers.swift': ['src/IIMA.Core/Commands/PlaylistCommands.cpp'],
    'MainMenuActions.swift': ['src/IIMA.App/MainWindow.Playlist.cpp', 'src/IIMA.App/Platform/ShellFileActions.cpp', 'src/IIMA.App/MainWindow.OnlineSubtitleSave.cpp'],
    'SidebarPlaylistPane.swift': ['src/IIMA.Core/Playlist/PlaylistPlan.cpp'],
    'VideoTime.swift': ['src/IIMA.Core/Media/VideoTime.cpp'],
    'MPVChapter.swift': ['src/IIMA.Core/Media/MediaChapter.cpp'],
    'SidebarChaptersPane.swift': ['src/IIMA.Core/Media/MediaChapter.cpp'],
    'MPVTrack.swift': ['src/IIMA.Core/Media/TrackPresentation.cpp'],
    'ISO639Helper.swift': ['src/IIMA.Core/Media/TrackPresentation.cpp'],
    'Aspect.swift': ['src/IIMA.Core/Geometry/Aspect.cpp'],
    'GeometryDef.swift': ['src/IIMA.Core/Geometry/WindowGeometry.cpp'],
    'Equalizations.swift': ['src/IIMA.Core/Audio/Equalizations.cpp'],
    'SidebarAudioPane.swift': ['src/IIMA.Core/Audio/Equalizations.cpp', 'src/IIMA.App/MainWindow.Audio.cpp'],
    'SidebarVideoPane.swift': ['src/IIMA.App/MainWindow.Video.cpp'],
    'SidebarSubtitlesPane.swift': ['src/IIMA.App/MainWindow.Subtitles.cpp'],
    'AutoFileMatcher.swift': ['src/IIMA.Core/Matching/SubtitleMatcher.cpp'],
    'FileGroup.swift': ['src/IIMA.Core/Matching/FileGroup.cpp'],
    'OpenURLWindowController.swift': ['src/IIMA.Core/Network/MediaUrl.cpp', 'src/IIMA.App/MainWindow.Network.cpp'],
    'OSDMessage.swift': ['src/IIMA.Core/OSD/OSDMessage.cpp'],
    'OSDView.swift': ['src/IIMA.Core/OSD/OSDMessage.cpp', 'src/IIMA.App/MainWindow.OSD.cpp'],
    'HistoryController.swift': ['src/IIMA.Core/Persistence/UserData.h', 'src/IIMA.Core/History/HistoryBrowser.cpp', 'src/IIMA.App/Platform/UserDataMerge.cpp'],
    'HistoryWindowController.swift': ['src/IIMA.Core/History/HistoryBrowser.cpp', 'src/IIMA.App/MainWindow.History.cpp'],
    'PlaybackHistory.swift': ['src/IIMA.Core/History/HistoryBrowser.cpp'],
    'InspectorWindowController.swift': ['src/IIMA.Core/Media/MediaInformation.cpp', 'src/IIMA.App/MainWindow.Information.cpp'],
    'Preference.swift': ['src/IIMA.Core/Persistence/UserData.h', 'src/IIMA.Core/Subtitles/SubtitleStyle.h', 'src/IIMA.Core/Settings/PreferenceSchema.cpp', 'src/IIMA.App/MainWindow.Preferences.cpp'],
    'NowPlayingInfoManager.swift': ['src/IIMA.App/Platform/MediaIntegration.cpp'],
    'SleepPreventer.swift': ['src/IIMA.App/Platform/MediaIntegration.cpp'],
}
records = []
for path in sorted((ROOT / 'references/iina').rglob('*.swift')):
    targets = MAPPING.get(path.name, [])
    records.append({'source': path.relative_to(ROOT).as_posix(),
                    'physicalLines': len(path.read_text().splitlines()),
                    'coverage': 'partial-static-unverified' if targets else 'not-mapped',
                    'reviewTargets': targets})
print(json.dumps({'sourceRevision': '685a40ee1d4165107182f7236751ad9d5873f267',
                  'note': 'not-mapped does not prove absent behavior; mapped does not prove full coverage. No file is asserted fully ported by this inventory.',
                  'files': records}, ensure_ascii=False, indent=2))
