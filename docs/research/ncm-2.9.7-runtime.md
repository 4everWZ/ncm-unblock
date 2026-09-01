# NCM 2.9.7 Runtime Investigation

This report records reproducible investigation evidence. It is not the product contract; behavioral requirements remain in the [canonical specification](../specs/ncm-unblock-297.md).

## Evidence boundary

- **Target observed:** NetEase Cloud Music `2.9.7.199711` at `D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe` on Windows.
- **Snapshot date:** 2026-09-01.
- **Methods:** The repository's Win32 `ncm_runtime_probe` and bounded loopback proxy observer, Visual Studio `dumpbin`, Windows Authenticode verification, process/module snapshots, process command lines, IPv4 TCP ownership inspection, local client-state inspection, and upstream primary sources.
- **Non-invasive scope:** No target files, NCM proxy settings, certificates, or NCM processes were changed. Only synthetic requests were sent to the repository's rejecting loopback observer; no payload content or credentials were captured or forwarded.
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
- A direct import is not evidence that a same-directory proxy is safe. Export completeness, KnownDLL behavior, load order, signature/integrity checks, child-process effects, and clean forwarding remain untested.
- `msimg32.dll` and other loaders mentioned by reference projects are not imports of the root executable and are not candidates without evidence from another early-loaded module.
- No hook library is justified by current evidence.

## Client-local proxy checkpoint

Read-only inspection found no CloudMusic proxy values under the current user's NetEase registry keys or in the readable server-provided JSON configuration files. Installed `cloudmusic.dll` strings associate `%LOCALAPPDATA%\Netease\CloudMusic\localdata` with `AppConfig::SaveConfigAsync` and `Config.Proxy` fields `Type`, `Host`, `Port`, `UserName`, and `Password`. The file uses an opaque private encoding and was not decoded or rewritten; direct byte editing is not a supported experiment path.

The running client had no explicit Chromium proxy command-line switch. Together with the client's `UseIE` and WinHTTP/WinINet integration strings and a currently enabled user-level Internet proxy (whose endpoint was intentionally not recorded), this strongly indicates that the observed session uses the IE/system-proxy mode. It does not prove the persisted `Type` value. A reversible A/B requires all NCM processes to exit normally, an out-of-repository private snapshot of `localdata` including its metadata/ACL, a proxy change through the NCM settings UI, a wait for asynchronous persistence, and restoration through the same UI. Snapshot replacement while NCM is stopped is recovery, not the normal write path. System proxy values remain unchanged in this experiment.

The repository's `ncm_proxy_observer` provides the safe endpoint for the next controlled experiment. It binds exclusively to `127.0.0.1`, stops after a bounded event count or idle time, retains only method/target-form/scheme/destination-class/port/completeness, and returns 502 without forwarding. A synthetic socket check produced one absolute-form HTTP event and one HTTPS `CONNECT` event while test paths, query values, and authorization data remained absent from output. This validates the observer, not NCM behavior.

### Process-local proxy experiment

A controlled launch started the exact target from its installation directory with `--proxy-server=http://127.0.0.1:<ephemeral-port>`. The root process command line contained that exact argument, but the observer received zero requests during the 15-second startup window. In the same window, the root process established connections on ports 80 and 443 through other paths. This falsifies the narrow claim that the Chromium command-line flag provides NCM-wide startup routing; it does not prove that every CEF request ignores the flag or establish playback behavior.

The normal main-window close request activated NCM's close-to-tray behavior rather than exiting the process. The harness did not force termination or overwrite live private state. The user-level Internet proxy remained exactly unchanged. A read-only comparison found `localdata` content, attributes, and owner/group/DACL unchanged, while creation/write timestamps differed after launch. A restricted temporary recovery bundle is retained until NCM exits through its tray menu; restoration and cleanup are therefore not yet claimed complete. No certificate state was changed.

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
