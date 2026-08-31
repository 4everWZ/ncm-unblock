# NCM 2.9.7 Runtime Investigation

This report records reproducible investigation evidence. It is not the product contract; behavioral requirements remain in the [canonical specification](../specs/ncm-unblock-297.md).

## Evidence boundary

- **Target observed:** NetEase Cloud Music `2.9.7.199711` at `D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe` on Windows.
- **Snapshot date:** 2026-09-01.
- **Methods:** The repository's Win32 `ncm_runtime_probe`, Visual Studio `dumpbin`, Windows Authenticode verification, process/module snapshots, process command lines, and IPv4 TCP ownership inspection.
- **Non-invasive scope:** No target files, proxy settings, certificates, processes, or network traffic were changed. No payload content or credentials were captured.
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

## Upstream checkpoint

The official [UnblockNeteaseMusic/server releases](https://github.com/UnblockNeteaseMusic/server/releases) page listed v0.28.0 and standalone assets when checked. Its release notes discuss compatibility with newer NCM clients, but that does not prove behavior for this 2.9.7 installation. No upstream executable was downloaded, executed, or approved for redistribution during this investigation.

## Remaining M0 experiments

1. Record the current NCM client-local proxy state and define an exact rollback without copying private values into the repository.
2. Route a controlled, non-sensitive request through a loopback observer to distinguish whether 2.9.7 honors its HTTP proxy for HTTPS destinations and whether it uses CONNECT.
3. Repeat connection ownership snapshots around search, normal-track play, unavailable-track play, and track changes; use stack-specific tracing only if ownership remains ambiguous.
4. Inspect loader candidates for search order and complete export forwarding in an isolated copy, not the installed client directory.
5. Pin an upstream UNM Windows artifact only after verifying its source, version, license, architecture, CLI, readiness behavior, and redistribution terms.

The launcher compatibility go/no-go remains open until the NCM-to-loopback-UNM matrix covers search, normal tracks, unavailable tracks, play, and track changes.
