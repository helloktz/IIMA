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
    try {
        $env:TEMP = $temp
        $env:TMP = $temp
        & $Action
    } finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
    }
}

function Assert-IimaLibMpvManifest([string]$SdkDirectory) {
    $manifest = Get-Content -LiteralPath (Join-Path $SdkDirectory 'manifest.json') -Raw | ConvertFrom-Json
    $pinnedCommit = 'bcde6b93cca4d128d84e1a60d1a6db2cd81c889e'
    if ($manifest.sourceCommit -ne $pinnedCommit) { throw "Expected mpv source commit $pinnedCommit." }
    $patch = Join-Path $RepoRoot 'build/patches/mpv-iima-owned-swapchain-v1.patch'
    $patchHash = (Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash
    if ($manifest.sourcePatch -ne 'mpv-iima-owned-swapchain-v1' -or $manifest.patchSha256 -ne $patchHash) {
        throw 'The libmpv SDK must match the SHA-256 of the current owned-swapchain patch. Stage a matching SDK.'
    }
    foreach ($field in @('sourceUrl','buildCommand','toolchain','dependencies')) {
        if (-not $manifest.$field) { throw "manifest.json must record $field." }
    }
}

function Assert-IimaLibMpvBinaries([string]$SdkDirectory, [string]$BinDirectory) {
    $hashes = @(Get-Content -LiteralPath (Join-Path $SdkDirectory 'verified-binaries.json') -Raw | ConvertFrom-Json)
    $actualNames = @(Get-ChildItem -LiteralPath (Join-Path $SdkDirectory 'bin') -File -Filter '*.dll' | ForEach-Object Name)
    if (-not $hashes.Count -or (Compare-Object $actualNames @($hashes | ForEach-Object file))) {
        throw 'Staged DLL inventory changed. Re-stage the libmpv SDK.'
    }
    foreach ($entry in $hashes) {
        if ([IO.Path]::GetFileName($entry.file) -ne $entry.file) { throw 'Invalid staged binary filename.' }
        $binary = Join-Path $BinDirectory $entry.file
        if ((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "libmpv DLL does not match the recorded SHA-256: $binary"
        }
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
