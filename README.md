# UnblockLite

A BetterNCM plugin plus a tiny native supervisor for NetEase Cloud Music 2.10.12. The plugin owns UI, config, and NCM's loopback proxy setting. `unm-host.exe` owns UnblockNeteaseMusic (UNM): one instance, a private Job Object, readiness, HTTPS MITM material, and exit.

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
2. Drop `UnblockLite.plugin` from a release (or `out/unblock-lite-*-x64/`) into BetterNCM's `plugins` directory. BetterNCM only loads `*.plugin` zip archives; a loose folder is ignored.
3. Download official UNM [v0.28.0](https://github.com/UnblockNeteaseMusic/server/releases/tag/v0.28.0) Windows x64 standalone, rename it to `UnblockNeteaseMusic.exe`, and place it at `<BetterNCM data>/UnblockLite/UnblockNeteaseMusic.exe` (typically `C:\betterncm\UnblockLite\`). The plugin also accepts `unblockneteasemusic-win-x64.exe` and a `core/` subfolder next to the extracted runtime.
4. Restart NCM so BetterNCM extracts the plugin to `plugins_runtime\UnblockLite`. Enable it. If **Start with NCM** is on, it starts the host and writes NCM's custom HTTP proxy to `127.0.0.1:3412` when that value is not already set.

The plugin ships a private CA (`certs/ca.crt`) and a leaf for `*.music.163.com`. On first successful host start the CA is installed into the **Current User** trusted Root store so NCM can accept UNM's HTTPS interception. Windows may show a security prompt the first time that store write happens. The CA private key is not shipped.

V1 does not download UNM. Leave **Sources** empty unless you override UNM's match-order list with ids such as `kugou,kuwo,migu` — never put `127.0.0.1` there (that is the proxy host, set automatically).

## Package

```powershell
./tools/package.ps1
```

The ZIP under `out/` contains `UnblockLite.plugin` (manifest, `main.js`, embedded `native/unm-host.exe`, `certs/`, placement notice) and `README.md`. It does not redistribute UNM.
