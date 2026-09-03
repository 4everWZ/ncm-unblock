# ncm-unblock-297

A lightweight native Win32 launcher that runs an upstream UnblockNeteaseMusic (UNM) standalone executable only while NetEase Cloud Music 2.9.7.199711 is in use.

The production direction is a portable launcher plus a loopback UNM sidecar. DLL proxying, CEF/V8 hooks, and in-process matching are not part of the production runtime; that work is preserved on the `research/native-injection` branch.

The current product contract is in [the canonical specification](docs/specs/ncm-unblock-297.md), and resumable work is tracked in [the current work plan](docs/plans/ncm-unblock-297.md).

Material under `tmp/` is intentionally ignored and is not an implementation contract.

## Build and test

From PowerShell on Windows with Visual Studio 2022 Build Tools, CMake, and Ninja installed:

```powershell
./tools/build.ps1
```

The script discovers the Visual Studio installation, activates its Win32 toolchain, configures an out-of-source build, compiles, and runs focused tests. Use `-Configuration Release` for a release build.

## Run

1. Build Release, then copy `ncm-unblock.ini.example` beside `build/win32-release/src/launcher/ncm-unblock.exe` as `ncm-unblock.ini`.
2. Set `[ncm] path` to the signed x86 NCM 2.9.7.199711 executable and `[unm] path` to the upstream standalone UNM executable. Relative paths are resolved from the launcher directory.
3. In NCM, set **Settings → Tools → HTTP proxy → Custom proxy** to `127.0.0.1` and the configured `http_port` once.
4. Exit NCM completely from its tray icon, then run `ncm-unblock.exe`.

The launcher starts UNM hidden, waits for both fixed loopback listeners and a valid PAC response, then starts NCM. Closing the NCM window to the tray keeps UNM alive; choosing NCM's tray **Exit** stops UNM and the launcher. With logging enabled, diagnostics are appended under `logs/` beside the launcher. Leave `sources` empty unless intentionally overriding the pinned UNM release defaults.

## Package

Create the verified portable ZIP with:

```powershell
./tools/package.ps1
```

The archive is written under `out/` and intentionally does not redistribute UNM. Follow `core/README.txt` in the package to download the official v0.28.0 Windows x64 standalone executable. No separate Node installation is required because that upstream executable contains its own runtime.

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
