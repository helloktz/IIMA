#Requires -Version 7.0
[CmdletBinding()]
param()
. (Join-Path $PSScriptRoot 'Common.ps1')

$mpvRoot = Join-Path $RepoRoot '.deps/libmpv'
if (-not (Test-Path -LiteralPath (Join-Path $mpvRoot 'verified-binaries.json'))) { throw 'Stage the libmpv SDK before packaging; see docs/LIBMPV.md.' }

# Always package a fresh Release build; do not ship stale files from earlier experiments.
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Rebuild
$toolchain = Get-IimaToolchain
$version = [string]$BuildProperties.Project.PropertyGroup.IimaVersion
if ($version -notmatch '^[012356789]+\.[012356789]+\.[012356789]+$') {
    throw 'IimaVersion must have three numeric segments using the project-approved digits.'
}
$packageName = "IIMA-$version-win-x64"
$stage = Join-Path $RepoRoot "artifacts/package/$packageName"
Remove-IimaArtifactDirectory $stage
New-Item -ItemType Directory -Force $stage | Out-Null
$binaryRoot = Join-Path $RepoRoot 'artifacts/bin/x64/Release/IIMA.App'
Get-ChildItem -LiteralPath $binaryRoot -Force |
    Where-Object { $_.Extension -notin @('.pdb','.ilk','.lib','.exp') } |
    Copy-Item -Destination $stage -Recurse -Force

$crtRoot = Join-Path $toolchain.Root "VC/Redist/MSVC/$($toolchain.ToolVersion)/x64/Microsoft.VC145.CRT"
if (-not (Test-Path -LiteralPath $crtRoot)) { throw "Missing app-local VC++ Release runtime: $crtRoot" }
Get-ChildItem -LiteralPath $crtRoot -Filter '*.dll' | Copy-Item -Destination $stage
foreach ($required in @('IIMA.exe','Microsoft.ui.xaml.dll','Microsoft.WindowsAppRuntime.dll','vcruntime140.dll','msvcp140.dll','libmpv-2.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $stage $required))) { throw "Incomplete package: missing $required" }
}

$licensesRoot = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force $licensesRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $mpvRoot 'licenses') -Destination (Join-Path $licensesRoot 'libmpv') -Recurse
Copy-Item -LiteralPath (Join-Path $mpvRoot 'manifest.json') -Destination (Join-Path $stage 'libmpv-manifest.json')
Copy-Item -LiteralPath (Join-Path $mpvRoot 'verified-binaries.json') -Destination $stage
Copy-Item -LiteralPath (Join-Path $RepoRoot 'references/iina/LICENSE') -Destination (Join-Path $licensesRoot 'IINA-LICENSE')
[xml]$packages = Get-Content -LiteralPath (Join-Path $RepoRoot 'src/IIMA.App/packages.config') -Raw
foreach ($package in $packages.packages.package) {
    $folder = "$($package.id).$($package.version)"
    $packageRoot = Join-Path $RepoRoot ".deps/packages/$folder"
    $notices = Get-ChildItem -LiteralPath $packageRoot -File |
        Where-Object { $_.Name -match '(?i)license|notice|redist' }
    if ($notices) {
        $destination = Join-Path $licensesRoot $folder
        New-Item -ItemType Directory -Force $destination | Out-Null
        $notices | Copy-Item -Destination $destination
    }
}
Copy-Item -LiteralPath (Join-Path $RepoRoot 'build/PACKAGE-README.txt') -Destination (Join-Path $stage 'README.txt')
Copy-Item -LiteralPath (Join-Path $RepoRoot 'build/DEPENDENCIES.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $RepoRoot 'src/IIMA.App/packages.config') -Destination (Join-Path $stage 'build-packages.config')
$dist = Join-Path $RepoRoot 'artifacts/dist'
New-Item -ItemType Directory -Force $dist | Out-Null
$zipPath = Join-Path $dist "$packageName.zip"
Compress-Archive -LiteralPath $stage -DestinationPath $zipPath -CompressionLevel Optimal -Force
Write-Host "Package: $zipPath"
