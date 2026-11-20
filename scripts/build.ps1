#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Rebuild
)
. (Join-Path $PSScriptRoot 'Common.ps1')

$preset = $Configuration.ToLowerInvariant()
$logRoot = Join-Path $BuildRoot 'logs'
New-Item -ItemType Directory -Force $logRoot | Out-Null
Invoke-IimaMsvcEnvironment {
    Push-Location -LiteralPath $RepoRoot
    try {
        & cmake --preset $preset 2>&1 | Tee-Object -FilePath (Join-Path $logRoot "$preset-configure.log")
        if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
        $buildArguments = @('--build','--preset',$preset)
        if ($Rebuild) { $buildArguments += '--clean-first' }
        & cmake @buildArguments 2>&1 | Tee-Object -FilePath (Join-Path $logRoot "$preset-ninja.log")
        if ($LASTEXITCODE -ne 0) { throw "Ninja build failed ($LASTEXITCODE). See .build/logs/$preset-ninja.log." }
    } finally {
        Pop-Location
    }
}

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
} else {
    Write-Warning 'libmpv SDK is absent: application compilation only. Playback requires scripts/stage-libmpv.ps1.'
}
Write-Host "Built: $(Join-Path $BuildRoot "cmake/$preset/bin/IIMA.exe")"
