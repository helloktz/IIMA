IIMA playback development build

Target: Windows 11 x64.
Extract the entire directory and run IIMA.exe from it. Use File > Open File to play local media.
See docs/VALIDATION.md in the source repository for the hardware and playback cases exercised.

The unsigned ZIP includes the Windows App SDK, VC++ runtime, and libmpv DLLs.
Run directly after extraction. The application saves settings and playback history under %LOCALAPPDATA%\IIMA.
Close all player windows before removing the extracted application directory.

This package is for development verification. See DEPENDENCIES.md, libmpv-manifest.json, and licenses/ for provenance and notices.
