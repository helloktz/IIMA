#Requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$BinDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $IsWindows -or -not [Environment]::Is64BitProcess) { throw 'Run this probe in x64 PowerShell on Windows.' }
$bin = (Resolve-Path -LiteralPath $BinDirectory).Path
$dll = Join-Path $bin 'libmpv-2.dll'
# Check the PE machine before loading any native code, including companion DLLs.
foreach ($file in Get-ChildItem -LiteralPath $bin -Filter '*.dll') {
    $stream = [IO.File]::OpenRead($file.FullName)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a PE image: $file" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw "Invalid PE offset: $file" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw "Expected AMD64 PE: $file" }
    } finally { $reader.Dispose(); $stream.Dispose() }
}
if (-not ('IimaMpvProbe' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class IimaMpvProbe {
 [DllImport("kernel32", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
 [DllImport("kernel32", CharSet=CharSet.Ansi, ExactSpelling=true)] static extern IntPtr GetProcAddress(IntPtr dll, string symbol);
 [DllImport("kernel32")] static extern bool FreeLibrary(IntPtr dll);
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate uint Version();
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int Acquire(IntPtr handle, out IntPtr chain);
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate IntPtr Create();
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate void Destroy(IntPtr handle);
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int Initialize(IntPtr handle);
 [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int SetOption(IntPtr handle, [MarshalAs(UnmanagedType.LPUTF8Str)] string name, [MarshalAs(UnmanagedType.LPUTF8Str)] string value);
 static T Function<T>(IntPtr dll, string name) where T:Delegate {
   var pointer=GetProcAddress(dll,name);
   if(pointer==IntPtr.Zero) throw new Exception("Missing C ABI export: "+name);
   return Marshal.GetDelegateForFunctionPointer<T>(pointer);
 }
 public static uint Check(string path) {
   // Dependencies resolve only beside this DLL and in System32, never from PATH/CWD.
   var dll=LoadLibraryEx(path,IntPtr.Zero,0x100|0x800);
   if(dll==IntPtr.Zero) throw new Exception("LoadLibraryEx failed: "+Marshal.GetLastWin32Error()+". Stage all companion DLLs.");
   IntPtr handle=IntPtr.Zero;
   try {
     foreach(var symbol in new[]{"mpv_iima_acquire_d3d11_swapchain_v1","mpv_terminate_destroy","mpv_command_async","mpv_set_property_async","mpv_set_property_string","mpv_get_property","mpv_observe_property","mpv_wait_event","mpv_wakeup","mpv_error_string","mpv_free","mpv_free_node_contents","mpv_request_log_messages"})
       if(GetProcAddress(dll,symbol)==IntPtr.Zero) throw new Exception("Missing C ABI export: "+symbol);
     uint api=Function<Version>(dll,"mpv_client_api_version")();
     if((api>>16)!=2 || (api&0xffff)<6) throw new Exception("Need libmpv client API 2.6 or compatible newer 2.x; got "+api);
     handle=Function<Create>(dll,"mpv_create")();
     if(handle==IntPtr.Zero) throw new Exception("mpv_create failed");
     var option=Function<SetOption>(dll,"mpv_set_option_string");
     string[,] settings={{"config","no"},{"load-scripts","no"},{"vo","gpu-next"},{"gpu-api","d3d11"},{"gpu-context","d3d11"},{"d3d11-output-mode","composition"},{"d3d11-composition-size","640x360"},{"idle","yes"}};
     for(int i=0;i<settings.GetLength(0);i++) {
       int result=option(handle,settings[i,0],settings[i,1]);
       if(result<0) throw new Exception("Unsupported option "+settings[i,0]+": "+result);
     }
     int initialized=Function<Initialize>(dll,"mpv_initialize")(handle);
     if(initialized<0) throw new Exception("mpv_initialize failed: "+initialized);
     IntPtr chain;
     int acquired=Function<Acquire>(dll,"mpv_iima_acquire_d3d11_swapchain_v1")(handle,out chain);
     if(chain!=IntPtr.Zero) Marshal.Release(chain);
     if((acquired!=0 && acquired!=-10) || (acquired==0 && chain==IntPtr.Zero) || (acquired<0 && chain!=IntPtr.Zero))
       throw new Exception("Invalid owned-swapchain extension result: "+acquired);
     return api;
   } finally {
     if(handle!=IntPtr.Zero) Function<Destroy>(dll,"mpv_terminate_destroy")(handle);
     FreeLibrary(dll);
   }
 }
}
'@
}
$api = [IimaMpvProbe]::Check($dll)
Write-Host "libmpv AMD64 / C ABI / initialization / option acceptance passed; API $($api -shr 16).$($api -band 0xffff)."
Write-Host 'Not a video-output, audio, swapchain, hardware-decoding or HDR test.'
