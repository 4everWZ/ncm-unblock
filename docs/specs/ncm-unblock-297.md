# NCM Unblock 2.9.7 Specification

## Intent

- **Intended outcome:** A portable, native-first x86 `ncm_unblock.dll` for NCM 2.9.7.199711. The DLL starts and owns a replaceable upstream UNM Windows sidecar, establishes NCM-local routing, and reclaims the sidecar when the client session ends. A native launcher remains an investigation/lifecycle harness and a possible separately gated loader fallback, not the final user-facing substitute for the DLL.
- **In scope for the first DLL deliverable:** A verified proxy-DLL load boundary, complete forwarding of the selected system-DLL contract, a thin native bootstrap, a replaceable upstream UNM executable, loopback-only routing, readiness/startup/cleanup handling, a small file-based configuration, reversible deployment, and reproducible compatibility/performance evidence.
- **Out of scope for the first DLL deliverable:** An embedded settings UI, Windows service, resident injection daemon, system-wide hook, AppInit DLL, modified UNM source, native reimplementation of UNM, automatic certificate installation, support for other NCM versions, and selective routing before whole-client routing is verified.

## Contract

### Runtime boundary

- Release-owned runtime code is native C++20. Node does not enter `cloudmusic.exe`.
- The primary bootstrap artifact is `ncm_unblock.dll`. A proxy-DLL loader is preferred when it can preserve the complete original DLL contract; suspended-process injection is only a separately reviewed fallback if the proxy boundary fails.
- Loader-lock work remains minimal. Sidecar startup, readiness, routing installation, and cleanup registration must not execute synchronously inside `DllMain`.
- The UNM core is an independently replaceable sidecar executable. A release does not ship a separate `node.exe`, Node package manager, `node_modules`, upstream source checkout, or development cache. An upstream standalone executable may contain its own implementation runtime.
- Select a standalone sidecar for the Windows operating-system architecture, not for the x86 architecture of `cloudmusic.exe`. Unsupported OS architectures fail before launch.
- The sidecar binds only to loopback. The bootstrap must not expose an unauthenticated general-purpose proxy on a LAN or public interface.
- The selected sidecar must run in its restricted/strict mode. For a sidecar that redirects HTTPS `CONNECT`, the bootstrap manages distinct HTTP and HTTPS loopback ports.
- The bootstrap owns each sidecar process it starts and must arrange bounded cleanup when NCM exits, initialization fails, or the owning process terminates. It must not terminate an unrelated pre-existing NCM or UNM process.
- Idle lifecycle handling is event-driven; polling loops are not part of the target design.

### DLL delivery behavior

1. Verify the exact NCM 2.9.7.199711 image, the accepted DLL load boundary, the bootstrap DLL, and the configured UNM core.
2. Preserve the selected proxy DLL's complete name/ordinal export contract and forward it to a verified system backend without self-forwarding or OS-build ambiguity. Completeness means every ordinal slot, every exported name, and every unnamed ordinal of the backend surface, not the subset the current client happens to import.
3. Leave loader lock before starting bootstrap work; load configuration and initialize one NCM-session owner.
4. Acquire exclusive loopback leases for an available HTTP/HTTPS port pair, honoring explicitly configured fixed ports only when both can be bound safely. Because the unmodified sidecar cannot accept inherited listener sockets, keep both leases until the suspended sidecar is owned by its private job, then release them immediately before resume. Automatic mode retries the entire handoff within a fixed budget; a configured fixed pair fails without silently changing ports.
5. Start the sidecar with explicit loopback binding and restricted proxy arguments. Readiness requires a live managed tree, both loopback listeners owned by processes in that private job, and a successful local PAC response where supported; it is not provider or playback health.
6. Establish NCM-local routing according to a verified 2.9.7 mechanism without silently changing system proxy or certificate state.
7. Keep the sidecar alive only for the owning NCM session, then stop it and report actionable failure information when startup fails or the managed lifetime ends.

The exact NCM proxy-setting mechanism, existing-instance behavior, multi-process exit condition, and end-to-end health check remain investigation outputs and are not guessed by this specification.

### Configuration and packaging

- Configuration is a human-readable file stored beside the bootstrap package or in its portable data directory. Registry and database storage are excluded from the first DLL deliverable.
- Configuration must distinguish automatic port selection from an explicit fixed port. Source ordering and quality options are passed through only after they are verified against the pinned sidecar interface.
- Downloaded binaries, user configuration, certificates, and runtime logs are not source-controlled.
- Placing the proxy DLL into an NCM installation is a deliverable of the reversible deployment feature, not a development affordance. It requires an implemented and authorized exact-version install/update/rollback contract; until then, investigation stays non-invasive and uses isolated copies.
- Third-party redistribution requires recorded upstream identity, version, license, architecture, runtime interface, corresponding-source path, required notices, and redistribution terms before an artifact enters a release.
- Publicly shared upstream private keys or expired leaf certificates are not accepted as the product trust design. If HTTPS interception proves necessary, certificate generation, storage, narrowly scoped trust, renewal, removal, and failure recovery require a separate accepted design and explicit user authorization before trust-store mutation.

### Evolution gates

- Compatibility of NCM 2.9.7 through a localhost UNM proxy is a go/no-go gate for the DLL deliverable.
- The current host accepts an application-directory WinMM before NCM initialization. Normal execution remains gated on complete WinMM export parity, a distinct verified system backend that cannot self-forward, a loader-lock-safe bootstrap trigger, and reversible deployment.
- The backend must be reached without renaming or redistributing a host system DLL and without a name-resolved PE forwarder. Both are rejected: a same-named proxy's forwarder string resolves back to the proxy, and a renamed system copy leaves module identity, servicing/build coupling, its dependency closure, and redistribution terms unresolved.
- Export parity, backend identity, and x86 ABI behavior are validated against repository-owned synthetic fixtures and synthetic processes before any experiment starts the real client through a proxy.
- The pinned export surface is captured from one Windows build. A proxy must state which host surfaces it accepts and fail with actionable information rather than forward a partial surface. The two directions of disagreement are treated differently on purpose: a host missing a pinned entry stops the process at resolution, because a thunk with no target would corrupt the caller's stack; a host exporting beyond the pinned set keeps forwarding what it does cover, reports the mismatch, and installs no routing, because stopping a working client over entries it may never call is worse than declining the feature.
- Suspended-process injection, inline hooks, and selective proxying remain separate fallback/evolution gates. MinHook is not a dependency until an accepted routing design requires inline hooking.
- Performance budgets are set from a documented baseline rather than from the provisional numbers in input material.

## Acceptance

| Observable requirement | Verification method |
|---|---|
| The target is the signed NCM 2.9.7.199711 x86 executable | Repeatable PE metadata inspection and Authenticode status recorded in the runtime report |
| The NCM-to-local-UNM path supports normal and unavailable tracks, search, play, and track changes | Versioned compatibility matrix using a pinned UNM executable and documented NCM configuration |
| `ncm_unblock.dll` loads through the accepted boundary while preserving the original DLL contract | Synthetic full-export fixture tests followed by exact-version isolated compatibility tests |
| A bootstrap-owned sidecar becomes ready before NCM routing begins | Focused integration test covering success, timeout, early exit, and port collision |
| The sidecar is reclaimed after normal NCM exit, bootstrap initialization failure, owning-process termination, and sidecar failure | Process-lifecycle integration tests that distinguish root exit from private-job tree completion |
| The bootstrap does not terminate unrelated existing processes or expose the proxy outside loopback | Negative integration tests and socket ownership/address inspection |
| Normal operation leaves system proxy and certificate state unchanged unless a separately authorized trust workflow is active | Before/after state comparison and uninstall/recovery test |
| Release contents are portable and omit separate Node development/runtime tooling and generated development data | Packaging manifest inspection |
| Performance claims use defined versions, sampling windows, and comparable scenarios | Baseline report for NCM alone, standalone UNM, and the launcher-managed path |

## Open decisions

- Which NCM-supported proxy configuration is reliable for 2.9.7 HTTP and HTTPS traffic without system-wide changes?
- What minimum Windows version is supported, including nested-job behavior when the launcher itself already runs inside a job?
- Is upstream UNM v0.28.0 acceptable after resolving its expired bundled leaf certificate, third-party notices, artifact authenticity, and target-version compatibility?
- If HTTPS interception is required, can it be implemented with an acceptable per-user certificate lifecycle, or must the routing design change?
- How should bootstrap ownership behave across NCM's existing-instance and multi-process startup paths?
- Which process set defines the end of an NCM session?
- Which Windows builds share the pinned WinMM export surface, and how should a proxy behave when the host surface differs from the one it was built against?
- What reversible installer/portable deployment contract may place the proxy DLL for the user without treating research-time installation mutation as implicit authorization?
