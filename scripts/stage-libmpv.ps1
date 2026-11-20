#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdkDirectory)
. (Join-Path $PSScriptRoot 'Common.ps1')
$source = (Resolve-Path -LiteralPath $SdkDirectory).Path
$manifestPath = Join-Path $source 'manifest.json'
Assert-IimaLibMpvManifest $source
if (-not (Test-Path -LiteralPath (Join-Path $source 'licenses') -PathType Container) -or
    -not (Get-ChildItem -LiteralPath (Join-Path $source 'licenses') -File -Recurse)) { throw 'Supply dependency license/source materials in licenses/.' }
$destination = Join-Path $BuildRoot 'libmpv'
if (Test-Path -LiteralPath $destination) { throw 'SDK already staged at .build/libmpv; move it aside before replacing to avoid stale DLLs.' }
$pending = Join-Path $BuildRoot "libmpv-staging-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $pending | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $source 'bin'),(Join-Path $source 'licenses') -Destination $pending -Recurse
    Copy-Item -LiteralPath $manifestPath -Destination $pending
    $bin = Join-Path $pending 'bin'
    $catalog = Join-Path $source 'verified-binaries.json'
    if (Test-Path -LiteralPath $catalog -PathType Leaf) {
        Copy-Item -LiteralPath $catalog -Destination $pending
        Assert-IimaLibMpvBinaries $pending $bin
    } else {
        $hashes = @(Get-ChildItem -LiteralPath $bin -File -Filter '*.dll' | ForEach-Object {
            [pscustomobject]@{ file = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
        })
        $hashes | ConvertTo-Json -AsArray | Set-Content -LiteralPath (Join-Path $pending 'verified-binaries.json') -Encoding utf8
    }
    & (Join-Path $PSScriptRoot 'test-libmpv.ps1') -BinDirectory $bin
    [IO.Directory]::Move($pending, $destination)
} finally {
    Remove-IimaBuildDirectory $pending
}
Write-Host 'Staged the verified libmpv SDK.'
