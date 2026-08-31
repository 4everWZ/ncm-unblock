# ncm-unblock-297

A native-first Windows integration for running an upstream UnblockNeteaseMusic core only while NetEase Cloud Music 2.9.7 is in use.

The project is in the investigation and bootstrap stage. The current product contract is in [the canonical specification](docs/specs/ncm-unblock-297.md), and resumable work is tracked in [the current work plan](docs/plans/ncm-unblock-297.md).

Material under `tmp/` is intentionally ignored and is not an implementation contract.

## Build and test

From PowerShell on Windows with Visual Studio 2022 Build Tools, CMake, and Ninja installed:

```powershell
./tools/build.ps1
```

The script discovers the Visual Studio installation, activates its Win32 toolchain, configures an out-of-source build, compiles, and runs focused tests. Use `-Configuration Release` for a release build.

Inspect the local NCM executable without changing it:

```powershell
./build/win32-debug/src/runtime_probe/ncm_runtime_probe.exe `
  'C:\Path\To\CloudMusic\cloudmusic.exe'
```
