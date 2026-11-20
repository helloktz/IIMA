#Requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$Rebuild
)
. (Join-Path $PSScriptRoot 'Common.ps1')

$mpvRoot = Join-Path $BuildRoot 'libmpv'
if (-not (Test-Path -LiteralPath $mpvRoot) -and $env:IIMA_LIBMPV_SDK) {
    & (Join-Path $PSScriptRoot 'stage-libmpv.ps1') -SdkDirectory $env:IIMA_LIBMPV_SDK
}
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
if (-not (Test-Path -LiteralPath (Join-Path $mpvRoot 'verified-binaries.json') -PathType Leaf)) {
    Write-Warning 'libmpv SDK is absent: application compilation only. Playback requires scripts/stage-libmpv.ps1.'
}
Write-Host "Built: $(Join-Path $BuildRoot "cmake/$preset/bin/IIMA.exe")"
