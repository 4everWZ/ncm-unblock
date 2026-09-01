# NCM Unblock 2.9.7 Specification

## Intent

- **Intended outcome:** A portable, native-first Windows launcher that makes an upstream UnblockNeteaseMusic (UNM) core available to NetEase Cloud Music (NCM) 2.9.7 only for the lifetime of that client.
- **In scope for the MVP:** NCM 2.9.7.199711 on Windows; a native launcher; a replaceable upstream UNM Windows executable; loopback-only proxying; readiness, startup, and cleanup handling; a small file-based configuration; and reproducible compatibility and performance evidence.
- **Out of scope for the MVP:** An embedded settings UI, Windows service, resident injector, system-wide hook, AppInit DLL, modified UNM source, native reimplementation of UNM, automatic certificate installation, support for other NCM versions, and a final DLL proxy or selective-routing design before investigation evidence supports one.

## Contract

### Runtime boundary

- Release-owned runtime code is native C++20. Node does not enter `cloudmusic.exe`.
- The UNM core is an independently replaceable sidecar executable. A release does not ship a separate `node.exe`, Node package manager, `node_modules`, upstream source checkout, or development cache. An upstream standalone executable may contain its own implementation runtime.
- Select a standalone sidecar for the Windows operating-system architecture, not for the x86 architecture of `cloudmusic.exe`. Unsupported OS architectures fail before launch.
- The sidecar binds only to loopback. The launcher must not expose an unauthenticated general-purpose proxy on a LAN or public interface.
- The selected sidecar must run in its restricted/strict mode. For a sidecar that redirects HTTPS `CONNECT`, the launcher manages distinct HTTP and HTTPS loopback ports.
- The launcher owns each sidecar process it starts and must arrange bounded cleanup when NCM exits, startup fails, or the launcher terminates. It must not terminate an unrelated pre-existing NCM or UNM process.
- Idle lifecycle handling is event-driven; polling loops are not part of the target design.

### MVP launch behavior

1. Resolve and validate the configured NCM 2.9.7 executable and UNM core.
2. Acquire exclusive loopback leases for an available HTTP/HTTPS port pair, honoring explicitly configured fixed ports only when both can be bound safely. Because the unmodified sidecar cannot accept inherited listener sockets, this is a bounded handoff rather than an atomic socket transfer: keep both leases until the suspended sidecar is owned by its private job, then release them immediately before resume. In automatic mode, a handoff collision retries the whole pair-selection/start attempt within a fixed budget; a configured fixed pair fails without silently changing ports.
3. Start the sidecar with explicit loopback binding and restricted proxy arguments. Readiness requires a live managed tree, both loopback listeners owned by processes in that private job, and a successful local PAC response where supported; it is not provider or playback health.
4. Start or attach to NCM only according to behavior established by the compatibility investigation; do not silently change system-wide proxy or certificate state.
5. Keep the sidecar alive while the launcher-owned NCM lifetime is active.
6. Stop the sidecar and report actionable failure information when the managed lifetime ends.

The exact NCM proxy-setting mechanism, existing-instance behavior, multi-process exit condition, and end-to-end health check remain investigation outputs and are not guessed by this specification.

### Configuration and packaging

- Configuration is a human-readable file stored beside the launcher or in its portable data directory. Registry and database storage are excluded from the MVP.
- Configuration must distinguish automatic port selection from an explicit fixed port. Source ordering and quality options are passed through only after they are verified against the pinned sidecar interface.
- Downloaded binaries, user configuration, certificates, and runtime logs are not source-controlled.
- Third-party redistribution requires recorded upstream identity, version, license, architecture, runtime interface, corresponding-source path, required notices, and redistribution terms before an artifact enters a release.
- Publicly shared upstream private keys or expired leaf certificates are not accepted as the product trust design. If HTTPS interception proves necessary, certificate generation, storage, narrowly scoped trust, renewal, removal, and failure recovery require a separate accepted design and explicit user authorization before trust-store mutation.

### Evolution gates

- Compatibility of NCM 2.9.7 through a localhost UNM proxy is a go/no-go gate for the launcher MVP.
- DLL proxying, suspended-process injection, inline hooks, and selective proxying require separate evidence after the launcher path works. MinHook is not a dependency until an accepted design requires inline hooking.
- Performance budgets are set from a documented baseline rather than from the provisional numbers in input material.

## Acceptance

| Observable requirement | Verification method |
|---|---|
| The target is the signed NCM 2.9.7.199711 x86 executable | Repeatable PE metadata inspection and Authenticode status recorded in the runtime report |
| The NCM-to-local-UNM path supports normal and unavailable tracks, search, play, and track changes | Versioned compatibility matrix using a pinned UNM executable and documented NCM configuration |
| A launcher-owned sidecar becomes ready before NCM routing begins | Focused integration test covering success, timeout, early exit, and port collision |
| The sidecar is reclaimed after normal NCM exit, NCM startup failure, launcher termination, and sidecar failure | Process-lifecycle integration tests that distinguish root exit from private-job tree completion |
| The launcher does not terminate unrelated existing processes or expose the proxy outside loopback | Negative integration tests and socket ownership/address inspection |
| Normal operation leaves system proxy and certificate state unchanged unless a separately authorized trust workflow is active | Before/after state comparison and uninstall/recovery test |
| Release contents are portable and omit separate Node development/runtime tooling and generated development data | Packaging manifest inspection |
| Performance claims use defined versions, sampling windows, and comparable scenarios | Baseline report for NCM alone, standalone UNM, and the launcher-managed path |

## Open decisions

- Which NCM-supported proxy configuration is reliable for 2.9.7 HTTP and HTTPS traffic without system-wide changes?
- What minimum Windows version is supported, including nested-job behavior when the launcher itself already runs inside a job?
- Is upstream UNM v0.28.0 acceptable after resolving its expired bundled leaf certificate, third-party notices, artifact authenticity, and target-version compatibility?
- If HTTPS interception is required, can it be implemented with an acceptable per-user certificate lifecycle, or must the routing design change?
- How should the launcher behave when an NCM 2.9.7 instance already exists?
- Which process set defines the end of an NCM session?
- After the MVP, does measured evidence justify a DLL proxy, injection, selective routing, or no injected component at all?
