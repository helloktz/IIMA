iima_package_directory(cppwinrt Microsoft.Windows.CppWinRT)
iima_package_directory(winui Microsoft.WindowsAppSDK.WinUI)
iima_package_directory(foundation Microsoft.WindowsAppSDK.Foundation)
iima_package_directory(interactive Microsoft.WindowsAppSDK.InteractiveExperiences)
iima_package_directory(webview2 Microsoft.Web.WebView2)

set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(build_scripts "${PROJECT_SOURCE_DIR}/build")
set(sdk_bin "${IIMA_WINDOWS_SDK}/bin/${IIMA_WINDOWS_SDK_VERSION}/x64")
set(xaml App.xaml MainWindow.xaml)
set(idl MainWindow.idl VideoSurface.idl WheelSlider.idl XamlMetaDataProvider.idl)
set(strings "${CMAKE_CURRENT_SOURCE_DIR}/Strings")
set(string_resources
    "${strings}/en-US/Resources.resw" "${strings}/en-US/Messages.resw"
    "${strings}/zh-CN/Resources.resw" "${strings}/zh-CN/Messages.resw"
)
configure_file(app.manifest.in "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" @ONLY)
set(runtime_manifests
    "${interactive}/runtimes-framework/package.appxfragment"
    "${foundation}/runtimes-framework/package.appxfragment"
    "${winui}/runtimes-framework/package.appxfragment"
)
set(runtime_resources
    "${interactive}/runtimes-framework/win-x64/native/Microsoft.UI.pri"
    "${foundation}/runtimes-framework/win-x64/native/Microsoft.WindowsAppRuntime.pri"
    "${winui}/runtimes-framework/win-x64/native/Microsoft.UI.Xaml.Controls.pri"
)

file(GLOB sdk_metadata CONFIGURE_DEPENDS
    "${winui}/metadata/*.winmd"
    "${foundation}/metadata/*.winmd"
    "${interactive}/metadata/10.0.18362.0/*.winmd"
)
list(APPEND sdk_metadata
    "${webview2}/lib/Microsoft.Web.WebView2.Core.winmd"
)
file(GLOB_RECURSE platform_metadata CONFIGURE_DEPENDS
    "${IIMA_WINDOWS_SDK}/References/${IIMA_WINDOWS_SDK_VERSION}/*.winmd"
)
list(APPEND platform_metadata
    "${IIMA_WINDOWS_SDK}/UnionMetadata/${IIMA_WINDOWS_SDK_VERSION}/Facade/Windows.winmd"
)

set(xaml_sources
    "${generated}/module.g.cpp"
    "${generated}/XamlTypeInfo.g.cpp"
    "${generated}/XamlTypeInfo.Impl.g.cpp"
)
add_custom_command(
    OUTPUT "${generated}/winui.stamp"
    BYPRODUCTS ${xaml_sources}
        "${generated}/App.xbf" "${generated}/MainWindow.xbf" "${generated}/IIMA.winmd"
        "${generated}/App.xaml.g.h" "${generated}/App.xaml.g.hpp"
        "${generated}/MainWindow.xaml.g.h" "${generated}/MainWindow.xaml.g.hpp"
        "${generated}/MainWindow.g.h" "${generated}/MainWindow.g.cpp"
        "${generated}/VideoSurface.g.h" "${generated}/VideoSurface.g.cpp"
        "${generated}/WheelSlider.g.h" "${generated}/WheelSlider.g.cpp"
        "${generated}/XamlMetaDataProvider.h" "${generated}/XamlMetaDataProvider.g.h"
        "${generated}/XamlMetaDataProvider.g.cpp"
        "${generated}/XamlTypeInfo.xaml.g.h"
        "${generated}/XamlBindingInfo.xaml.g.h" "${generated}/XamlBindingInfo.xaml.g.hpp"
        "${generated}/winrt/base.h" "${generated}/winrt/IIMA.h"
    COMMAND "${IIMA_PWSH}" -NoProfile -File "${build_scripts}/GenerateWinUI.ps1"
        -Source "${CMAKE_CURRENT_SOURCE_DIR}" -Output "${generated}"
        -WindowsSdk "${IIMA_WINDOWS_SDK}" -SdkVersion "${IIMA_WINDOWS_SDK_VERSION}"
        -VCTools "${IIMA_VC_TOOLS}" -CppWinRT "${cppwinrt}/bin/cppwinrt.exe"
        -WinUI "${winui}" -Foundation "${foundation}" -Interactive "${interactive}" -WebView2 "${webview2}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${generated}/winui.stamp"
    DEPENDS ${xaml} ${idl} ${sdk_metadata} ${platform_metadata}
        App.xaml.h MainWindow.xaml.h pch.h
        "${build_scripts}/GenerateWinUI.ps1"
        "${IIMA_WINDOWS_SDK}/Platforms/UAP/${IIMA_WINDOWS_SDK_VERSION}/Platform.xml"
        "${sdk_bin}/midl.exe" "${sdk_bin}/mdmerge.exe"
        "${cppwinrt}/bin/cppwinrt.exe" "${winui}/tools/net472/XamlCompiler.exe"
    VERBATIM
    COMMENT "Generating WinUI XAML, WinMD and C++/WinRT projections"
)
add_custom_command(
    OUTPUT "${generated}/IIMA.manifest" "${generated}/IIMA.pri"
    COMMAND "${IIMA_PWSH}" -NoProfile -File "${build_scripts}/PrepareWinUIRuntime.ps1"
        -ApplicationManifest "${CMAKE_CURRENT_BINARY_DIR}/app.manifest"
        -Generated "${generated}" -Strings "${strings}" -MakePri "${sdk_bin}/makepri.exe"
        -Interactive "${interactive}" -Foundation "${foundation}" -WinUI "${winui}"
    DEPENDS "${generated}/winui.stamp"
        "${CMAKE_CURRENT_BINARY_DIR}/app.manifest" "${build_scripts}/PrepareWinUIRuntime.ps1"
        "${sdk_bin}/makepri.exe" ${runtime_resources} ${runtime_manifests} ${string_resources}
    VERBATIM
    COMMENT "Generating the app-local WinUI manifest and resources"
)
add_custom_target(iima_winui_codegen DEPENDS
    "${generated}/winui.stamp" "${generated}/IIMA.pri" "${generated}/IIMA.manifest"
)

set(initializers
    "${foundation}/include/UndockedRegFreeWinRT-AutoInitializer.cpp"
    "${foundation}/include/WindowsAppRuntimeAutoInitializer.cpp"
)
set_source_files_properties(${initializers} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

add_executable(IIMA WIN32
    App.OpenPolicy.cpp
    App.xaml.cpp
    Main.cpp
    MainWindow.ABLoop.cpp
    MainWindow.ABLoopMarkers.cpp
    MainWindow.Accessibility.cpp
    MainWindow.Actions.cpp
    MainWindow.Audio.cpp
    MainWindow.AutomaticFiles.cpp
    MainWindow.AutomaticVideoSize.cpp
    MainWindow.BindingEditor.cpp
    MainWindow.BindingLibrary.cpp
    MainWindow.Buffering.cpp
    MainWindow.ChapterMarkers.cpp
    MainWindow.Commands.cpp
    MainWindow.CompactOverlay.cpp
    MainWindow.CompactPlacement.cpp
    MainWindow.ControlBar.cpp
    MainWindow.ControlBarLayout.cpp
    MainWindow.ControlBarToolbar.cpp
    MainWindow.Crop.cpp
    MainWindow.CropTransaction.cpp
    MainWindow.Diagnostics.cpp
    MainWindow.Drop.cpp
    MainWindow.Equalizer.cpp
    MainWindow.ExternalAudio.cpp
    MainWindow.ExternalAV.cpp
    MainWindow.ExternalTrackReload.cpp
    MainWindow.ExternalTrackRemoval.cpp
    MainWindow.ExternalVideo.cpp
    MainWindow.Files.cpp
    MainWindow.FilterPresets.cpp
    MainWindow.Filters.cpp
    MainWindow.Folders.cpp
    MainWindow.Fullscreen.cpp
    MainWindow.FullscreenInformation.cpp
    MainWindow.History.cpp
    MainWindow.HistoryAvailability.cpp
    MainWindow.HistoryDeletion.cpp
    MainWindow.Information.cpp
    MainWindow.InitialWindow.cpp
    MainWindow.InspectorWatch.cpp
    MainWindow.InteractiveCrop.cpp
    MainWindow.Keyboard.cpp
    MainWindow.KeyRecorder.cpp
    MainWindow.Menus.cpp
    MainWindow.Menus.Dynamic.cpp
    MainWindow.Menus.Media.cpp
    MainWindow.MouseClicks.cpp
    MainWindow.MouseWheel.cpp
    MainWindow.MpvDiagnostics.cpp
    MainWindow.Music.cpp
    MainWindow.MusicVolume.cpp
    MainWindow.Navigation.cpp
    MainWindow.Network.cpp
    MainWindow.NetworkStatus.cpp
    MainWindow.OnlineSubtitles.cpp
    MainWindow.OnlineSubtitleSave.cpp
    MainWindow.OpenPolicy.cpp
    MainWindow.OpenSubAccount.cpp
    MainWindow.OscArrows.cpp
    MainWindow.OSD.cpp
    MainWindow.Placement.cpp
    MainWindow.Playback.cpp
    MainWindow.Playlist.cpp
    MainWindow.PlaylistBrowser.cpp
    MainWindow.PlaylistClipboard.cpp
    MainWindow.PlaylistDeletion.cpp
    MainWindow.PlaylistDrag.cpp
    MainWindow.PlaylistDurations.cpp
    MainWindow.PlaylistMetadata.cpp
    MainWindow.PlaylistProgress.cpp
    MainWindow.PlaylistSelection.cpp
    MainWindow.PlaylistSubtitleApply.cpp
    MainWindow.PlaylistSubtitles.cpp
    MainWindow.Preferences.cpp
    MainWindow.QuickLut.cpp
    MainWindow.RevealSelection.cpp
    MainWindow.SavedFilters.cpp
    MainWindow.ScreenshotPreview.cpp
    MainWindow.Screenshots.cpp
    MainWindow.Scrubbing.cpp
    MainWindow.SecondarySubtitles.cpp
    MainWindow.Settings.cpp
    MainWindow.SettingsSearch.cpp
    MainWindow.SliderWheel.cpp
    MainWindow.SubtitleColorPicker.cpp
    MainWindow.SubtitleEncoding.cpp
    MainWindow.SubtitleFontPicker.cpp
    MainWindow.SubtitleLoad.cpp
    MainWindow.SubtitleMatching.cpp
    MainWindow.Subtitles.cpp
    MainWindow.SubtitleStyle.cpp
    MainWindow.System.cpp
    MainWindow.ThumbnailCache.cpp
    MainWindow.Thumbnails.cpp
    MainWindow.TimeDisplay.cpp
    MainWindow.TrackInspector.cpp
    MainWindow.UserData.cpp
    MainWindow.Video.cpp
    MainWindow.VideoAdjustments.cpp
    MainWindow.VideoCursor.cpp
    MainWindow.VideoSize.cpp
    MainWindow.WindowAspect.cpp
    MainWindow.WindowPolicy.cpp
    MainWindow.Windows.cpp
    MainWindow.WindowScale.cpp
    MainWindow.xaml.cpp
    Platform/AssrtClient.cpp
    Platform/AtomicTextFile.cpp
    Platform/AutomaticFileMatcher.cpp
    Platform/FileAssociations.cpp
    Platform/FontCatalog.cpp
    Platform/FullscreenInformation.cpp
    Platform/HistoryAvailability.cpp
    Platform/HistoryLocale.cpp
    Platform/InstanceRouter.cpp
    Platform/KeyboardInput.cpp
    Platform/LocalInputResolver.cpp
    Platform/Localization.cpp
    Platform/MediaIntegration.cpp
    Platform/OpenSubClient.cpp
    Platform/PlaylistFileDeletion.cpp
    Platform/PowerLifecycle.cpp
    Platform/ScreenshotImage.cpp
    Platform/ScreenshotStaging.cpp
    Platform/ShellFileActions.cpp
    Platform/ShooterClient.cpp
    Platform/SubtitleMediaHash.cpp
    Platform/SubtitleStaging.cpp
    Platform/ThumbnailDiskCache.cpp
    Platform/ThumbnailExtractor.cpp
    Platform/UserDataMerge.cpp
    Platform/UserDataStore.cpp
    Platform/WindowLifecycle.cpp
    Platform/WindowPlacement.cpp
    Presentation/VideoPresenter.cpp
    Settings/MediaPresetsJson.cpp
    Settings/PreferenceJson.cpp
    VideoSurface.cpp
    WheelSlider.cpp
    XamlMetaDataProvider.cpp
    ${xaml_sources}
    ${initializers}
    "${generated}/IIMA.manifest"
)
add_dependencies(IIMA iima_winui_codegen)
target_precompile_headers(IIMA PRIVATE pch.h)
target_include_directories(IIMA PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${generated}"
    "${interactive}/include" "${winui}/include" "${foundation}/include"
    "${webview2}/build/native/include" "${webview2}/build/native/include-winrt"
)
target_compile_definitions(IIMA PRIVATE
    DISABLE_XAML_GENERATED_MAIN __WRL_NO_DEFAULT_LIB__
    MICROSOFT_WINDOWSAPPSDK_SELFCONTAINED=1
    MICROSOFT_WINDOWSAPPSDK_AUTOINITIALIZE_UNDOCKEDREGFREEWINRT
    MICROSOFT_WINDOWSAPPSDK_UNDOCKEDREGFREEWINRT_AUTO_INITIALIZE_LOADLIBRARY
    MICROSOFT_WINDOWSAPPSDK_UNDOCKEDREGFREEWINRT_AUTO_INITIALIZE
)
target_link_libraries(IIMA PRIVATE iima_compile_settings IIMA.Core
    WindowsApp.lib
    "${interactive}/lib/native/x64/Microsoft.UI.Dispatching.lib"
    "${foundation}/lib/native/x64/Microsoft.WindowsAppRuntime.Bootstrap.lib"
    "${foundation}/lib/native/x64/Microsoft.WindowsAppRuntime.lib"
    "${foundation}/lib/native/x64/mrm.lib"
    "${webview2}/build/native/x64/WebView2Loader.dll.lib"
)
target_link_options(IIMA PRIVATE /MANIFESTUAC:NO)
set_property(TARGET IIMA APPEND PROPERTY LINK_DEPENDS "${generated}/IIMA.manifest")

set(runtime_dir "${CMAKE_BINARY_DIR}/bin")
set(dependencies_stamp "${CMAKE_CURRENT_BINARY_DIR}/runtime-dependencies.stamp")
set(application_stamp "${CMAKE_CURRENT_BINARY_DIR}/runtime-application.stamp")
set(framework_dirs
    "${interactive}/runtimes-framework/win-x64/native"
    "${foundation}/runtimes-framework/win-x64/native"
    "${winui}/runtimes-framework/win-x64/native"
)
foreach(framework_dir IN LISTS framework_dirs)
    file(GLOB_RECURSE framework_files CONFIGURE_DEPENDS RELATIVE "${framework_dir}" "${framework_dir}/*")
    foreach(file IN LISTS framework_files)
        if(file STREQUAL "DeploymentAgent.exe")
            continue()
        endif()
        string(TOLOWER "${file}" normalized_file)
        set(runtime_file "${file}")
        if(normalized_file MATCHES "^en-us/")
            string(REGEX REPLACE "^[^/]+/" "en-US/" runtime_file "${file}")
        elseif(normalized_file MATCHES "^zh-cn/")
            string(REGEX REPLACE "^[^/]+/" "zh-CN/" runtime_file "${file}")
        elseif(normalized_file MATCHES "^[a-z][a-z][a-z]?-[a-z0-9-]+/")
            continue()
        endif()
        list(APPEND framework_copy_entries "${framework_dir}/${file}|${runtime_file}")
        list(APPEND runtime_inputs "${framework_dir}/${file}")
        list(APPEND runtime_outputs "${runtime_dir}/${runtime_file}")
    endforeach()
endforeach()
set(framework_inventory "${CMAKE_CURRENT_BINARY_DIR}/winui-runtime.txt")
file(GENERATE OUTPUT "${framework_inventory}" CONTENT "$<JOIN:${framework_copy_entries},\n>\n")
set(mpv_sdk "${IIMA_WORK_DIR}/libmpv")
file(GLOB mpv_runtime CONFIGURE_DEPENDS "${mpv_sdk}/bin/*.dll")
file(GLOB mpv_metadata CONFIGURE_DEPENDS "${mpv_sdk}/manifest.json" "${mpv_sdk}/verified-binaries.json")
if(mpv_runtime OR mpv_metadata)
    set(mpv_patch "${build_scripts}/patches/mpv-iima-owned-swapchain-v1.patch")
    set(mpv_checks COMMAND "${IIMA_PWSH}" -NoProfile -File "${PROJECT_SOURCE_DIR}/scripts/verify-libmpv.ps1"
        -SdkDirectory "${mpv_sdk}")
    list(APPEND runtime_inputs ${mpv_metadata} "${mpv_patch}"
        "${PROJECT_SOURCE_DIR}/scripts/verify-libmpv.ps1"
        "${PROJECT_SOURCE_DIR}/scripts/test-libmpv.ps1"
        "${PROJECT_SOURCE_DIR}/scripts/Common.ps1")
    # A restored SDK can have older timestamps than the last deployment.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${mpv_metadata} "${mpv_patch}")
    foreach(input IN ITEMS ${mpv_metadata} "${mpv_patch}")
        file(SHA256 "${input}" input_hash)
        string(APPEND mpv_identity "${input_hash}\n")
    endforeach()
endif()
set(application_files
    "${generated}/App.xbf" "${generated}/MainWindow.xbf"
    "${generated}/IIMA.pri" "${generated}/IIMA.winmd"
)
set(dependency_files
    "${foundation}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
    "${webview2}/runtimes/win-x64/native_uap/Microsoft.Web.WebView2.Core.dll"
    ${sdk_metadata} ${mpv_runtime}
)
foreach(file IN LISTS dependency_files)
    cmake_path(GET file FILENAME filename)
    list(APPEND runtime_outputs "${runtime_dir}/${filename}")
endforeach()
list(REMOVE_DUPLICATES runtime_outputs)

# Track inventory removals as well as changes to the files that still exist.
set(runtime_inventory "${CMAKE_CURRENT_BINARY_DIR}/runtime-dependencies.txt")
file(GENERATE OUTPUT "${runtime_inventory}"
    CONTENT "${mpv_identity}$<JOIN:${runtime_inputs};${dependency_files},\n>\n")
add_custom_command(
    OUTPUT "${dependencies_stamp}"
    BYPRODUCTS ${runtime_outputs}
    ${mpv_checks}
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "${runtime_dir}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${runtime_dir}"
    COMMAND "${CMAKE_COMMAND}" "-Dinventory=${framework_inventory}" "-Ddestination=${runtime_dir}"
        -P "${build_scripts}/CopyWinUIRuntime.cmake"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${dependency_files} "${runtime_dir}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${dependencies_stamp}"
    DEPENDS "${runtime_inventory}" "${framework_inventory}" "${build_scripts}/CopyWinUIRuntime.cmake"
        ${runtime_inputs} ${dependency_files}
    VERBATIM
    COMMENT "Staging the app-local WinUI and media runtimes"
)
add_custom_command(
    OUTPUT "${application_stamp}"
    BYPRODUCTS "${runtime_dir}/IIMA.exe" "${runtime_dir}/App.xbf" "${runtime_dir}/MainWindow.xbf"
        "${runtime_dir}/IIMA.pri" "${runtime_dir}/IIMA.winmd"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:IIMA>" ${application_files} "${runtime_dir}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${application_stamp}"
    DEPENDS IIMA "${dependencies_stamp}" ${application_files}
    VERBATIM
    COMMENT "Staging IIMA executable and resources"
)
add_custom_target(IIMA_runtime ALL DEPENDS "${application_stamp}")
