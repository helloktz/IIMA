IIMA initialization build

Target: Windows 11 x64.
Extract the entire directory, then run IIMA.exe. Do not move the executable alone.
This build opens an empty native WinUI 3 window. It contains no media playback
implementation and does not yet bundle libmpv.

Windows App SDK and Release VC++ runtime DLLs are included app-locally.
The package is unsigned, requires no installer, and does not register file associations.
Close the window to exit. Remove the extracted directory to remove this scaffold.

This is a development verification artifact, not a public product release.
Project-wide licensing and signing must be settled before public distribution.
Dependency versions and upstream license materials accompany this package.
