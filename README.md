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
2. Install **Node.js 18+** on PATH (or nvm-windows). The default matcher is the bundled patched UNM JS under `core/unm-app.js`.
3. Drop `UnblockLite.plugin` from a release (or `out/unblock-lite-*-x64/`) into BetterNCM's `plugins` directory. BetterNCM only loads `*.plugin` zip archives; a loose folder is ignored.
4. Optional fallback: download official UNM [v0.28.0](https://github.com/UnblockNeteaseMusic/server/releases/tag/v0.28.0) Windows x64 standalone, rename it to `UnblockNeteaseMusic.exe`, and place it at `<BetterNCM data>/UnblockLite/UnblockNeteaseMusic.exe` if you are not using the bundled JS path.
5. Restart NCM so BetterNCM extracts the plugin to `plugins_runtime\UnblockLite`. Enable it. If **Start with NCM** is on, it starts the host and writes NCM's custom HTTP proxy to `127.0.0.1:3412` when that value is not already set.

The plugin ships a private CA (`certs/ca.crt`) and a leaf for `*.music.163.com`. On first successful host start the CA is installed into the **Current User** trusted Root store so NCM can accept UNM's HTTPS interception. Windows may show a security prompt the first time that store write happens. The CA private key is not shipped.

V1 does not download UNM or Node. Leave **Sources** empty for the product default match order `bodian,migu,kugou` (~320k-oriented; FLAC attempted when a source supports it; `bodian` first so FOLLOW_SOURCE_ORDER does not wait on migu timeouts; `kuwo` is omitted because VIP tracks can resolve to a promo clip). The host overlays `ENABLE_FLAC=true`, `FOLLOW_SOURCE_ORDER=true`, and `ENABLE_LOCAL_VIP=svip`. The bundled JS floors privilege levels (`plLevel`/`dlLevel`/`flLevel` and allied max-level fields on privilege-shaped objects only) to `lossless` when FLAC is enabled, and fills matched `player/url` `level`/`encodeType` from the stream (FLAC → `lossless`/`flac`) so the player quality chip can leave「标准」when NCM's online-quality preference allows it. Max-level fields are not written onto unrelated API roots (for example `vip/info`), which previously could make the username menu full-refresh before opening. LOCAL_VIP membership `expireTime` is day-stable (not per-response `now+1y`) so opening the username dropdown does not keep force-refreshing the shell from drifting VIP payloads. Override Sources with other UNM match-order ids only when you need a different order — never put `127.0.0.1` there (that is the proxy host, set automatically).

The shipped `core/unm-app.js` is LGPL-3.0 material from UnblockNeteaseMusic/server v0.28.0 with a documented privilege/URL inject patch; see `core/NOTICE.md` and `third_party/unm/`. The official pkg binary is **not** redistributed.

## Package

```powershell
./tools/package.ps1
```

The ZIP under `out/` contains `UnblockLite.plugin` (manifest, `main.js`, embedded `native/unm-host.exe`, `certs/`, `core/unm-app.js` + NOTICE, placement notice) and `README.md`. It does not redistribute the official UNM pkg executable.
