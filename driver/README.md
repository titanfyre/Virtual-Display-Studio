# Virtual-Display-Studio driver source

Evaluation / engineering-preview source. The existing code has no technical trial timer or commercial activation system. This package does not enforce a timed evaluation cutoff.

This is a UMDF 2 / IddCx indirect display driver. Its project produces a user-mode DLL with UMDF 2.25 and IddCx 1.4.

## Build

Prerequisites: Windows x64, 64-bit Windows PowerShell, Visual Studio 2022
with Desktop development with C++, x64 MSVC tools and matching x64
Spectre-mitigated libraries, plus Windows SDK and WDK **10.0.26100.0**
with Visual Studio integration. The wrapper selects the newest installed
toolset that has both the x64 compiler and x64 Spectre libraries.
MSVC **14.32.31326** was validated in the validation environment.

From the repository root:

```powershell
.\driver\Build.ps1
# To rebuild the same Release x64 configuration:
.\driver\Build.ps1 -Rebuild
```

[Build.ps1](Build.ps1) compiles the primary project(s) below with signing,
test signing and Inf2Cat disabled. Outputs and intermediate files go to
`obj/DriverBuild/` at the repository root, outside this source folder.
It does not install or load a driver or change Windows security or network
configuration.

Primary source project(s), relative to this folder:

- [Driver/TitanVirtualDisplay.vcxproj](Driver/TitanVirtualDisplay.vcxproj)

## Validation boundary

Successful compilation establishes that these sources and their
local dependencies build with the selected Windows toolchain. It does not
establish driver loading, runtime behavior, expiry or permission enforcement
on a running system, hardware compatibility, Microsoft signing, HLK results,
or production installation qualification. Build outputs are unsigned.

## Source licenses

[LICENSE.txt](LICENSE.txt) supplies separate evaluation-source terms only
for files identified as Titan evaluation source in
[LICENSE-MAP.txt](LICENSE-MAP.txt). It permits inspection, compilation,
internal evaluation and source-review contributions while retaining the
covered demo controls and separate production/OEM terms.

The file map also identifies complete retained MIT/MS-PL permissions.
Those permissions apply to the whole mapped files, including sample-derived
files with Titan changes; the evaluation restrictions do not narrow them.
The root application's/SDK's existing license and previously distributed
driver-binary terms remain unchanged. Full/commercial driver source and
private signing/activation material are outside this source package.

All files in `Driver/` retain MS-PL, including the mixed Microsoft/Titan
source, helpers and project descriptions. See the file map and retained text.
