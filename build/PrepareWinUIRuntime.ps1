#Requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$ApplicationManifest,
    [Parameter(Mandatory)][string]$Generated,
    [Parameter(Mandatory)][string]$Strings,
    [Parameter(Mandatory)][string]$MakePri,
    [Parameter(Mandatory)][string]$Interactive,
    [Parameter(Mandatory)][string]$Foundation,
    [Parameter(Mandatory)][string]$WinUI
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

[xml]$manifest = Get-Content -LiteralPath $ApplicationManifest -Raw
$assembly = $manifest.DocumentElement
$asmNamespace = 'urn:schemas-microsoft-com:asm.v3'
$winrtNamespace = 'urn:schemas-microsoft-com:winrt.v1'
$files = @{}
foreach ($package in @($Interactive, $Foundation, $WinUI)) {
    [xml]$fragment = Get-Content -LiteralPath (Join-Path $package 'runtimes-framework/package.appxfragment') -Raw
    $ns = [Xml.XmlNamespaceManager]::new($fragment.NameTable)
    $ns.AddNamespace('appx', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $servers = $fragment.SelectNodes('/appx:Fragment/appx:Extensions/appx:Extension/*', $ns)
    foreach ($server in $servers) {
        if ($server.LocalName -notin @('InProcessServer','ProxyStub')) { continue }
        $dll = $server.SelectSingleNode('appx:Path', $ns).InnerText
        # This proxy belongs to the singleton package, which is not app-local.
        if ($dll -eq 'PushNotificationsLongRunningTask.ProxyStub.dll') { continue }
        if (-not $files.ContainsKey($dll)) {
            $entry = $manifest.CreateElement('asmv3', 'file', $asmNamespace)
            $entry.SetAttribute('name', $dll)
            $null = $assembly.AppendChild($entry)
            $files[$dll] = $entry
        }
        $entry = $files[$dll]
        if ($server.LocalName -eq 'InProcessServer') {
            foreach ($class in $server.SelectNodes('appx:ActivatableClass', $ns)) {
                $node = $manifest.CreateElement('winrt', 'activatableClass', $winrtNamespace)
                $node.SetAttribute('name', $class.GetAttribute('ActivatableClassId'))
                $node.SetAttribute('threadingModel', 'both')
                $null = $entry.AppendChild($node)
            }
        } else {
            $node = $manifest.CreateElement('asmv3', 'comClass', $asmNamespace)
            $node.SetAttribute('clsid', [guid]::Parse($server.GetAttribute('ClassId')).ToString('B'))
            $null = $entry.AppendChild($node)
            foreach ($interface in $server.SelectNodes('appx:Interface', $ns)) {
                $node = $manifest.CreateElement('asmv3', 'comInterfaceProxyStub', $asmNamespace)
                $node.SetAttribute('name', $interface.GetAttribute('Name'))
                $node.SetAttribute('iid', [guid]::Parse($interface.GetAttribute('InterfaceId')).ToString('B'))
                $null = $entry.AppendChild($node)
            }
        }
    }
}
$manifest.Save((Join-Path $Generated 'IIMA.manifest'))

$priList = Join-Path $Generated 'pri.resfiles'
$xbfList = Join-Path $Generated 'xbf.resfiles'
@(
    Join-Path $Foundation 'runtimes-framework/win-x64/native/Microsoft.WindowsAppRuntime.pri'
    Join-Path $Interactive 'runtimes-framework/win-x64/native/Microsoft.UI.pri'
    Join-Path $WinUI 'runtimes-framework/win-x64/native/Microsoft.UI.Xaml.Controls.pri'
) | Set-Content -LiteralPath $priList -Encoding utf8
@('App.xbf','MainWindow.xbf') | Set-Content -LiteralPath $xbfList -Encoding utf8

$escapedRoot = [Security.SecurityElement]::Escape($Generated)
$escapedStrings = [Security.SecurityElement]::Escape($Strings)
$escapedPriList = [Security.SecurityElement]::Escape($priList)
$escapedXbfList = [Security.SecurityElement]::Escape($xbfList)
$configuration = Join-Path $Generated 'priconfig.xml'
$qualifiers = @'
    <default>
      <qualifier name="Language" value="en-US" />
      <qualifier name="Contrast" value="standard" />
      <qualifier name="Scale" value="200" />
      <qualifier name="HomeRegion" value="001" />
      <qualifier name="TargetSize" value="256" />
      <qualifier name="LayoutDirection" value="LTR" />
      <qualifier name="DXFeatureLevel" value="DX9" />
      <qualifier name="Configuration" value="" />
      <qualifier name="AlternateForm" value="" />
      <qualifier name="Platform" value="UAP" />
    </default>
'@
@"
<resources targetOsVersion="10.0.0" majorVersion="1">
  <index root="\" startIndexAt="$escapedPriList">
$qualifiers
    <indexer-config type="PRI" />
    <indexer-config type="RESFILES" qualifierDelimiter="." />
  </index>
  <index root="$escapedRoot" startIndexAt="$escapedXbfList">
$qualifiers
    <indexer-config type="RESFILES" qualifierDelimiter="." />
    <indexer-config type="EMBEDFILES" />
  </index>
  <index root="$escapedStrings" startIndexAt="">
$qualifiers
    <indexer-config type="FOLDER" foldernameAsQualifier="true" filenameAsQualifier="false" qualifierDelimiter="." />
    <indexer-config type="RESW" convertDotsToSlashes="true" initialPath="" />
  </index>
</resources>
"@ | Set-Content -LiteralPath $configuration -Encoding utf8
& $MakePri new /pr $Generated /cf $configuration /in IIMA /of (Join-Path $Generated 'IIMA.pri') /o |
    ForEach-Object { $_.Replace([string][char]0, '') }
if ($LASTEXITCODE -ne 0) { throw "MakePri failed ($LASTEXITCODE)." }
