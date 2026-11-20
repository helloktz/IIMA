#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Output,
    [Parameter(Mandatory)][string]$WindowsSdk,
    [Parameter(Mandatory)][string]$SdkVersion,
    [Parameter(Mandatory)][string]$VCTools,
    [Parameter(Mandatory)][string]$CppWinRT,
    [Parameter(Mandatory)][string]$WinUI,
    [Parameter(Mandatory)][string]$Foundation,
    [Parameter(Mandatory)][string]$Interactive,
    [Parameter(Mandatory)][string]$WebView2
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-SdkTool([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path -Leaf $File) failed ($LASTEXITCODE)." }
}

function New-XamlItem([string]$Path, [string]$DependentUpon = '', [bool]$IsSystemReference = $false) {
    @{
        FullPath = [IO.Path]::GetFullPath($Path)
        ItemSpec = if ([IO.Path]::GetExtension($Path) -ieq '.winmd') { [IO.Path]::GetFullPath($Path) }
                   else { [IO.Path]::GetRelativePath($Source, $Path) }
        DependentUpon = $DependentUpon
        IsSystemReference = $IsSystemReference
        IsNuGetReference = $false
        IsStaticLibraryReference = $false
        MSBuild_Link = ''
        MSBuild_TargetPath = ''
        MSBuild_XamlResourceMapName = ''
        MSBuild_XamlComponentResourceLocation = ''
    }
}

$Source = [IO.Path]::GetFullPath($Source)
$Output = [IO.Path]::GetFullPath($Output)
$sdkBin = Join-Path $WindowsSdk "bin/$SdkVersion/x64"
$unmerged = Join-Path $Output 'unmerged'
$componentSources = Join-Path $Output 'sources'
New-Item -ItemType Directory -Force -Path $Output,$unmerged,$componentSources | Out-Null

[xml]$platform = Get-Content -LiteralPath (Join-Path $WindowsSdk "Platforms/UAP/$SdkVersion/Platform.xml") -Raw
$platformMetadata = @($platform.ApplicationPlatform.ContainedApiContracts.ApiContract | ForEach-Object {
    Join-Path $WindowsSdk "References/$SdkVersion/$($_.name)/$($_.version)/$($_.name).winmd"
})
$sdkMetadata = @(
    Get-ChildItem -LiteralPath (Join-Path $Interactive 'metadata/10.0.18362.0'),(Join-Path $Foundation 'metadata'),(Join-Path $WinUI 'metadata') -Filter '*.winmd' -File |
        Sort-Object FullName | ForEach-Object FullName
    Join-Path $WebView2 'lib/Microsoft.Web.WebView2.Core.winmd'
)
$metadata = $sdkMetadata + $platformMetadata
$metadataDirectories = @($metadata | ForEach-Object { Split-Path -Parent $_ } | Select-Object -Unique)
$referenceArguments = @($metadata | ForEach-Object { '-ref'; $_ })
$projectMetadata = Join-Path $Output 'IIMA.winmd'
$platformArguments = @($platformMetadata | ForEach-Object { '-in'; $_ })
$sdkArguments = @($sdkMetadata | ForEach-Object { '-in'; $_ })
$platformReferenceArguments = @($platformMetadata | ForEach-Object { '-ref'; $_ })

$xamlCompiler = Join-Path $WinUI 'tools/net472/XamlCompiler.exe'
$xamlReferences = @($sdkMetadata | ForEach-Object { New-XamlItem $_ })
$xamlReferences += @($platformMetadata | ForEach-Object { New-XamlItem $_ -IsSystemReference $true })
# The façade supplies the projected forms of WinRT types such as IReference.
$xamlReferences += New-XamlItem (Join-Path $WindowsSdk "UnionMetadata/$SdkVersion/Facade/Windows.winmd")
$xamlInputs = @{
    ProjectPath = Join-Path $Source 'CMakeLists.txt'
    ProjectName = 'IIMA.App'
    RootNamespace = 'IIMA'
    Language = 'CppWinRT'
    LanguageSourceExtension = '.cpp'
    OutputType = 'exe'
    OutputPath = "$Output\"
    ReferenceAssemblyPaths = @()
    ReferenceAssemblies = $xamlReferences
    XamlApplications = @(New-XamlItem (Join-Path $Source 'App.xaml'))
    XamlPages = @(New-XamlItem (Join-Path $Source 'MainWindow.xaml'))
    ClIncludeFiles = @(
        New-XamlItem (Join-Path $Source 'pch.h')
        New-XamlItem (Join-Path $Source 'App.xaml.h') 'App.xaml'
        New-XamlItem (Join-Path $Source 'MainWindow.xaml.h') 'MainWindow.xaml'
    )
    CIncludeDirectories = "$Source;$Output"
    LocalAssembly = $null
    TargetPlatformMinVersion = '10.0.22000.0'
    FeatureControlFlags = 'EnableDefaultValidationContextGeneration;EnableWin32Codegen'
    SavedStateFile = Join-Path $Output 'XamlSaveStateFile.xml'
    GenXbfPath = (Join-Path $WinUI 'tools') + '\'
    PrecompiledHeaderFile = 'pch.h'
    UseVCMetaManaged = $true
    VCInstallPath32 = Join-Path $VCTools 'bin/Hostx64/x86/vcmeta.dll'
    VCInstallPath64 = Join-Path $VCTools 'bin/Hostx64/x64/vcmeta.dll'
    WindowsSdkPath = $WindowsSdk
}

function Invoke-XamlPass([bool]$FirstPass) {
    $xamlInputs.IsPass1 = $FirstPass
    $xamlInputs.CompileMode = if ($FirstPass) { 'RealBuildPass1' } else { 'RealBuildPass2' }
    $inputPath = Join-Path $Output "$($xamlInputs.CompileMode).input.json"
    $resultPath = Join-Path $Output "$($xamlInputs.CompileMode).output.json"
    $xamlInputs | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $inputPath -Encoding utf8
    & $xamlCompiler $inputPath $resultPath
    if ($LASTEXITCODE -ne 0) {
        $exitCode = $LASTEXITCODE
        if (Test-Path -LiteralPath $resultPath) {
            $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
            $result.MSBuildLogEntries | Where-Object Type -EQ 2 | ForEach-Object {
                Write-Host "$($_.File)($($_.LineNumber),$($_.ColumnNumber)): $($_.ErrorCode): $($_.Message)"
            }
        }
        throw "XamlCompiler $($xamlInputs.CompileMode) failed ($exitCode). See $resultPath."
    }
}

Remove-Item -LiteralPath $xamlInputs.SavedStateFile -ErrorAction Ignore
Invoke-XamlPass $true

$midl = Join-Path $sdkBin 'midl.exe'
$midlReferences = @($metadata | ForEach-Object { '/reference'; $_ })
foreach ($name in @('MainWindow','VideoSurface','WheelSlider','XamlMetaDataProvider')) {
    Invoke-SdkTool $midl (@(
        '/metadata_dir', (Split-Path -Parent ($platformMetadata | Where-Object { $_ -like '*Windows.Foundation.FoundationContract.winmd' })),
        '/I', (Join-Path $WindowsSdk "Include/$SdkVersion/winrt"),
        '/I', (Join-Path $WindowsSdk "Include/$SdkVersion/um"),
        '/I', (Join-Path $WindowsSdk "Include/$SdkVersion/shared"),
        '/winrt', '/nologo', '/char', 'signed', '/env', 'x64',
        '/winmd', (Join-Path $unmerged "$name.winmd"),
        '/h', (Join-Path $unmerged "${name}_h.h"),
        '/dlldata', 'nul', '/iid', 'nul', '/proxy', 'nul', '/tlb', (Join-Path $unmerged "$name.tlb"),
        '/client', 'none', '/server', 'none', '/enum_class', '/ns_prefix', '/target', 'NT60', '/nomidl'
    ) + $midlReferences + (Join-Path $Source "$name.idl"))
}

$mergeInputs = @(Get-ChildItem -LiteralPath $unmerged -File -Filter '*.winmd' | ForEach-Object { '-i'; $_.FullName })
$mergeReferences = @($metadataDirectories | ForEach-Object { '-metadata_dir'; $_ })
Invoke-SdkTool (Join-Path $sdkBin 'mdmerge.exe') ($mergeReferences + $mergeInputs + @('-o', $Output, '-partial', '-n:1'))
Invoke-SdkTool $CppWinRT ($platformArguments + @('-out', $Output))
Invoke-SdkTool $CppWinRT ($sdkArguments + $platformReferenceArguments + @('-out', $Output))
Invoke-SdkTool $CppWinRT (@('-overwrite', '-name', 'IIMA', '-pch', 'pch.h', '-prefix', '-comp', $componentSources,
    '-opt', '-in', $projectMetadata, '-out', $Output) + $referenceArguments)

$xamlInputs.LocalAssembly = @(New-XamlItem $projectMetadata)
Invoke-XamlPass $false
