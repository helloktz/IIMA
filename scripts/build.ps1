#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Rebuild
)
. (Join-Path $PSScriptRoot 'Common.ps1')
$toolchain = Get-IimaToolchain
$nugetVersion = [string]$BuildProperties.Project.PropertyGroup.NuGetVersion
$nuget = Join-Path $BuildRoot "tools/nuget-$nugetVersion.exe"
if (-not (Test-Path -LiteralPath $nuget)) {
    New-Item -ItemType Directory -Force (Split-Path $nuget) | Out-Null
    $temporaryDownload = "$nuget.download"
    Invoke-WebRequest "https://dist.nuget.org/win-x86-commandline/v$nugetVersion/nuget.exe" -OutFile $temporaryDownload
    Move-Item -LiteralPath $temporaryDownload -Destination $nuget -Force
}
Invoke-IimaBuildTool {
    & $nuget restore (Join-Path $RepoRoot 'src/IIMA.App/packages.config') `
        -PackagesDirectory (Join-Path $BuildRoot 'packages') `
        -ConfigFile (Join-Path $RepoRoot 'NuGet.Config') -NonInteractive -Verbosity quiet
    if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed ($LASTEXITCODE)." }
}

if ($Rebuild) {
    Remove-IimaBuildDirectory (Join-Path $BuildRoot "bin/x64/$Configuration")
    Remove-IimaBuildDirectory (Join-Path $BuildRoot "obj/x64/$Configuration")
}
$logRoot = Join-Path $BuildRoot 'logs'
New-Item -ItemType Directory -Force $logRoot | Out-Null
Invoke-IimaBuildTool {
    & $toolchain.MSBuild (Join-Path $RepoRoot 'IIMA.sln') /m /t:Build /nologo /v:quiet `
        "/p:Configuration=$Configuration" /p:Platform=x64 `
        "/bl:$logRoot/$Configuration.binlog" /fl "/flp:logfile=$logRoot/$Configuration.log;verbosity=normal"
    if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE). See .build/logs/$Configuration.log." }
}
$applicationOutput = Join-Path $BuildRoot "bin/x64/$Configuration/IIMA.App"
Write-Host "Built: $applicationOutput/IIMA.exe"

# The player can be compiled without a media SDK.
$mpvRoot = Join-Path $BuildRoot 'libmpv'
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
        Copy-Item -Destination $applicationOutput -Force
} else {
    Write-Warning 'libmpv SDK is absent: application compilation only. Playback requires scripts/stage-libmpv.ps1.'
}
