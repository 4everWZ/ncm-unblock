# UnblockLite Specification

## Intent

- **Product:** A BetterNCM v2 plugin for NetEase Cloud Music 2.10.12 plus `unm-host.exe`, a native supervisor for one official UnblockNeteaseMusic (UNM) standalone process.
- **MVP user experience:** Install BetterNCM, drop in `UnblockLite.plugin` (a zip BetterNCM extracts under `plugins_runtime`), place the pinned UNM executable under the BetterNCM data directory, enable the plugin. The plugin sets NCM's custom HTTP proxy to `127.0.0.1:3412` when needed. Closing NCM to the tray keeps UNM; a real NCM exit leaves no host or UNM process.
- **In scope:** Plugin UI/config/proxy/start; host mutex, NCM-main attach, Job Object UNM ownership, PAC readiness, session-end cleanup; x64 CMake build; `UnblockLite.plugin` package without UNM.
- **Out of scope:** Downloaders, version pickers, rich logging, shortcut setup, launching NCM, DLL/CEF/WinMM injection, system proxy, certificate installation, `localdata` edits, Windows services, unbounded UNM restart, a RevivedUnblockInstaller fork.

UNM remains the matcher. The host does not reimplement providers. Injection research stays on `research/native-injection`. The 2.9.7 launcher stays on `ncm-2.9.7`.

## Runtime contract

### Components

- Plugin: BetterNCM `UnblockLite.plugin` zip with root-level `manifest.json` + `main.js` (`ncm3-compatible`), plus embedded `native/unm-host.exe`. States are Disabled, Starting, Running. Config: Enabled, Sources, HTTP port 3412, Start with NCM. Host/UNM paths resolve from the extracted `pluginPath`, then `<data>/UnblockLite/`, then data root.
- Host: `unm-host.exe`. Single-instance mutex `Local\UnblockNeteaseMusic-Lite`. Optional `--stop` through `Local\UnblockNeteaseMusic-Lite-Stop`.
- UNM: independently placed official v0.28.0 Windows x64 standalone (preferred `UnblockNeteaseMusic.exe` under BetterNCM data `UnblockLite/`). Not redistributed.
- NCM stays outside the UNM job.

### Host sequence

1. Parse `--ncm`, `--unm`, `--http`, `--https`, optional `--sources`. `--stop` only signals the running host.
2. Take the single-instance mutex. A second start exits success without launching another UNM.
3. Attach to NCM main: `cloudmusic.exe` whose image path is `--ncm` and whose command line does not contain `--type=`. Prefer the oldest matching process. Do not treat window-close as exit.
4. Reserve the fixed loopback pair, start UNM suspended, assign `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, resume, wait until both listeners belong to that job and `/proxy.pac` returns a complete HTTP 200.
5. Wait on the NCM main process handle (and the stop event). One-second tree checks detect unexpected UNM death.
6. `TerminateJobObject`, wait until the tree is empty, release the mutex, exit.

Host crash or kill still reclaims UNM because the job is kill-on-close. Pre-existing or merely same-named processes are never terminated.

### Plugin sequence

1. `onLoad`: if Enabled and Start with NCM, start the host when PAC is not already ready.
2. Confirm `http://127.0.0.1:<port>/proxy.pac`.
3. Read NCM proxy through `app.getLocalConfig` `["Proxy", ""]`. Write `Type=http`, `Host=127.0.0.1`, `Port=<http>` only when it does not already match. Do not restart NCM when the value is already correct.
4. `onConfig` exposes the four settings. Disable signals `--stop`.

V1 does not download UNM or pick versions.

### Ports and sources

HTTP 3412 / HTTPS 3413 by default. Occupied configured ports fail; the host does not silently pick others. Empty sources omit `-o` so the pinned UNM defaults apply. Sources are UNM match-order ids only (for example `kugou,kuwo,migu`); host addresses such as `127.0.0.1` are rejected and ignored. The plugin launches the host through PowerShell `Start-Process` with a single pre-quoted `-ArgumentList` string so paths under `Program Files` survive `betterncm.app.exec`.

## Acceptance

| Observable requirement | Verification method |
|---|---|
| Host starts UNM only after attaching to this install's NCM main | Focused attach tests plus a live NCM run |
| CEF `--type=` processes are not treated as session end | `ncm_watch` fixture with `--type=renderer` |
| PAC readiness requires job-owned listeners and a complete `/proxy.pac` | Existing sidecar tests |
| Tray hide keeps host+UNM; tray Exit leaves zero host/UNM/port residue | Exact-client lifecycle run |
| Host kill reclaims the UNM tree and does not kill NCM | Job-close and owner-kill tests |
| Plugin does not `taskkill` by image name | Source inspection |
| Package ships `UnblockLite.plugin` (zip) + README, not a loose folder or UNM | `tools/package.ps1` manifest |

## Deferred

- UNM downloader and version picker.
- Bounded single UNM restart.
- Conventional installer.
- HTTPS certificate trust (upstream bundled leaf is not a product trust claim).
