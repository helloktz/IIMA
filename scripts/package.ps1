#Requires -Version 7.0
[CmdletBinding()]
param([switch]$Rebuild)
. (Join-Path $PSScriptRoot 'Common.ps1')

function Set-IimaMsiRuntimeLanguage([string]$PackagePath) {
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $view = $null
    $invoke = [Reflection.BindingFlags]::InvokeMethod
    $get = [Reflection.BindingFlags]::GetProperty
    $set = [Reflection.BindingFlags]::SetProperty
    try {
        $database = $installer.GetType().InvokeMember('OpenDatabase', $invoke, $null, $installer, @($PackagePath, 1))
        $view = $database.GetType().InvokeMember('OpenView', $invoke, $null, $database, @('SELECT * FROM File'))
        $null = $view.GetType().InvokeMember('Execute', $invoke, $null, $view, $null)
        while ($row = $view.GetType().InvokeMember('Fetch', $invoke, $null, $view, $null)) {
            $name = $row.GetType().InvokeMember('StringData', $get, $null, $row, @(3))
            if ($name -notmatch '(?:^|\|)Microsoft\.UI\.Xaml(?:\.Phone)?\.dll(?:\.mui)?$') { continue }
            $language = $row.GetType().InvokeMember('StringData', $get, $null, $row, @(6))
            # WinUI ships language lists longer than the MSI field and LCIDs ICE03 rejects.
            # Use neutral language for those app-local files in MSI; the payload is unchanged.
            if ($language.Length -le 20 -and $language -notmatch '^(1152|1153|1169)$') { continue }
            $null = $row.GetType().InvokeMember('StringData', $set, $null, $row, @(6, '0'))
            $null = $view.GetType().InvokeMember('Modify', $invoke, $null, $view, @(2, $row))
        }
        $null = $view.GetType().InvokeMember('Close', $invoke, $null, $view, $null)
        $null = $database.GetType().InvokeMember('Commit', $invoke, $null, $database, $null)
    } finally {
        foreach ($object in @($view, $database, $installer)) {
            if ($object) { $null = [Runtime.InteropServices.Marshal]::FinalReleaseComObject($object) }
        }
    }
}

$version = [string]$BuildConfig.version
$msiVersion = $null
if ($version -notmatch '^(0|[1-35-9][0-35-9]*)\.(0|[1-35-9][0-35-9]*)\.(0|[1-35-9][0-35-9]*)$' -or
    -not [version]::TryParse($version, [ref]$msiVersion) -or
    $msiVersion.Major -gt 255 -or $msiVersion.Minor -gt 255 -or $msiVersion.Build -gt 65535) {
    throw 'The version in build/config.json must be an MSI-compatible three-part version using the project-approved digits.'
}
$mpvRoot = Join-Path $BuildRoot 'libmpv'
if (-not $env:IIMA_LIBMPV_SDK -and -not (Test-Path -LiteralPath (Join-Path $mpvRoot 'verified-binaries.json'))) {
    throw 'Stage the libmpv SDK with scripts/stage-libmpv.ps1 before packaging.'
}
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -Rebuild:$Rebuild

$wixVersion = [string]$BuildConfig.wix
$wix = Get-Command wix -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $wix -or ((& $wix.Source --version) -split '\+')[0] -ne $wixVersion) {
    $toolDirectory = Join-Path $BuildRoot "tools/wix-$wixVersion"
    $localWix = Join-Path $toolDirectory 'wix.exe'
    if (-not (Test-Path -LiteralPath $localWix)) {
        Invoke-IimaBuildTool {
            dotnet tool install wix --tool-path $toolDirectory --version $wixVersion --configfile (Join-Path $RepoRoot 'NuGet.Config')
            if ($LASTEXITCODE -ne 0) { throw "Could not install WiX $wixVersion ($LASTEXITCODE)." }
        }
    }
    $wix = Get-Command $localWix -CommandType Application
}

$toolchain = Get-IimaToolchain
$packageName = "IIMA-$version-win-x64"
$work = Join-Path $BuildRoot 'package'
Remove-IimaBuildDirectory $work
$payload = Join-Path $work $packageName
$pending = Join-Path $work 'dist'
New-Item -ItemType Directory -Path $payload,$pending -Force | Out-Null

$binaryRoot = Join-Path $BuildRoot 'cmake/release/bin'
Get-ChildItem -LiteralPath $binaryRoot -Force |
    Where-Object { $_.Extension -notin @('.pdb','.ilk','.lib','.exp') } |
    Copy-Item -Destination $payload -Recurse -Force
$crtRoot = Join-Path $toolchain.Root "VC/Redist/MSVC/$($toolchain.ToolVersion)/x64/Microsoft.VC145.CRT"
Get-ChildItem -LiteralPath $crtRoot -File -Filter '*.dll' | Copy-Item -Destination $payload
foreach ($required in @('IIMA.exe','Microsoft.ui.xaml.dll','Microsoft.WindowsAppRuntime.dll','vcruntime140.dll','msvcp140.dll','libmpv-2.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $payload $required) -PathType Leaf)) {
        throw "Incomplete package: missing $required"
    }
}
Assert-IimaLibMpvBinaries $mpvRoot $payload

$licenses = Join-Path $payload 'licenses'
New-Item -ItemType Directory -Path $licenses | Out-Null
Copy-Item -LiteralPath (Join-Path $mpvRoot 'licenses') -Destination (Join-Path $licenses 'libmpv') -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot 'build/patches/mpv-iima-owned-swapchain-v1.patch') -Destination (Join-Path $licenses 'libmpv')
Copy-Item -LiteralPath (Join-Path $mpvRoot 'manifest.json') -Destination (Join-Path $payload 'libmpv-manifest.json')
Copy-Item -LiteralPath (Join-Path $mpvRoot 'verified-binaries.json') -Destination $payload
Copy-Item -LiteralPath (Join-Path $RepoRoot 'references/iina/LICENSE') -Destination (Join-Path $licenses 'IINA-LICENSE')
Copy-Item -LiteralPath (Join-Path $RepoRoot 'references/iina/IIMA-SOURCE.txt') -Destination (Join-Path $licenses 'IINA-SOURCE.txt')
$packageManifest = Join-Path $RepoRoot 'build/IIMA.Dependencies.csproj'
$packages = (& (Join-Path $PSScriptRoot 'resolve-nuget-packages.ps1') | ConvertFrom-Json -AsHashtable).packages
foreach ($package in $packages.GetEnumerator()) {
    $folder = "$($package.Key).$(Split-Path -Leaf $package.Value)"
    $notices = Get-ChildItem -LiteralPath $package.Value -File |
        Where-Object { $_.Name -match '(?i)license|notice|redist' }
    if ($notices) {
        $destination = Join-Path $licenses $folder
        New-Item -ItemType Directory -Path $destination | Out-Null
        $notices | Copy-Item -Destination $destination
    }
}
Copy-Item -LiteralPath $packageManifest -Destination (Join-Path $payload 'build-packages.csproj')

$msiPath = Join-Path $pending "$packageName.msi"
Invoke-IimaBuildTool {
    & $wix.Source build (Join-Path $RepoRoot 'build/IIMA.wxs') (Join-Path $RepoRoot 'build/InstallerUI.wxs') -arch x64 `
        -loc (Join-Path $RepoRoot 'build/Strings/Installer.en-US.wxl') `
        -loc (Join-Path $RepoRoot 'build/Strings/Installer.zh-CN.wxl') `
        -bindpath "payload=$payload" -define "IimaVersion=$version" `
        -intermediatefolder (Join-Path $work 'wix') -pdbtype none -out $msiPath
    if ($LASTEXITCODE -ne 0) { throw "WiX build failed ($LASTEXITCODE)." }
}
Set-IimaMsiRuntimeLanguage (Resolve-Path -LiteralPath $msiPath).ProviderPath
Invoke-IimaBuildTool {
    & $wix.Source msi validate -intermediatefolder (Join-Path $work 'ice') $msiPath
    if ($LASTEXITCODE -ne 0) { throw "MSI validation failed ($LASTEXITCODE)." }
}
[IO.Compression.ZipFile]::CreateFromDirectory($payload, (Join-Path $pending "$packageName.zip"), 'Optimal', $true)
[IO.File]::WriteAllText((Join-Path $pending 'version'), $version)

# Switch the distribution directory only after both packages are ready.
$dist = Join-Path $RepoRoot '.dist'
$previous = Join-Path $work 'previous-dist'
if (Test-Path -LiteralPath $dist) {
    if ((Get-Item -LiteralPath $dist).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Refusing to replace a linked .dist directory.'
    }
    Move-Item -LiteralPath $dist -Destination $previous
}
try {
    Move-Item -LiteralPath $pending -Destination $dist
} catch {
    if (Test-Path -LiteralPath $previous) { Move-Item -LiteralPath $previous -Destination $dist }
    throw
}
Remove-IimaBuildDirectory $previous
Get-ChildItem -LiteralPath $dist -File | ForEach-Object { Write-Host $_.FullName }
