# UnblockLite

A BetterNCM plugin plus a tiny native supervisor for NetEase Cloud Music 2.10.12. The plugin owns UI, config, and NCM's loopback proxy setting. `unm-host.exe` owns UnblockNeteaseMusic (UNM): one instance, a private Job Object, readiness, and exit.

Closing NCM to the tray keeps UNM. Choosing NCM's tray **Exit** ends the NCM main process; the host then terminates its UNM job and exits. After a full NCM exit there is no host or UNM residue.

DLL proxying, CEF/V8 hooks, and in-process matching are not part of this product. That work stays on `research/native-injection`. The previous NCM 2.9.7 portable launcher is on `ncm-2.9.7`.

The contract is [docs/specs/ncm-unblock-lite.md](docs/specs/ncm-unblock-lite.md). Work is tracked in [docs/plans/ncm-unblock-lite.md](docs/plans/ncm-unblock-lite.md). `tmp/` is ignored and is not a contract.

## Build

```powershell
./tools/build.ps1
```

Requires Visual Studio 2022 Build Tools, CMake, and Ninja. The host is x64. Use `-Configuration Release` for a release build.

## First use

1. Install NCM 2.10.12 and BetterNCM v2.
2. Copy the `UnblockLite` folder from a release (or a staged `out/` package) into BetterNCM's plugins directory.
3. Download official UNM [v0.28.0](https://github.com/UnblockNeteaseMusic/server/releases/tag/v0.28.0) Windows x64 standalone, rename it to `UnblockNeteaseMusic.exe`, and place it at `UnblockLite/core/UnblockNeteaseMusic.exe`.
4. Enable the plugin. If **Start with NCM** is on, it starts the host and writes NCM's custom HTTP proxy to `127.0.0.1:3412` when that value is not already set.

V1 does not download UNM. Leave **Sources** empty unless you are overriding the pinned UNM defaults.

## Package

```powershell
./tools/package.ps1
```

The ZIP under `out/` contains the plugin, `unm-host.exe`, and a `core/` placement notice. It does not redistribute UNM.
