Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
[xml]$script:BuildProperties = Get-Content -LiteralPath (Join-Path $RepoRoot 'Directory.Build.props') -Raw

function Get-IimaToolchain {
    if (-not $IsWindows) { throw 'MSVC/WinUI builds require a Windows worker. See docs/BUILD.md.' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio 2026 Build Tools with C++ desktop tools and Windows SDK. See docs/BUILD.md.' }
    $instances = & $vswhere -products '*' -all -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $toolVersion = [string]$BuildProperties.Project.PropertyGroup.VCToolsVersion
    foreach ($instance in $instances) {
        $compiler = Join-Path $instance.installationPath "VC/Tools/MSVC/$toolVersion/bin/Hostx64/x64/cl.exe"
        $msbuild = Join-Path $instance.installationPath 'MSBuild/Current/Bin/MSBuild.exe'
        if ((Test-Path -LiteralPath $compiler) -and (Test-Path -LiteralPath $msbuild)) {
            return [pscustomobject]@{ Root=$instance.installationPath; MSBuild=$msbuild; ToolVersion=$toolVersion }
        }
    }
    throw "MSVC $toolVersion is required by Directory.Build.props. Provision the pinned toolset on the Windows worker."
}

function Remove-IimaArtifactDirectory([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $artifactRoot = [IO.Path]::GetFullPath((Join-Path $RepoRoot 'artifacts')) + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside artifacts: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        if ((Get-Item -LiteralPath $fullPath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to recursively remove a linked directory: $fullPath"
        }
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}
