# NCM 2.9.7 Runtime Investigation

This report records reproducible investigation evidence. It is not the product contract; behavioral requirements remain in the [canonical specification](../specs/ncm-unblock-297.md).

## Evidence boundary

- **Target observed:** NetEase Cloud Music `2.9.7.199711` at `D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe` on Windows.
- **Snapshot date:** 2026-09-01.
- **Methods:** The repository's Win32 `ncm_runtime_probe` and bounded loopback proxy observer, Visual Studio `dumpbin`, Windows Authenticode verification, process/module snapshots, process command lines, IPv4 TCP ownership inspection, local client-state inspection, and upstream primary sources.
- **Controlled scope:** No installed target files, NCM proxy settings, system proxy values, or certificates were changed. Controlled NCM instances were launched and, after normal close only minimized to the tray, their recorded experiment processes were terminated under explicit authorization. Private `localdata` was snapshotted outside the repository and fully restored. No payload content or credentials were captured or forwarded.
- **Not established:** Playback-request routing, HTTP/HTTPS proxy behavior, certificate requirements, UNM compatibility, stable loader behavior, or performance.

## Static image facts

| Property | Observation | Reproduction |
|---|---|---|
| Product file version | `2.9.7.199711` | `ncm_runtime_probe <path> --no-processes` |
| Architecture | x86, PE32, machine `0x014c` | Runtime probe and `dumpbin /headers` |
| Publisher signature | Valid Authenticode signature for NetEase | Runtime probe uses cache-only `WinVerifyTrust`; independently observed with `Get-AuthenticodeSignature` |
| Direct imports | `advapi32.dll`, `kernel32.dll`, `shell32.dll`, `shlwapi.dll`, `user32.dll`, `winmm.dll` | Runtime probe and `dumpbin /imports` |
| Delay imports | `dbghelp.dll` | Runtime probe and `dumpbin /imports` |

The executable does not directly import WinHTTP, WinINet, libcurl, or CEF. Those stacks enter through dynamically loaded or dependent modules.

## Process and network-stack facts

A live snapshot contained:

- one root `cloudmusic.exe` browser/main process;
- one `cloudmusic.exe --type=gpu-process` child;
- one `cloudmusic.exe --type=renderer` child; and
- one `cloudmusic_reporter.exe` child of the root process.

The command lines identify an embedded `Chrome/35.0.1916.157` / NCM `2.9.7.199711` runtime. The root, GPU, and renderer processes all loaded `WINHTTP.dll`, `WININET.dll`, `libcurl.dll`, `libcef.dll`, and `netutils.dll` in the observed session. Only the root `cloudmusic.exe` owned observed IPv4 TCP sockets; GPU and renderer children owned none in the same snapshot.

This supports the narrow hypothesis that current client network activity is brokered by the root process. It does not identify which stack carries playback requests, because shared module presence is not call-path evidence and the snapshot was not synchronized to a play action.

## Loader hypotheses

- `winmm.dll` and `shlwapi.dll` are direct imports and therefore candidates for controlled DLL search/load-order investigation.
- On this host, the 32-bit loader's KnownDLL registry includes `SHLWAPI.dll` but not `WINMM.dll`. The exact root imports `timeGetTime`; `cloudmusic.dll` imports `PlaySoundW`, timer-period functions, and wave-output functions from WinMM. This removes Shlwapi from the local same-directory experiment and makes WinMM the narrower candidate, but does not prove that NCM accepts an application-directory WinMM or that a forwarding proxy is safe.
- A direct import is not evidence that a same-directory proxy is safe. Export completeness, KnownDLL behavior, load order, signature/integrity checks, child-process effects, and clean forwarding remain untested.
- `msimg32.dll` and other loaders mentioned by reference projects are not imports of the root executable and are not candidates without evidence from another early-loaded module.
- No hook library is justified by current evidence.

## Client-local proxy checkpoint

Read-only inspection found no CloudMusic proxy values under the current user's NetEase registry keys or in the readable server-provided JSON configuration files. Installed `cloudmusic.dll` strings associate `%LOCALAPPDATA%\Netease\CloudMusic\localdata` with `AppConfig::SaveConfigAsync` and `Config.Proxy` fields `Type`, `Host`, `Port`, `UserName`, and `Password`. The file uses an opaque private encoding and was not decoded or rewritten; direct byte editing is not a supported experiment path.

The running client had no explicit Chromium proxy command-line switch. Together with the client's `UseIE` and WinHTTP/WinINet integration strings and a currently enabled user-level Internet proxy (whose endpoint was intentionally not recorded), this strongly indicates that the observed session uses the IE/system-proxy mode. It does not prove the persisted `Type` value. A reversible A/B requires all NCM processes to exit normally, an out-of-repository private snapshot of `localdata` including its metadata/ACL, a proxy change through the NCM settings UI, a wait for asynchronous persistence, and restoration through the same UI. Snapshot replacement while NCM is stopped is recovery, not the normal write path. System proxy values remain unchanged in this experiment.

### Static proxy-construction checkpoint

Read-only ASCII extraction from the exact 2.9.7 `cloudmusic.dll` places `Proxy`, `Type`, `Host`, `Port`, `UserName`, and `Password` together with `app_config.cpp` and `orpheus::AppConfig::SaveConfigAsync`. A separate `client\client_app.cpp` string cluster contains `Proxy Setting: UseIE`, `Proxy Setting:`, `--proxy-server`, and `--no-proxy-server`. Version-specific x86 disassembly around the latter references also compares the configured scheme against `socks`, `socks4`, `socks5`, `https`, `http`, and `ie` before choosing the CEF proxy switch.

The DLL exports its misspelled application entry `CloudMuiscMain`, but no named AppConfig, save, or proxy-setting function. `SaveConfigAsync` is an internal method that requires a valid object graph and asynchronously queues work, so its linked address is not a callable external contract. The embedded page/native command table separately contains `showProxySetting`, `setLocalConfig`, `getLocalConfig`, and `testProxy` handlers. Focused disassembly of `setLocalConfig` shows a minimum three-argument check, requires each of the first three values to have the same string type code, extracts those strings, and calls a generic internal configuration setter through an initialized singleton/object graph. Neither the three strings' semantic roles nor a proxy-object persistence edge is established. The handler is therefore not a stable external proxy setter. `objdump -p` also exposes WinHTTP/IE proxy imports.

Together these observations support the narrow hypothesis that NCM converts its persisted client-local proxy object into CEF startup switches internally; no stable external setter or complete call edge from the native command bridge through AppConfig persistence to the switch-building function has been established. The evidence explains why supplying a root-process Chromium flag is not equivalent to changing the supported client setting. It does not establish that playback uses CEF or justify patching version-specific linked addresses. The internal-call route stops here: no RVA injection, fabricated AppConfig object, or invocation of the private bridge is justified. The next bounded experiment is the isolated WinMM loader candidate described above.

The repository's `ncm_proxy_observer` provides the safe endpoint for the next controlled experiment. It binds exclusively to `127.0.0.1`, stops after a bounded event count or idle time, retains only method/target-form/scheme/destination-class/port/completeness, and returns 502 without forwarding. A synthetic socket check produced one absolute-form HTTP event and one HTTPS `CONNECT` event while test paths, query values, and authorization data remained absent from output. This validates the observer, not NCM behavior.

### Process-local proxy experiment

A controlled launch started the exact target from its installation directory with `--proxy-server=http://127.0.0.1:<ephemeral-port>`. The root process command line contained that exact argument, but the observer received zero requests during the 15-second startup window. In the same window, the root process established connections on ports 80 and 443 through other paths. This falsifies the narrow claim that the Chromium command-line flag provides NCM-wide startup routing; it does not prove that every CEF request ignores the flag or establish playback behavior.

The normal main-window close request activated NCM's close-to-tray behavior rather than exiting the process. After explicit authorization, the recorded experiment root/process tree was terminated without targeting unrelated names or paths. The user-level Internet proxy remained exactly unchanged. `localdata` content, creation/write timestamps, attributes, and owner/group/DACL were restored against the private manifest and backup, then every recovery, observer, sibling, and displaced file was removed. No certificate state was changed.

### Client UI automation checkpoint

A public [2.9.7 procedure](https://jingyan.baidu.com/article/ac6a9a5e155c506a653eacbe.html) and a separate [restart-prompt procedure](https://jingyan.baidu.com/article/7908e85c70a95bee491ad270.html) corroborate the settings path `settings → tools → HTTP proxy → custom proxy`, including server/port fields and a restart prompt. The embedded settings controls were not exposed through Windows UI Automation. Attempts gated to the exact signed version, executable path, foreground window, and expected layout did not cause `localdata` to persist a proxy change; each run was therefore recovered without claiming that the client-local proxy was configured. Fragile coordinate automation is not a reproducible compatibility method and has been stopped.

The experiment harness prints its private recovery directory before launching NCM and warns that a second interrupt must not interrupt recovery. If an external host termination leaves that directory behind, `tools/restore-ncm-proxy-experiment.ps1` validates the exact temporary-directory and bundle identity, private owner/DACL, non-reparse files, current-user target, bundle-owned sibling paths, backup length and SHA-256 integrity record, and stopped-process boundary before atomically restoring bytes, timestamps, attributes, and owner/group/DACL. A synthetic-profile recovery test passed; this validates recovery mechanics, not NCM routing.

## Upstream v0.28.0 checkpoint

The official [v0.28.0 release](https://github.com/UnblockNeteaseMusic/server/releases/tag/v0.28.0) provides Windows x64 and ARM64 standalone executables, but no x86 executable. The sidecar therefore follows OS architecture rather than the x86 client architecture. Its official [build workflow](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/.github/workflows/build-binaries.yml) uses `pkg` Node 18 targets, so the standalone contains its runtime and does not require a separately installed Node toolchain.

The official [CLI and startup code](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/src/app.js) uses separate HTTP/HTTPS ports, defaults to `8080:8081`, and does not make loopback binding the safe default. A launcher candidate must explicitly pass `-a 127.0.0.1`, an HTTP/HTTPS port pair, and `-s`. There is no dedicated health endpoint: a live child, both listening sockets, and the [HTTP `/proxy.pac` response](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/src/server.js) establish initialization only, not provider or playback health.

HTTPS is currently blocked at the trust-design level. The upstream [CONNECT path](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/src/hook.js#L515-L533) redirects selected tunnels to its HTTPS listener. The bundled [server certificate](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/server.crt) expired on 2026-07-24, before this investigation, and its default private key is public. Although custom certificate paths are supported, the project has not accepted a CA/leaf generation, per-user trust, renewal, removal, or recovery design. No certificate was installed.

The project declares `LGPL-3.0-only` in its [package metadata](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/package.json) and distributes [GPLv3](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/COPYING) and [LGPLv3](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/COPYING.LESSER) texts. Before redistribution, this repository still needs a corresponding-source route plus a license/notice audit for the embedded Node runtime and bundled dependencies. No upstream executable was downloaded, executed, or approved for redistribution in this checkpoint.

## Remaining M0 experiments

1. Identify how 2.9.7 persists its client-local proxy state and prove an exact read/write/rollback cycle without copying private values into the repository.
2. Route a controlled, non-sensitive request through the validated loopback observer to distinguish whether 2.9.7 honors its HTTP proxy for HTTPS destinations and whether it uses CONNECT.
3. Repeat connection ownership snapshots around search, normal-track play, unavailable-track play, and track changes; use stack-specific tracing only if ownership remains ambiguous.
4. Inspect loader candidates for search order and complete export forwarding in an isolated copy, not the installed client directory.
5. Pin an upstream UNM Windows artifact only after verifying authenticity, target compatibility, certificate lifecycle, corresponding source, and bundled dependency notices in addition to its already-audited source, version, architecture, CLI, and initialization behavior.

The launcher compatibility go/no-go remains open until the NCM-to-loopback-UNM matrix covers search, normal tracks, unavailable tracks, play, and track changes.
