# UnblockLite Specification

## Intent

- **Product:** A BetterNCM v2 plugin for NetEase Cloud Music 2.10.12 plus `unm-host.exe`, a native supervisor for one UnblockNeteaseMusic (UNM) matcher process (bundled patched JS via system Node, or user-placed official standalone).
- **MVP user experience:** Install BetterNCM and Node 18+, drop in `UnblockLite.plugin` (a zip BetterNCM extracts under `plugins_runtime`), enable the plugin. The plugin sets NCM's custom HTTP proxy to `127.0.0.1:3412` when needed. Closing NCM to the tray keeps UNM; a real NCM exit leaves no host or UNM process.
- **In scope:** Plugin UI/config/proxy/start; host mutex, NCM-main attach, Job Object UNM ownership, PAC readiness, session-end cleanup; packaged MITM leaf + private CA with Current User Root trust so NCM's Chromium stack accepts UNM HTTPS interception; thin privilege-level fork of UNM `precompiled/app.js` (LGPL) shipped as `core/unm-app.js`; x64 CMake build; `UnblockLite.plugin` package without the official pkg binary.
- **Out of scope:** Downloaders, version pickers, rich logging, shortcut setup, launching NCM, DLL/CEF/WinMM injection, system proxy, machine-wide certificate stores, `localdata` edits, Windows services, unbounded UNM restart, a RevivedUnblockInstaller fork, provider reimplementation, embedding the official pkg `.exe`.

UNM remains the matcher. The host does not reimplement providers. Injection research stays on `research/native-injection`. The 2.9.7 launcher stays on `ncm-2.9.7`.

## Runtime contract

### Components

- Plugin: BetterNCM `UnblockLite.plugin` zip with root-level `manifest.json` + `main.js` (`ncm3-compatible`), embedded `native/unm-host.exe`, `certs/` (`ca.crt`, `server.crt`, `server.key`), and `core/unm-app.js` (+ `core/NOTICE.md`). States are Disabled, Starting, Running. Config: Enabled, Sources, HTTP port 3412, Start with NCM. Host/UNM paths resolve from the extracted `pluginPath`, then `<data>/UnblockLite/`, then data root. Matcher resolve order prefers `core/unm-app.js`, then official exe names.
- Host: `unm-host.exe`. Single-instance mutex `Local\UnblockNeteaseMusic-Lite`. Optional `--stop` through `Local\UnblockNeteaseMusic-Lite-Stop`.
- UNM matcher:
  - **Default:** bundled patched `core/unm-app.js` (UnblockNeteaseMusic/server v0.28.0 `precompiled/app.js` + privilege inject). Host runs `node <app.js> …` when `--unm` ends with `.js` (Node from PATH, `NODE_BINARY`, or common nvm-windows path).
  - **Fallback:** user-placed official v0.28.0 Windows x64 standalone (preferred `UnblockNeteaseMusic.exe` under BetterNCM data `UnblockLite/`). Not redistributed.
- NCM stays outside the UNM job.

### Host sequence

1. Parse `--ncm`, `--unm`, `--http`, `--https`, optional `--sources`. `--stop` only signals the running host.
2. Take the single-instance mutex. A second start exits success without launching another UNM.
3. Attach to NCM main: `cloudmusic.exe` whose image path is `--ncm` and whose command line does not contain `--type=`. Prefer the oldest matching process. Do not treat window-close as exit.
4. Resolve MITM material from `<plugin>/certs` (preferred), then `<UNM dir>/certs`, then the UNM directory. Install `ca.crt` into the **Current User** Root store when its thumbprint is not already present. Fail the session if material or trust setup is missing.
5. Resolve launch target: if `--unm` is `.js`, require `node.exe` and set executable=node with the script as the first argument; otherwise run the exe path as today. Reserve the fixed loopback pair, start UNM suspended with `SIGN_CERT` / `SIGN_KEY` pointing at the packaged leaf and `ENABLE_FLAC=true` / `FOLLOW_SOURCE_ORDER=true` / `ENABLE_LOCAL_VIP=svip` overlaid on the same environment, assign `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, resume, wait until both listeners belong to that job and `/proxy.pac` returns a complete HTTP 200.
6. Wait on the NCM main process handle (and the stop event). One-second tree checks detect unexpected UNM death.
7. `TerminateJobObject`, wait until the tree is empty, release the mutex, exit.

Host crash or kill still reclaims UNM because the job is kill-on-close. Pre-existing or merely same-named processes are never terminated. The private CA is not removed on exit (trust remains for the next session).

### Plugin sequence

1. `onLoad`: if Enabled and Start with NCM, start the host when PAC is not already ready.
2. Confirm `http://127.0.0.1:<port>/proxy.pac`.
3. Read NCM proxy through `app.getLocalConfig` `["Proxy", ""]`. Write `Type=http`, `Host=127.0.0.1`, `Port=<http>` only when it does not already match. Do not restart NCM when the value is already correct.
4. `onConfig` exposes the four settings. Disable signals `--stop`.

V1 does not download UNM, Node, or pick versions.

### Ports and sources

HTTP 3412 / HTTPS 3413 by default. Occupied configured ports fail; the host does not silently pick others. Empty or omitted sources still pass `-o bodian migu kugou` (product default: bodian first under FOLLOW_SOURCE_ORDER so VIP matches do not wait on migu's ~10s miss timeouts; prefers ~320k-capable matchers and UNM's bodian path over broken anonymous kuwo VIP promo clips / qq-without-cookie M500); an explicit Sources list overrides that order. Sources are UNM match-order ids only (for example `bodian,migu,kugou`); host addresses such as `127.0.0.1` are rejected and ignored. The host always overlays `ENABLE_FLAC=true`, `FOLLOW_SOURCE_ORDER=true`, and `ENABLE_LOCAL_VIP=svip` when starting UNM (no plugin toggles), so UNM honors `-o` order instead of racing sources and patches local red+/SVIP membership on vip/info when the NCM session is logged in. The bundled JS privilege fork (when FLAC is on) raises `plLevel`/`dlLevel`/`flLevel` and allied max-level fields to `lossless` and floors `playMaxbr`/`downloadMaxbr` to `999000` when present and lower — official inject only did `none`→`exhigh` / `0`→`320000`. The player quality chip can still follow NCM **设置 → 播放 → 在线音质**; UnblockLite does not rewrite that preference. The plugin launches the host through PowerShell `Start-Process` with a single pre-quoted `-ArgumentList` string, delivered via `-EncodedCommand` so nested quotes survive `betterncm.app.exec`. Config UI exposes Save & apply and Disable.

### HTTPS MITM trust

Official UNM v0.28.0 embeds a leaf that expires and is signed by an unpublished CA. UnblockLite ships its own short-lived leaf for `music.163.com` / `*.music.163.com` and a product CA (`UnblockLite Root CA`). The host passes the leaf to UNM through `SIGN_CERT` / `SIGN_KEY` and installs only that CA into **Current User\Root**. This is required for Chromium/NCM to accept CONNECT interception. No system proxy and no Local Machine store writes.

### LGPL JS matcher

`core/unm-app.js` is derived from UnblockNeteaseMusic/server v0.28.0 `precompiled/app.js` (LGPL-3.0). Corresponding source and patch notes live under `third_party/unm/` (`app.js.upstream`, `app.js`, `NOTICE.md`). Rebuild helper: `tools/vendor-unm-app.ps1`.

## Acceptance

| Observable requirement | Verification method |
|---|---|
| Host starts UNM only after attaching to this install's NCM main | Focused attach tests plus a live NCM run |
| CEF `--type=` processes are not treated as session end | `ncm_watch` fixture with `--type=renderer` |
| PAC readiness requires job-owned listeners and a complete `/proxy.pac` | Existing sidecar tests |
| Host overlays `SIGN_CERT`/`SIGN_KEY` and trusts the packaged CA in Current User Root | `mitm_certs` tests plus managed-process env overlay test |
| Host always passes `-o` (default `bodian migu kugou` when Sources empty) and overlays `ENABLE_FLAC=true` + `FOLLOW_SOURCE_ORDER=true` + `ENABLE_LOCAL_VIP=svip` | Host source inspection plus live UNM argv/env spot-check |
| `--unm` ending in `.js` launches via `node` with script as first arg; `.exe` path unchanged | Host log `unm mode=js` / live process command line |
| Bundled JS privilege inject yields `plLevel=lossless` (and allied levels) under FLAC while URL stays FLAC `br=999000` | Live privilege + player/url probes through proxy 3412 |
| HTTPS through the proxy validates without ignoring TLS errors once the CA is trusted | Live curl without `-k` and NCM session without `net_error -202` |
| Tray hide keeps host+UNM; tray Exit leaves zero host/UNM/port residue | Exact-client lifecycle run |
| Host kill reclaims the UNM tree and does not kill NCM | Job-close and owner-kill tests |
| Plugin does not `taskkill` by image name | Source inspection |
| Package ships `UnblockLite.plugin` (zip with `certs/`, `core/unm-app.js`, NOTICE) + README, not a loose folder or official pkg exe | `tools/package.ps1` manifest |

## Deferred

- UNM/Node downloader and version picker.
- Bounded single UNM restart.
- Conventional installer.
- Automatic CA rotation / renewal UI.
- Uninstall path that removes the Current User Root entry.
- Guaranteeing the quality chip when the user preference is explicitly「标准」.
