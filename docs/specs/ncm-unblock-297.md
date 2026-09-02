# NCM Unblock 2.9.7 Specification

## Intent

- **Intended outcome:** A portable, native-first x86 `ncm_unblock.dll` for NCM 2.9.7.199711. The DLL loads through a verified proxy-DLL boundary, injects a small JavaScript shim into the client's own embedded browser, and answers the shim's match requests from an in-process native matcher. When a track is unavailable the shim patches the already-decrypted business-layer response; when it is available nothing is contacted and nothing is changed. No proxy server, no separate runtime, and no resident process is part of the primary path.
- **In scope for the first DLL deliverable:** A verified proxy-DLL load boundary, complete forwarding of the selected system-DLL contract, a thin loader-lock-safe native bootstrap, a small file-based configuration, JavaScript injection through the client's embedded browser, a JavaScript-to-native bridge, a native matcher covering track metadata and one alternate source, candidate selection with a bounded audio probe, an in-memory cache, reversible deployment, and reproducible compatibility/performance evidence.
- **Out of scope for the first DLL deliverable:** An HTTP or HTTPS proxy server, TLS interception, certificate generation or trust-store mutation, a native reimplementation of EAPI cryptography, an embedded settings UI, a Windows service, a resident injection daemon, a system-wide hook, an AppInit DLL, an embedded Node runtime, download-URL patching, support for other NCM versions, and additional alternate sources beyond the first.
- **Retained fallback:** The launcher, port-pair, and UNM sidecar lifecycle modules are implemented and tested against a synthetic sidecar; no real UNM artifact has been executed and client start/attach was never built. They remain the accepted fallback if the business-layer route fails a gate below, and are not deleted. They are not the primary path, because no client-local routing mechanism for 2.9.7 has been found and three candidates have been falsified.

## Contract

### Runtime boundary

- Release-owned runtime code is native C++20. Node does not enter `cloudmusic.exe`, and no release ships a Node toolchain, package manager, `node_modules`, or upstream source checkout.
- The primary bootstrap artifact is `ncm_unblock.dll`, delivered through the verified WinMM proxy boundary. Suspended-process injection remains a separately reviewed fallback.
- Loader-lock work remains minimal. Nothing beyond scheduling a bootstrap body executes synchronously inside `DllMain`.
- The bootstrap runs in every process of the client that loads it and must decide its role from that process rather than assume one. Browser-process and render-process responsibilities are distinct and must not be conflated.
- JavaScript is injected through the embedded browser's own supported extension mechanism. Inline hooking of CEF callback structs is not part of the primary design; MinHook is not a dependency until an accepted design requires it.
- CEF entry interception is limited to named ordinary imports in the loaded `cloudmusic.dll` image. The bootstrap replaces an IAT slot only after the loaded `libcef.dll` repeats the pinned API hashes and required exports; a mismatch leaves the slot and client callbacks untouched.
- Any struct layout taken from the embedded browser must be pinned against the API revision reported by the client's own module, not assumed from a published CEF release, because the client ships a privately modified Chromium.
- The injected shim is a request interceptor, not an application. It anchors only on stable artifacts — request path literals, the framework module registry, and response field names — and never on a minified identifier, because the frontend package updates independently of the pinned client version.
- The shim does no matching, no networking, and no persistence. It observes a response, decides availability, and either passes it through or asks native code for a replacement.
- The matcher runs only for a track the client itself reported as unavailable. An available track contacts no alternate source.
- Native networking uses WinHTTP. No additional network runtime, HTTP library, or TLS stack enters the release.
- Caching is in-memory only for the client session and is released when the process exits. No database, no on-disk cache, and no user data are written.
- Idle lifecycle handling is event-driven; polling loops are not part of the target design.
- The bootstrap exposes no listening socket. If a transport between the render and browser processes is required, it uses the embedded browser's own inter-process messaging rather than a loopback listener.

### DLL delivery behavior

1. Verify the exact NCM 2.9.7.199711 image, the accepted DLL load boundary, and the bootstrap DLL.
2. Preserve the selected proxy DLL's complete name/ordinal export contract and forward it to a verified system backend without self-forwarding or OS-build ambiguity. Completeness means every ordinal slot, every exported name, and every unnamed ordinal of the backend surface, not the subset the current client happens to import.
3. Leave loader lock before starting bootstrap work; load configuration and initialize one owner per process role.
4. Pin the embedded browser's API revision from the client's own module before relying on any layout derived from it, and decline the feature rather than proceed on a mismatch.
5. Inject the shim into the client's contexts. The shim must defer its own installation until the frontend registry it hooks exists, and a failure to find its anchors must leave the client untouched rather than partially hooked.
6. Intercept the player-URL response in the business layer. Pass through anything the client can already play; for an unavailable track, request a replacement from native code.
7. Resolve a replacement natively: read the track's metadata, query the configured alternate source, select a candidate, probe it with a bounded ranged read, and return a validated URL with its bitrate and size. Fail by returning nothing, never by producing an unverified URL.
8. Return the result to the shim asynchronously and patch the response object in place, then let the client's original callback proceed unchanged.
9. Clean up with the client session. The bootstrap owns nothing that outlives the process it loaded into.

The exact injection timing, the deferral mechanism the shim uses to reach the frontend registry, the client's tolerance for an asynchronously resolved callback, and the end-to-end playback health check remain investigation outputs and are not guessed by this specification.

### Configuration and packaging

- Configuration is a human-readable file stored beside the bootstrap package or in its portable data directory. Registry and database storage are excluded from the first DLL deliverable.
- The file is `ncm_unblock.ini` beside the bootstrap DLL. It is UTF-8, optionally with a byte-order mark, and holds one `key = value` per line with `#`/`;` comments, blank lines, and case-insensitive keys; there are no sections.
- An absent file means "run with this build's defaults" and the bootstrap continues. A present file that is unreadable, contains an unknown key, a repeated key, a malformed line, or an out-of-range value means the user stated an intent this build cannot honor: the bootstrap reports the offending key and line, keeps forwarding, and installs nothing. Neither outcome may stop a working client, and a partially understood file is never approximated.
- The implemented keys `enabled`, `sidecar_executable`, `http_port`, `https_port`, `automatic_attempts`, and `readiness_timeout_ms` remain accepted so existing fallback configuration is not invalidated. Only `enabled` affects the selected primary path; the production bootstrap must not launch or configure a sidecar unless a later accepted gate explicitly activates the fallback. Keys for source ordering, quality preference, and cache lifetime are added by the milestone that implements them, not in advance.
- Downloaded binaries, user configuration, and runtime logs are not source-controlled. The client's frontend packages are read-only inputs and are never extracted into the repository, modified, or redistributed.
- Placing the proxy DLL into an NCM installation is a deliverable of the reversible deployment feature, not a development affordance. It requires an implemented and authorized exact-version install/update/rollback contract; until then, investigation stays non-invasive and uses isolated copies.
- Third-party redistribution requires recorded upstream identity, version, license, architecture, runtime interface, corresponding-source path, required notices, and redistribution terms before an artifact enters a release. Provenance is recorded in [the third-party notices](../../THIRD-PARTY-NOTICES.md), and an entry is required before material enters the repository, not only before it enters a release. This applies to the retained fallback's sidecar and to any header set used to describe the embedded browser's interface.
- Alternate-source request behavior must be attributable to a documented upstream reference and re-verified against current provider behavior, because published provider interfaces change independently of this project.

### Evolution gates

Each gate below can falsify the primary path. Work does not proceed past a gate that has not been cleared.

- **Cleared.** The current host accepts an application-directory WinMM before NCM initialization; the proxy forwards all 193 pinned entries into the real system module; the exact client starts from an isolated copy with the proxy beside it and stops with the proxy's own `0xE0C40001` when the backend cannot resolve; and the bootstrap leaves loader lock before doing work.
- **Cleared statically, unverified at runtime.** The player-URL request path, the framework module registry, and the `privilege` fields exist in the frontend package the client actually loads, and the response reaching the business layer is already decrypted.
- **Cleared.** The embedded browser reports the published CEF 3.1916 Windows and universal API hashes, and the pinned Win32 layouts cover only the structures the bootstrap will dereference. Any mismatch must decline injection.
- **Partially cleared.** On an isolated exact-client Release run, three processes accepted the ordinary `cef_execute_process` IAT replacement and one renderer invoked the wrapped WebKit-initialized callback with `cef_register_extension` succeeding and `marker=2`. M3 marker clearance means a native `cef_v8handler_t::execute` counter greater than zero (`marker>0` in the injection report), not a JavaScript boolean alone and not registration success alone; that Execute observation is now cleared. The shim's deferral to a live frontend registry remains unmeasured, and the client's tolerance for an asynchronously resolved callback is unknown. The last is load-bearing: nothing beyond a mocked matcher is built before it is answered.
- The backend must be reached without renaming or redistributing a host system DLL and without a name-resolved PE forwarder. Both are rejected on measured evidence: a same-named proxy's forwarder string resolves back to the proxy, and a renamed system copy leaves module identity, servicing/build coupling, its dependency closure, and redistribution terms unresolved.
- The pinned export surface is captured from one Windows build. A host missing a pinned entry stops the process at resolution, because a thunk with no target would corrupt the caller's stack; a host exporting beyond the pinned set keeps forwarding what it does cover, reports the mismatch, and installs nothing, because stopping a working client over entries it may never call is worse than declining the feature.
- Falling back to the sidecar path additionally requires a verified 2.9.7 client-local routing mechanism, which does not exist today. Ambient proxy environment variables, a root `--proxy-server` switch, and direct private-configuration writes are each falsified or closed.
- If the fallback is ever taken and HTTPS interception proves necessary, certificate generation, storage, narrowly scoped trust, renewal, removal, and failure recovery require a separate accepted design and explicit user authorization before trust-store mutation. Publicly shared upstream private keys and expired leaf certificates are not accepted as the product trust design.
- Performance budgets are set from a documented baseline rather than from provisional numbers in input material.

## Acceptance

| Observable requirement | Verification method |
|---|---|
| The target is the signed NCM 2.9.7.199711 x86 executable | Repeatable PE metadata inspection and Authenticode status recorded in the runtime report |
| `ncm_unblock.dll` loads through the accepted boundary while preserving the original DLL contract | Synthetic full-export fixture tests followed by exact-version isolated compatibility tests |
| The embedded browser's API revision is pinned from the client's own module before any derived layout is used | Reported revision recorded in the runtime report and asserted by the bootstrap at startup |
| The shim installs only when its anchors are present, and otherwise leaves the client unmodified | Negative tests over a package whose anchors are renamed or absent |
| A native call issued from the shim can resolve asynchronously without triggering the client's retry path or a UI timeout | Isolated exact-version run with a mocked matcher over a controlled delay range |
| An unavailable track becomes playable and an available track is untouched | Versioned compatibility matrix over search, normal-track play, unavailable-track play, and track changes |
| The matcher contacts no alternate source for an available track | Request accounting over a session containing only available tracks |
| A candidate is validated before it is returned | Probe unit tests over valid, truncated, wrong-type, and unreachable candidates |
| Normal operation opens no listening socket, writes no user data, and leaves system proxy and certificate state unchanged | Socket and filesystem inspection, plus before/after state comparison |
| Nothing outlives the client session | Process-lifecycle tests covering normal exit, bootstrap failure, and abrupt termination |
| Release contents are portable and omit separate Node development/runtime tooling and generated development data | Packaging manifest inspection |
| Performance claims use defined versions, sampling windows, and comparable scenarios | Baseline report for NCM alone and the shim-plus-matcher path |

## Open decisions

- Which injection point makes the shim's anchors reliably reachable, given that an extension runs before page scripts?
- How long may a native call take before the client's retry path or the user experience degrades, and what timeout must the matcher therefore honor?
- Which alternate source should ship first, and against which current upstream reference is its request behavior verified?
- Does patching the player-URL response suffice, or does the client separately re-check availability through a path the shim does not observe?
- What cache lifetime is correct for a resolved URL, given that alternate-source URLs commonly expire?
- Which Windows builds share the pinned WinMM export surface, and how should a proxy behave when the host surface differs from the one it was built against?
- What reversible installer/portable deployment contract may place the proxy DLL for the user without treating research-time installation mutation as implicit authorization?
- Which process set defines the end of an NCM session?
