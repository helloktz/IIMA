Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$script:BuildRoot = Join-Path $RepoRoot '.build'
$script:BuildConfig = Get-Content -LiteralPath (Join-Path $RepoRoot 'build/config.json') -Raw | ConvertFrom-Json

function Get-IimaToolchain {
    if (-not $IsWindows) { throw 'MSVC/WinUI builds require Windows.' }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio Build Tools with C++ desktop tools and the Windows SDK pinned in build/config.json.' }
    $instances = & $vswhere -products '*' -all -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
    $toolVersion = [string]$BuildConfig.msvc
    foreach ($instance in $instances) {
        $compiler = Join-Path $instance.installationPath "VC/Tools/MSVC/$toolVersion/bin/Hostx64/x64/cl.exe"
        $vcvars = Join-Path $instance.installationPath 'VC/Auxiliary/Build/vcvarsall.bat'
        if ((Test-Path -LiteralPath $compiler) -and (Test-Path -LiteralPath $vcvars)) {
            return [pscustomobject]@{ Root=$instance.installationPath; VCVars=$vcvars; ToolVersion=$toolVersion }
        }
    }
    throw "MSVC $toolVersion is required by build/config.json."
}

function Invoke-IimaMsvcEnvironment([scriptblock]$Action) {
    $toolchain = Get-IimaToolchain
    $command = 'call "{0}" x64 {1} -vcvars_ver={2} >nul && set' -f $toolchain.VCVars,$BuildConfig.windowsSdk,$toolchain.ToolVersion
    $setup = & $env:ComSpec /d /c $command
    if ($LASTEXITCODE -ne 0) { throw 'Unable to initialize the pinned x64 MSVC and Windows SDK environment.' }
    $previous = @{}
    try {
        foreach ($entry in $setup) {
            if ($entry -notmatch '^([^=]+)=(.*)$') { continue }
            $name = $Matches[1]
            $value = $Matches[2]
            $oldValue = [Environment]::GetEnvironmentVariable($name)
            if ($oldValue -ceq $value) { continue }
            $previous[$name] = $oldValue
            [Environment]::SetEnvironmentVariable($name, $value)
        }
        Invoke-IimaBuildTool $Action
    } finally {
        foreach ($name in $previous.Keys) { [Environment]::SetEnvironmentVariable($name, $previous[$name]) }
    }
}

function Invoke-IimaBuildTool([scriptblock]$Action) {
    $temp = Join-Path $BuildRoot 'tmp'
    New-Item -ItemType Directory -Force $temp | Out-Null
    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $previousNuGetCache = $env:NUGET_HTTP_CACHE_PATH
    try {
        $env:TEMP = $temp
        $env:TMP = $temp
        $env:NUGET_HTTP_CACHE_PATH = Join-Path $BuildRoot 'nuget-http-cache'
        & $Action
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        $env:NUGET_HTTP_CACHE_PATH = $previousNuGetCache
    }
}

function Remove-IimaBuildDirectory([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    $buildPrefix = $BuildRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove a path outside .build: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        if ((Get-Item -LiteralPath $fullPath).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Refusing to recursively remove a linked directory: $fullPath"
        }
        Remove-Item -LiteralPath $fullPath -Recurse -Force
    }
}
