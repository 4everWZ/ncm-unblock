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

Run the bounded loopback proxy observer for controlled experiments:

```powershell
./build/win32-debug/src/proxy_observer/ncm_proxy_observer.exe `
  --port 0 --max-events 20 --idle-timeout-ms 30000
```

The observer binds exclusively to `127.0.0.1`, prints only classified request metadata, never logs raw hosts, paths, query strings, headers, or bodies, and rejects every request with HTTP 502. It is an investigation tool, not a forwarding proxy.

Reproduce the isolated pre-entry WinMM load experiment:

```powershell
./tools/probe-ncm-winmm-load.ps1
```

The run launches only isolated signed copies of the exact target, never the installed client. Expected evidence on a host that accepts an application-directory WinMM is a control load from `C:\Windows\SysWOW64\winmm.dll`, a treatment load from the private temporary experiment directory, `debug-events: 20` for both, no remaining `cloudmusic.exe` process, and no leftover `ncm-unblock-297-winmm-probe-*` directory.

## WinMM proxy surface

The pinned WinMM export surface lives in [src/winmm_proxy/winmm.exports](src/winmm_proxy/winmm.exports). It generates the proxy's export table and thunks alongside the synthetic backend and negative-control fixtures the tests exercise, so the parity contract has one source. Building the proxy does not deploy it; nothing writes into an NCM installation.

Check the release proxy against this host's real WinMM:

```powershell
./tools/probe-winmm-system-backend.ps1
```

Start an isolated copy of the exact client with the proxy beside it:

```powershell
./tools/probe-ncm-winmm-proxy.ps1
```

The installed tree is copied to a private directory and never modified, the run refuses to start while any client process exists, and only processes whose image lives under that private directory are ever reclaimed. Pass `-ProxyPath` and `-FixtureBackend` to run the positive control, which points the test fixture at a backend that does not exist and expects the client to stop with the proxy's `0xE0C40001`.

Attribute playback traffic to a network stack by staging the census build of the proxy instead:

```powershell
./tools/probe-ncm-winmm-proxy.ps1 `
  -ProxyPath ./build/win32-release/src/network_stack_census/census_proxy/winmm.dll `
  -CensusOutputDirectory $env:TEMP\ncm-census `
  -ObservationSeconds 600
```

The census proxy forwards the same pinned surface as the release proxy and only differs in the bootstrap body it schedules. Every isolated process writes a module-load timeline tagged with its process role, recording allowlisted module names and fixed classifications only — never paths, request targets, headers, or credentials. It routes nothing. The run holds for the whole window so an operator can sign in and play one normal track and one greyed-out track; the discriminator is a network stack that maps lazily in the root process just before the first `audio_render` load.
