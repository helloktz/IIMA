#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$Executable)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'The native window smoke test requires an interactive Windows desktop.' }
$exe = (Resolve-Path -LiteralPath $Executable).Path
$process = Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe) -WindowStyle Hidden -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
        if ($process.HasExited) { throw "Application exited before creating a window ($($process.ExitCode))." }
    } while ($process.MainWindowHandle -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) { throw 'No native window appeared within 20 seconds.' }
    if ($process.MainWindowTitle -notlike 'IIMA *') { throw "Unexpected window title: $($process.MainWindowTitle)" }

    # Give initialization failures time to surface before requesting a normal window close.
    Start-Sleep -Seconds 2
    $process.Refresh()
    if ($process.HasExited) { throw "Application exited during initialization ($($process.ExitCode))." }
    $title = $process.MainWindowTitle
    $modulePaths = @($process.Modules | ForEach-Object FileName)
    $exeRoot = (Split-Path $exe) + [IO.Path]::DirectorySeparatorChar
    foreach ($name in @('Microsoft.ui.xaml.dll','Microsoft.WindowsAppRuntime.dll','vcruntime140.dll','msvcp140.dll')) {
        $expected = Join-Path $exeRoot $name
        if ($modulePaths -notcontains $expected) { throw "Expected an app-local runtime module: $expected" }
    }
    if (-not $process.CloseMainWindow()) { throw 'The application did not accept a window close request.' }
    if (-not $process.WaitForExit(10000)) { throw 'The application did not exit within 10 seconds.' }
    if ($process.ExitCode -ne 0) { throw "Application exit code: $($process.ExitCode)" }
    Write-Host "PASS: $title; app-local runtimes loaded; normal exit 0."
} finally {
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
}
