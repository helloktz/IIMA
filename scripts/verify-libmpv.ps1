#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$SdkDirectory)
. (Join-Path $PSScriptRoot 'Common.ps1')

$bin = Join-Path $SdkDirectory 'bin'
Assert-IimaLibMpvManifest $SdkDirectory
Assert-IimaLibMpvBinaries $SdkDirectory $bin
& (Join-Path $PSScriptRoot 'test-libmpv.ps1') -BinDirectory $bin
