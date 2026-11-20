#Requires -Version 7.0
. (Join-Path $PSScriptRoot 'Common.ps1')

$nuget = Join-Path $BuildRoot "tools/nuget-$($BuildConfig.nuget).exe"
if (-not (Test-Path -LiteralPath $nuget -PathType Leaf)) {
    New-Item -ItemType Directory -Force (Split-Path $nuget) | Out-Null
    $download = "$nuget.download"
    Invoke-WebRequest "https://dist.nuget.org/win-x86-commandline/v$($BuildConfig.nuget)/nuget.exe" -OutFile $download
    Move-Item -LiteralPath $download -Destination $nuget -Force
}
Invoke-IimaBuildTool {
    & $nuget restore (Join-Path $RepoRoot 'src/IIMA.App/packages.config') `
        -PackagesDirectory (Join-Path $BuildRoot 'packages') `
        -ConfigFile (Join-Path $RepoRoot 'NuGet.Config') -NonInteractive -Verbosity quiet
    if ($LASTEXITCODE -ne 0) { throw "NuGet restore failed ($LASTEXITCODE)." }
}
