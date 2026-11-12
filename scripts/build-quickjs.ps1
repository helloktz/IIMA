#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$CMakePath = 'cmake'
)
. (Join-Path $PSScriptRoot 'Common.ps1')
$toolchain = Get-IimaToolchain
# The engine is an upstream dependency, not product code. Never use a moving tag
# or a downloaded executable as a substitute for this source-built static target.
$repository = 'https://github.com/quickjs-ng/quickjs.git'
$revision = '58bdcf0ce35594df30bf5cfbba3be8b454799cc0'
$version = '0.12.1'
$cmakeVersion = '4.2.0'
$cmake = (Get-Command $CMakePath -CommandType Application -ErrorAction Stop).Source
$git = (Get-Command git -CommandType Application -ErrorAction Stop).Source
$versionOutput = & $cmake --version
if ($LASTEXITCODE -ne 0 -or $versionOutput[0] -ne "cmake version $cmakeVersion") {
    throw "CMake $cmakeVersion is required. Install it from cmake.org and pass -CMakePath if necessary."
}
$source = Join-Path $RepoRoot ".deps/sources/quickjs-ng-$version"
if (-not (Test-Path -LiteralPath $source)) {
    New-Item -ItemType Directory -Force $source | Out-Null
    & $git -C $source init
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize QuickJS source checkout.' }
    & $git -C $source remote add origin $repository
    if ($LASTEXITCODE -ne 0) { throw 'Cannot record QuickJS upstream.' }
    & $git -C $source fetch --depth 1 origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'Cannot fetch the pinned QuickJS commit. Remove only this incomplete checkout after inspecting it, then retry.' }
    & $git -C $source checkout --detach FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check out the pinned QuickJS commit.' }
}
$actualRevision = & $git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualRevision -ne $revision) { throw 'QuickJS checkout has the wrong revision; it will not be overwritten.' }
$actualOrigin = & $git -C $source remote get-url origin
if ($LASTEXITCODE -ne 0 -or $actualOrigin -ne $repository) { throw 'QuickJS checkout does not use the declared official origin.' }
$dirty = & $git -C $source status --porcelain --untracked-files=all
if ($LASTEXITCODE -ne 0 -or $dirty) { throw 'QuickJS checkout contains changes or untracked files; preserve/review them before rebuilding.' }
$build = Join-Path $RepoRoot "artifacts/quickjs/$revision/$Configuration"
$stage = Join-Path $RepoRoot '.deps/quickjs'
$compiler = Join-Path $toolchain.Root "VC/Tools/MSVC/$($toolchain.ToolVersion)/bin/Hostx64/x64/cl.exe"
$nmake = Join-Path $toolchain.Root "VC/Tools/MSVC/$($toolchain.ToolVersion)/bin/Hostx64/x64/nmake.exe"
$devcmd = Join-Path $toolchain.Root 'Common7/Tools/VsDevCmd.bat'
$runtime = if ($Configuration -eq 'Debug') { 'MultiThreadedDebugDLL' } else { 'MultiThreadedDLL' }
$sdk = [string]$BuildProperties.Project.PropertyGroup.WindowsTargetPlatformVersion
foreach ($path in @($source, $build, $stage, $compiler, $nmake, $devcmd, $cmake)) {
    # The generated command file has quoted paths; reject cmd expansion tokens
    # rather than silently invoking a different location for exotic folder names.
    if ($path.IndexOfAny([char[]]"`"%`r`n!") -ge 0) { throw 'QuickJS build paths cannot contain quotes, percent signs, exclamation marks or newlines.' }
}
foreach ($required in @($compiler, $nmake, $devcmd)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing pinned build tool: $required" }
}
New-Item -ItemType Directory -Force $build | Out-Null
$commandFile = Join-Path $build 'build-pinned.cmd'
# NMake lets the upstream CMake target use the exact cl.exe already pinned by
# IIMA, instead of CMake silently selecting another installed Visual Studio.
$commandText = @"
@echo off
chcp 65001 >nul
call "$devcmd" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver=$($toolchain.ToolVersion) -winsdk=$sdk
if errorlevel 1 exit /b 1
"$cmake" -S "$source" -B "$build" -G "NMake Makefiles" "-DCMAKE_C_COMPILER=$compiler" "-DCMAKE_MAKE_PROGRAM=$nmake" "-DCMAKE_BUILD_TYPE=$Configuration" "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW" "-DCMAKE_MSVC_RUNTIME_LIBRARY=$runtime" -DBUILD_SHARED_LIBS=OFF -DQJS_BUILD_LIBC=OFF -DQJS_BUILD_EXAMPLES=OFF -DQJS_BUILD_WERROR=OFF -DQJS_DISABLE_PARSER=OFF -DQJS_BUILD_CLI_WITH_MIMALLOC=OFF -DQJS_BUILD_CLI_WITH_STATIC_MIMALLOC=OFF
if errorlevel 1 exit /b 1
"$cmake" --build "$build" --target qjs
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($commandFile, $commandText, [Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /v:off /s /c "`"`"$commandFile`"`""
if ($LASTEXITCODE -ne 0) { throw "QuickJS $Configuration build failed. See $build." }
$library = Join-Path $build 'qjs.lib'
if (-not (Test-Path -LiteralPath $library -PathType Leaf)) { throw 'Upstream qjs target did not produce qjs.lib.' }
$include = Join-Path $stage 'include'
$lib = Join-Path $stage "lib/$Configuration"
$license = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $include, $lib, $license | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'quickjs.h') -Destination $include -Force
Copy-Item -LiteralPath $library -Destination (Join-Path $lib 'qjs.lib') -Force
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination (Join-Path $license 'QuickJS-ng-LICENSE') -Force
# Write provenance last. A failed copy cannot produce a valid new staging record.
[ordered]@{
    dependency = 'QuickJS-ng'; version = $version; repository = $repository; revision = $revision
    configuration = $Configuration; architecture = 'x64'; linkage = 'static'; target = 'qjs'
    compiler = 'MSVC'; toolVersion = $toolchain.ToolVersion; windowsSdk = $sdk
    cmakeVersion = $cmakeVersion; runtime = $runtime; libc = $false
    headerSha256 = (Get-FileHash -LiteralPath (Join-Path $include 'quickjs.h') -Algorithm SHA256).Hash
    librarySha256 = (Get-FileHash -LiteralPath (Join-Path $lib 'qjs.lib') -Algorithm SHA256).Hash
    licenseSha256 = (Get-FileHash -LiteralPath (Join-Path $license 'QuickJS-ng-LICENSE') -Algorithm SHA256).Hash
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage "$Configuration-manifest.json") -Encoding utf8
Write-Host "Staged QuickJS-ng $version $Configuration x64. This is a build record, not plugin/runtime behavior validation."
