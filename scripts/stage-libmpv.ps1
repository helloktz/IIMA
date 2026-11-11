#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdkDirectory)
. (Join-Path $PSScriptRoot 'Common.ps1')
$source = (Resolve-Path -LiteralPath $SdkDirectory).Path
$manifestPath = Join-Path $source 'manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$pinnedCommit = 'bcde6b93cca4d128d84e1a60d1a6db2cd81c889e'
if ($manifest.sourceCommit -ne $pinnedCommit) { throw "Expected mpv source commit $pinnedCommit." }
$patch = Join-Path $RepoRoot 'build/patches/mpv-iima-owned-swapchain-v1.patch'
$patchHash = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash
if ($manifest.sourcePatch -ne 'mpv-iima-owned-swapchain-v1' -or $manifest.patchSha256 -ne $patchHash) {
    throw 'SDK must record the current owned-swapchain patch and its exact SHA256; see scripts/prepare-mpv-source.ps1.'
}
foreach ($field in @('sourceUrl','buildCommand','toolchain','dependencies')) {
    if (-not $manifest.$field) { throw "manifest.json must record $field." }
}
if (-not (Test-Path -LiteralPath (Join-Path $source 'licenses') -PathType Container) -or
    -not (Get-ChildItem -LiteralPath (Join-Path $source 'licenses') -File -Recurse)) { throw 'Supply dependency license/source materials in licenses/.' }
$destination = Join-Path $RepoRoot '.deps/libmpv'
if (Test-Path -LiteralPath $destination) { throw 'SDK already staged at .deps/libmpv; move it aside before replacing to avoid stale DLLs.' }
& (Join-Path $PSScriptRoot 'test-libmpv.ps1') -BinDirectory (Join-Path $source 'bin')
New-Item -ItemType Directory -Force $destination | Out-Null
Copy-Item -LiteralPath (Join-Path $source 'bin') -Destination $destination -Recurse
Copy-Item -LiteralPath (Join-Path $source 'licenses') -Destination $destination -Recurse
Copy-Item -LiteralPath $manifestPath -Destination $destination
$hashes = @(Get-ChildItem -LiteralPath (Join-Path $destination 'bin') -File -Filter '*.dll' | ForEach-Object {
    [pscustomobject]@{ file = $_.Name; sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
})
$hashes | ConvertTo-Json -AsArray | Set-Content -LiteralPath (Join-Path $destination 'verified-binaries.json') -Encoding utf8
Write-Host 'Staged verified SDK. Playback/composition still require the Windows validation checklist.'
