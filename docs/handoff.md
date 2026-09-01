# NCM Unblock 2.9.7 Handoff

## Objective

The final product is a thin native x86 `ncm_unblock.dll`, not a launcher-only deliverable. Prefer a proxy-DLL load path so users can start NCM normally; keep suspended injection only as a separately reviewed fallback. The DLL owns an on-demand upstream UNM sidecar, establishes NCM-local routing, and leaves no service, resident injector, Node development tree, or system-wide proxy mutation.

## Current verified state

- Native Win32 foundations are implemented and tested: exact private-Job process ownership, root-versus-tree completion, dual loopback leases, bounded port handoff/retry, complete listener-owner checks, and PAC-framed sidecar readiness.
- The root Chromium `--proxy-server` flag was falsified as an NCM-wide routing mechanism. Coordinate UI automation and private NCM config RVAs are also closed as unsupported implementation paths.
- The exact 2.9.7.199711 x86 root ordinarily imports `WINMM!timeGetTime`. A private debug-event harness proved that this host loads an application-directory WinMM before DLL initialization and executable entry. Control and treatment both resolved at event 20; the exact Jobs exited and temporary material was removed.
- The loader probe pins target/expected files, matches event `FILE_ID_INFO`, rejects reparse paths, terminates before continuing the matching load event, and retains pending debug-event ownership through teardown. Debug and Release each pass all six test targets. Independent Tier A review reports no remaining P0/P1.
- Offline static inspection found 48 distinct WinMM names across the root and conditional NCM/CEF closure, with no observed ordinal imports. The current SysWOW64 WinMM exports 193 consecutive ordinals (2–194), 192 names, one NONAME ordinal 2, and no PE forwarders. Current imports are covered, but dynamic lookup requires complete 193-entry parity.

## Blocking design facts

- A `.def` forwarder from a proxy also named `winmm.dll` to `WINMM.symbol` self-resolves to the proxy. It is not a valid system-backend design.
- Renaming and redistributing a host system WinMM is not accepted: module identity, OS servicing/build coupling, dependencies, and redistribution remain unresolved.
- A 48-name proxy is incomplete even if current static imports link. The synthetic proxy must preserve all 193 ordinal slots, names, and the NONAME export before any normal target execution.
- Research remains non-invasive. A final installer may place the proxy DLL only after an explicit exact-version install/update/rollback contract is implemented and authorized; this is a pending product feature, not permission to edit the current installation during development.

## Next implementation slice

1. Generate a repository-owned synthetic x86 fixture representing the full 193-entry WinMM export surface, including ordinal 2 NONAME.
2. Establish a distinct-backend mechanism that cannot self-forward and does not depend on copying/renaming the host system DLL. Validate name/ordinal identity and x86 ABI behavior entirely against synthetic processes first.
3. Add a loader-lock-safe bootstrap handoff. `DllMain` must not start UNM, wait for readiness, perform network work, or run cleanup coordination synchronously.
4. Only after those gates pass, run an isolated full-start compatibility experiment; do not place a proxy in the installed NCM directory yet.
5. Then implement the actual thin bootstrap: load config, start the existing sidecar coordinator, establish verified NCM-local routing, and tie cleanup to the exact NCM session.

## Reproduction

```powershell
& .\tools\build.ps1 -Configuration Debug
& .\tools\build.ps1 -Configuration Release
& .\tools\probe-ncm-winmm-load.ps1
```

Expected loader evidence on this host is a control path under `C:\Windows\SysWOW64\winmm.dll`, a treatment path under the private temporary experiment directory, `debug-events: 20` for each, zero remaining `cloudmusic.exe` processes, and no `ncm-unblock-297-winmm-probe-*` directory after cleanup.
