#Requires -Version 7.0
. (Join-Path $PSScriptRoot 'Common.ps1')

$nuget = Get-Command nuget -CommandType Application -ErrorAction SilentlyContinue
if (-not $nuget) { throw 'NuGet CLI was not found on PATH.' }

Push-Location -LiteralPath $RepoRoot
try {
    $output = & $nuget.Source locals global-packages -list -ForceEnglishOutput
    if ($LASTEXITCODE -ne 0) { throw "NuGet could not list its shared package cache ($LASTEXITCODE)." }
} finally {
    Pop-Location
}

if (($output -join [Environment]::NewLine) -notmatch '(?m)^global-packages:[ \t]*([^\r\n]+)') {
    throw 'NuGet did not report a shared package cache path.'
}
$cache = $Matches[1].Trim()
$manifest = Join-Path $RepoRoot 'build/IIMA.Dependencies.csproj'
[xml]$project = Get-Content -LiteralPath $manifest -Raw
$packagePaths = [ordered]@{}
foreach ($package in $project.Project.ItemGroup.PackageDownload) {
    $id = $package.Include.ToLowerInvariant()
    $version = $package.Version.Trim('[', ']')
    $packagePaths[$package.Include] = Join-Path $cache "$id/$version"
}

$isMissing = { -not (Test-Path -LiteralPath (Join-Path $_.Value '.nupkg.metadata') -PathType Leaf) }
$missing = @($packagePaths.GetEnumerator() | Where-Object $isMissing)
if ($missing.Count) {
    $dotnet = Get-Command dotnet -CommandType Application -ErrorAction SilentlyContinue
    if (-not $dotnet) { throw 'The .NET SDK is required to restore missing NuGet packages.' }
    $intermediate = (Join-Path $BuildRoot 'nuget-restore') + [IO.Path]::DirectorySeparatorChar
    Push-Location -LiteralPath $RepoRoot
    try {
        Invoke-IimaBuildTool {
            & $dotnet.Source restore $manifest --packages $cache --force `
                "-p:BaseIntermediateOutputPath=$intermediate" --verbosity minimal 2>&1 |
                ForEach-Object { [Console]::Error.WriteLine($_.ToString()) }
            if ($LASTEXITCODE -ne 0) { throw "NuGet package restore failed ($LASTEXITCODE)." }
        }
    } finally {
        Pop-Location
    }
    $missing = @($packagePaths.GetEnumerator() | Where-Object $isMissing)
    if ($missing.Count) {
        throw "NuGet restore did not populate these cache entries: $($missing.Value -join ', ')."
    }
}

@{ cache = $cache; packages = $packagePaths } | ConvertTo-Json -Compress
