#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Rebuild
)
. (Join-Path $PSScriptRoot 'Common.ps1')
$toolchain = Get-IimaToolchain
$quickjsRoot = Join-Path $RepoRoot '.deps/quickjs'
$quickjsManifestPath = Join-Path $quickjsRoot "$Configuration-manifest.json"
if (-not (Test-Path -LiteralPath $quickjsManifestPath -PathType Leaf)) {
    throw "Stage QuickJS-ng first: scripts/build-quickjs.ps1 -Configuration $Configuration. See docs/QUICKJS.md."
}
$quickjs = Get-Content -LiteralPath $quickjsManifestPath -Raw | ConvertFrom-Json
$expectedRuntime = if ($Configuration -eq 'Debug') { 'MultiThreadedDebugDLL' } else { 'MultiThreadedDLL' }
if ($quickjs.dependency -ne 'QuickJS-ng' -or $quickjs.version -ne '0.12.1' -or
    $quickjs.revision -ne '58bdcf0ce35594df30bf5cfbba3be8b454799cc0' -or
    $quickjs.configuration -ne $Configuration -or $quickjs.architecture -ne 'x64' -or
    $quickjs.linkage -ne 'static' -or $quickjs.target -ne 'qjs' -or $quickjs.libc -ne $false -or
    $quickjs.toolVersion -ne $toolchain.ToolVersion -or $quickjs.runtime -ne $expectedRuntime -or
    $quickjs.cmakeVersion -ne '4.2.0' -or
    $quickjs.windowsSdk -ne [string]$BuildProperties.Project.PropertyGroup.WindowsTargetPlatformVersion) {
    throw 'QuickJS staging does not match the pinned engine/toolchain/configuration. Rebuild with scripts/build-quickjs.ps1.'
}
foreach ($entry in @(
    @{ Path='include/quickjs.h'; Hash=$quickjs.headerSha256 },
    @{ Path="lib/$Configuration/qjs.lib"; Hash=$quickjs.librarySha256 },
    @{ Path='licenses/QuickJS-ng-LICENSE'; Hash=$quickjs.licenseSha256 }
)) {
    $path = Join-Path $quickjsRoot $entry.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.Hash) {
        throw "QuickJS staged file missing/changed: $path. Rebuild before linking."
    }
}
$nugetVersion = [string]$BuildProperties.Project.PropertyGroup.NuGetVersion
$nuget = Join-Path $RepoRoot ".deps/tools/nuget-$nugetVersion.exe"
if (-not (Test-Path -LiteralPath $nuget)) {
    New-Item -ItemType Directory -Force (Split-Path $nuget) | Out-Null
    $temporaryDownload = "$nuget.download"
    Invoke-WebRequest "https://dist.nuget.org/win-x86-commandline/v$nugetVersion/nuget.exe" -OutFile $temporaryDownload
    Move-Item -LiteralPath $temporaryDownload -Destination $nuget -Force
}
& $nuget restore (Join-Path $RepoRoot 'src/IIMA.App/packages.config') `
    -PackagesDirectory (Join-Path $RepoRoot '.deps/packages') `
    -ConfigFile (Join-Path $RepoRoot 'NuGet.Config') -NonInteractive -Verbosity quiet
if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed ($LASTEXITCODE)." }

if ($Rebuild) {
    Remove-IimaArtifactDirectory (Join-Path $RepoRoot "artifacts/bin/x64/$Configuration")
    Remove-IimaArtifactDirectory (Join-Path $RepoRoot "artifacts/obj/x64/$Configuration")
}
$logRoot = Join-Path $RepoRoot 'artifacts/logs'
New-Item -ItemType Directory -Force $logRoot | Out-Null
& $toolchain.MSBuild (Join-Path $RepoRoot 'IIMA.sln') /m /t:Build /nologo /v:quiet `
    "/p:Configuration=$Configuration" /p:Platform=x64 `
    "/bl:$logRoot/$Configuration.binlog" /fl "/flp:logfile=$logRoot/$Configuration.log;verbosity=normal"
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE). See artifacts/logs/$Configuration.log." }
Write-Host "Built: artifacts/bin/x64/$Configuration/IIMA.App/IIMA.exe"

# Source-only development remains buildable without a media SDK; never silently package it.
$mpvRoot = Join-Path $RepoRoot '.deps/libmpv'
if (Test-Path -LiteralPath $mpvRoot) {
    $mpvManifest = Get-Content -LiteralPath (Join-Path $mpvRoot 'manifest.json') -Raw | ConvertFrom-Json
    $patchHash = (Get-FileHash -LiteralPath (Join-Path $RepoRoot 'build/patches/mpv-iima-owned-swapchain-v1.patch') -Algorithm SHA256).Hash
    if ($mpvManifest.sourcePatch -ne 'mpv-iima-owned-swapchain-v1' -or $mpvManifest.patchSha256 -ne $patchHash) { throw 'SDK owned-swapchain patch is out of date; rebuild and re-stage.' }
    $hashes = Get-Content -LiteralPath (Join-Path $mpvRoot 'verified-binaries.json') -Raw | ConvertFrom-Json
    $actualNames = @(Get-ChildItem -LiteralPath (Join-Path $mpvRoot 'bin') -File -Filter '*.dll' | ForEach-Object Name)
    $recordedNames = @($hashes | ForEach-Object file)
    if (-not $recordedNames.Count -or (Compare-Object $actualNames $recordedNames)) { throw 'Staged DLL inventory changed; re-stage the SDK.' }
    foreach ($entry in $hashes) {
        if ([IO.Path]::GetFileName($entry.file) -ne $entry.file) { throw 'Invalid staged binary filename.' }
        $binary = Join-Path $mpvRoot "bin/$($entry.file)"
        if ((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $entry.sha256) { throw "Staged DLL changed: $binary. Re-run SDK staging." }
    }
    & (Join-Path $PSScriptRoot 'test-libmpv.ps1') -BinDirectory (Join-Path $mpvRoot 'bin')
    Get-ChildItem -LiteralPath (Join-Path $mpvRoot 'bin') -File -Filter '*.dll' |
        Copy-Item -Destination (Join-Path $RepoRoot "artifacts/bin/x64/$Configuration/IIMA.App") -Force
} else {
    Write-Warning 'libmpv SDK is absent: application compilation only. Playback and packaging require scripts/stage-libmpv.ps1.'
}
