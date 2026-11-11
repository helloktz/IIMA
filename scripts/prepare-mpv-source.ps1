#Requires -Version 7.0
[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Common.ps1')
$commit = 'bcde6b93cca4d128d84e1a60d1a6db2cd81c889e'
$source = Join-Path $RepoRoot '.deps/mpv-source'
$patch = Join-Path $RepoRoot 'build/patches/mpv-iima-owned-swapchain-v1.patch'
if (-not (Test-Path -LiteralPath $source)) {
    New-Item -ItemType Directory -Force (Split-Path $source) | Out-Null
    & git clone https://github.com/mpv-player/mpv.git $source
    if ($LASTEXITCODE -ne 0) { throw 'Cannot obtain mpv source.' }
    & git -C $source checkout --detach $commit
    if ($LASTEXITCODE -ne 0) { throw 'Cannot check out pinned mpv source.' }
}
$head = & git -C $source rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $commit) { throw 'Existing .deps/mpv-source must be at the pinned commit; no automatic reset is performed.' }
$changes = & git -C $source status --porcelain
if ($LASTEXITCODE -ne 0 -or $changes) { throw 'Source directory has changes (possibly the patch is already applied). Preserve them; do not reapply automatically.' }
& git -C $source apply --check $patch
if ($LASTEXITCODE -ne 0) { throw 'Owned-swapchain patch does not apply cleanly.' }
& git -C $source apply $patch
if ($LASTEXITCODE -ne 0) { throw 'Owned-swapchain patch failed.' }
Write-Host "Prepared $source. Build in CLANG64 using docs/LIBMPV.md. No toolchain was installed or build run."
Write-Host "Record sourcePatch=mpv-iima-owned-swapchain-v1 and patchSha256=$((Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash) in SDK manifest.json."
