# NCM Unblock 2.9.7 MVP Work Plan

- **Goal:** Deliver and verify a thin native `ncm_unblock.dll` for NCM 2.9.7.199711 that loads through a verified proxy-DLL boundary, owns a pinned UNM sidecar only for the client session, and leaves no resident injector or service. The launcher code is supporting lifecycle/harness infrastructure, not a downgraded final product.
- **Primary contract:** [NCM Unblock 2.9.7 specification](../specs/ncm-unblock-297.md)

## Changed claim

The repository will provide a reproducible, non-invasive investigation path first, then the DLL bootstrap and its sidecar lifecycle/routing implementation. Reference material under `tmp/` remains non-canonical evidence input, but its explicit DLL outcome is now adopted by the specification.

## Work

| Task | Status | Done when | Evidence or dependency |
|---|---|---|---|
| Establish repository governance and canonical scope | Done | Root instructions, one specification, one current plan, ignore rules, and line-ending policy are committed | Initial repository commit |
| M0: implement repeatable static/runtime inspection tooling | Done | A Win32 tool can inspect PE architecture/imports and report relevant NCM processes/modules without modifying the installation | Fresh Win32 build; focused CTest; real 2.9.7 probe |
| M0: record the 2.9.7 runtime inventory | In progress | Architecture, signature, process roles, loaded network stacks, active connection ownership, and loader candidates are separated into observed facts and hypotheses | [Runtime investigation](../research/ncm-2.9.7-runtime.md); playback path and loader stability remain open |
| M0: establish proxy and HTTPS behavior | In progress | Client-local proxy configuration, certificate behavior, traffic ownership, and rollback are reproducible without exposing user data | Synthetic observer and command-line negative control are complete; client UI automation did not persist a proxy change, so the UI matrix remains open |
| M1: pin and validate an upstream UNM executable | In progress | Version, source, license, architecture, CLI, readiness, and the normal/unavailable-track workflow are recorded | v0.28.0 primary-source audit complete; artifact execution, notices, certificate design, and compatibility matrix remain open |
| M2: implement the native launcher lifecycle | In progress | Port reservation, sidecar job ownership, readiness, NCM start/attach policy, cleanup, and diagnostics meet the specification | Sidecar job ownership, loopback-pair handoff, bounded automatic retry, fixed-port failure, listener ownership, and PAC initialization are covered with a synthetic sidecar; NCM start/attach remains |
| M3: measure the baseline | Pending | NCM-alone, standalone-UNM, and launcher-managed measurements use one documented method | Performance report |
| M4: implement the native DLL bootstrap | In progress | The selected proxy preserves the original DLL contract, leaves loader lock before bootstrap, starts the sidecar, installs verified routing, and cleans up with NCM | Application-directory WinMM selection is proven on this host; complete forwarding/backend identity and bootstrap trigger remain |
| M5: package reversible DLL deployment | Pending | Exact-version checks, install/update/rollback, config, notices, and artifacts are reproducible without a resident injector | Packaging and recovery tests |
| Package the MVP | Pending | A portable manifest contains only approved runtime artifacts and configuration | Clean release build and manifest inspection |

## Current evidence

- The local target is `cloudmusic.exe` version 2.9.7.199711 with a valid NetEase Authenticode signature and a PE32 x86 header.
- The running client has one browser/main process plus GPU, renderer, and reporter processes. The browser/main process currently owns observed TCP connections.
- The process set loads WinHTTP, WinINet, libcurl, and CEF-related modules, so static imports alone do not identify the playback routing layer.
- The Win32 runtime probe cleanly configures and builds through `tools/build.ps1`; focused PE/process tests pass, and the tool reproduces the target facts without changing NCM.
- The independent Win32 module-load investigation probe requires signed x86 PE32 target and expected images, pins both source files without write/delete sharing, rejects reparse paths, verifies an ordinary non-delay import, assigns the debuggee to a private kill-on-close job before continuing its first debug event, and requires the `CREATE_PROCESS`/matching `LOAD_DLL` file handles to match the pinned volume/file identities. Synthetic tests prove both expected- and unexpected-identity runs terminate the job before the fixture DLL initializer or executable entry point writes a marker, and that bounded event-limit cleanup drains the debuggee.
- A real isolated A/B on this host observed the control root load `C:\Windows\SysWOW64\winmm.dll` and the treatment root load the signed x86 WinMM copy beside the isolated executable, both at debug event 20 and before DLL initialization or executable entry. The private Jobs exited and allowlisted temporary directories were removed. This proves the current loader accepts the candidate location; it does not prove forwarding, routing, full startup, or an acceptable deployment mutation.
- The loopback observer binds only to `127.0.0.1`, records fixed classifications rather than raw targets or headers, has bounded event/time limits, and rejects traffic. A synthetic check distinguished an absolute HTTP request from HTTPS `CONNECT` without emitting test secrets.
- A controlled launch exposed the exact process-local `--proxy-server` argument on the target root process, but the observer received zero events while the root process established other HTTP/HTTPS connections. This path cannot be treated as NCM-wide routing evidence.
- Official v0.28.0 sources define x64 and ARM64 Windows standalone artifacts, explicit loopback/strict CLI arguments, separate HTTP/HTTPS ports, and PAC-based initialization evidence. They do not provide an x86 artifact or a dedicated health endpoint.
- The v0.28.0 bundled leaf certificate expired on 2026-07-24. The public default private-key/CA arrangement is not an acceptable product trust lifecycle, so downloading the executable would not by itself clear the HTTPS or compatibility gate.
- Static 2.9.7 evidence shows the persisted `Proxy` object's `Type/Host/Port/UserName/Password` schema and, separately, an internal client path that recognizes `socks/socks4/socks5/https/http/ie` and emits CEF `--proxy-server` or `--no-proxy-server` switches. The DLL has no named AppConfig/proxy export. Its internal `setLocalConfig` bridge accepts three string-typed arguments and calls a generic internal configuration setter, but neither their meaning nor a proxy-object persistence contract is established. This does not prove playback uses CEF and is not a safe external write API.
- The launcher process primitive creates the child suspended, assigns it to an unnamed kill-on-close job before resume, preserves Windows argv values, distinguishes root exit from job-tree completion, and verifies bounded tree termination. Tests also prove that closing the private job reclaims a grandchild while a same-image process outside that job remains active.
- The loopback-pair primitive holds distinct HTTP/HTTPS ports with exclusive binds on `127.0.0.1`. An integration test prepares a suspended job-owned child while both leases remain held, releases the leases, resumes the child, and verifies that it can bind both ports; fixed-pair collision and post-release rebinding are also covered.
- The UNM sidecar coordinator appends the audited v0.28.0 safety arguments, prevents pass-through overrides of address/ports/strict mode, keeps lease release adjacent to process resume, and requires both listeners' complete owner sets to remain inside the private job before and after a complete non-empty PAC response. Synthetic tests cover automatic retry, fixed-port identity/collision, and invalid PAC rejection; no real UNM artifact has been executed.

## Blockers

- Proxy/HTTPS investigation must avoid exposing credentials or changing persistent client/system state without an explicit rollback procedure.
- NCM persists proxy fields inside opaque private `localdata` through its own asynchronous AppConfig writer; there is no verified external write API. A real A/B run therefore requires a controlled NCM UI change and private stopped-process snapshot/restore, not direct file editing.
- The controlled command-line experiment required bounded termination after NCM closed to the tray. With explicit authorization, only the recorded experiment process tree was terminated; `localdata` bytes, timestamps, attributes, and owner/group/DACL were restored and verified, and all private recovery files were removed.
- NCM's embedded UI controls are not exposed through UI Automation. Version/layout-gated coordinate and window-message attempts did not produce a persisted `localdata` change, so they do not establish client-local routing. The experiment harness now emits its private recovery path before launch, and a separate recovery command can restore a retained snapshot after an external interruption.
- An upstream executable cannot enter packaging until artifact authenticity, target compatibility, certificate lifecycle, corresponding source, and bundled dependency notices are verified.

## Next action

Continue with the DLL target. Build a synthetic x86 WinMM-proxy fixture with complete 193-entry name/ordinal parity and a distinct verified backend; prove that it cannot self-forward and that bootstrap work begins only after loader lock. Do not run a proxy DLL inside NCM or modify the installed directory until the synthetic backend, full export surface, loader-lock handoff, and reversible deployment contract pass review. Do not substitute the falsified root flag, private NCM RVAs, fragile coordinate automation, system proxy, or certificate trust for the required DLL routing implementation.
