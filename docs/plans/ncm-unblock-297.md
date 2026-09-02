# NCM Unblock 2.9.7 MVP Work Plan

- **Goal:** Deliver and verify a thin native `ncm_unblock.dll` for NCM 2.9.7.199711 that loads through the verified WinMM proxy boundary, injects a small JavaScript shim into the client's own embedded browser, and patches unavailable player-URL responses from an in-process native matcher. No proxy server, no separate runtime, and no resident process is part of the primary path.
- **Primary contract:** [NCM Unblock 2.9.7 specification](../specs/ncm-unblock-297.md)

## Changed claim

NCM 2.9.7 resolves playable track URLs in business-layer JavaScript running in its own embedded browser, so an unavailable response can be replaced in an already-decrypted object. This removes the requirement that motivated the sidecar path — a client-local network routing mechanism — which remains unfound after three falsified candidates.

The load boundary, export parity, loader-lock handoff, and configuration work already completed carry over unchanged; they are the delivery vehicle for either path. The launcher, port-pair, and sidecar lifecycle modules are retained, tested, and demoted to fallback rather than deleted.

## Work

| Task | Status | Done when | Evidence or dependency |
|---|---|---|---|
| Establish repository governance and canonical scope | Done | Root instructions, one specification, one current plan, ignore rules, and line-ending policy are committed | Initial repository commit |
| M0: implement repeatable static/runtime inspection tooling | Done | A Win32 tool can inspect PE architecture/imports and report relevant NCM processes/modules without modifying the installation | Fresh Win32 build; focused CTest; real 2.9.7 probe |
| M0: record the 2.9.7 runtime inventory | Done | Architecture, signature, process roles, loaded network stacks, loader candidates, embedded browser version and exports, and frontend package structure are separated into observed facts and hypotheses | [Runtime investigation](../research/ncm-2.9.7-runtime.md) |
| M1: verified DLL load boundary and loader-lock-safe bootstrap | Done | The proxy preserves the original DLL contract, resolves a system backend that cannot self-forward, leaves loader lock before bootstrap, and loads configuration in the exact client | Full-export fixtures, host-WinMM resolution, isolated full start with a positive control, focused handoff and configuration tests |
| M2: pin the embedded browser API revision | Done | The revision reported by the client's own `libcef.dll` is recorded and reconciled against a published header set | Measured and reconciled: the C API surface is stock CEF 3.1916 |
| M3: inject JavaScript into the client's contexts | In progress | A null-handler extension executes in the client and is observable | Command-line debugging endpoint refused; native `cef_app_t` wrapping is the route, gated on a pinned header set |
| M4: reach the frontend from the injected context | Pending | The shim defers past extension time, finds the framework registry and the player-URL dispatcher, and records interception without modifying anything | M3; anchors recorded in the runtime investigation |
| M5: patch privilege synchronously | Pending | Greyed-out tracks become selectable and the UI stays consistent, with no native call involved | M4; no matcher dependency |
| M6: asynchronous native bridge with a mocked matcher | Pending | A native call from the shim resolves after a controlled delay without triggering the dispatcher's retry path or a UI timeout, over a delay range that establishes the usable budget | M2, M4; this is the load-bearing gate |
| M7: implement the native matcher | Pending | Metadata lookup, one alternate source, candidate selection, and a bounded ranged audio probe return a validated URL, bitrate, and size over WinHTTP | M6 cleared; provider behavior verified against a current upstream reference |
| M8: add the in-memory cache | Pending | Resolved and negative results are reused within the session and released at exit, with a lifetime chosen against observed URL expiry | M7 |
| M9: measure the baseline | Pending | NCM-alone and shim-plus-matcher measurements use one documented method | M7 |
| M10: package reversible DLL deployment | Pending | Exact-version checks, install/update/rollback, config, notices, and artifacts are reproducible without a resident injector | Packaging and recovery tests |
| Fallback: launcher and sidecar lifecycle | Retained | Port reservation, sidecar job ownership, readiness, cleanup, and diagnostics meet the specification | Complete against a synthetic sidecar; kept, not extended |
| Fallback: client-local routing | Blocked | A supported 2.9.7 routing mechanism is verified | No candidate remains; see Blockers |

## Current evidence

### Load boundary and bootstrap

- The local target is `cloudmusic.exe` version 2.9.7.199711 with a valid NetEase Authenticode signature and a PE32 x86 header.
- The host SysWOW64 WinMM (`10.0.26100.8972`) exports 193 functions over consecutive ordinals 2–194 with 192 names, one unnamed ordinal 2 aliasing the `PlaySound` entry point, and no PE forwarders. The root and conditional NCM/CEF closure import only 48 distinct WinMM names and no ordinals, so the observed import subset is far smaller than the parity requirement.
- The pinned manifest generates a proxy, a synthetic backend, and a `.def`-forwarder control, all named `winmm.dll`. The proxy and backend each expose 193 functions, 192 names, and the unnamed ordinal 2; all 193 entries dispatch through the proxy into a distinct absolute-path backend by name and by ordinal, the measured stack delta is zero on every call, and the ordinal 2 alias survives.
- The `.def` control fails at link time and at run time, closing that mechanism. Resolution is all-or-nothing: a probe pointed at an absent backend stops before any thunk dispatches, so a partial or substituted surface is never forwarded.
- Against this host's real WinMM, the release proxy exports and resolves all 193 pinned entries, the modules stay distinct, and a forwarded `mmsystemGetVersion` matches the direct call.
- The exact client starts from an isolated copy with the proxy beside it and reaches its main window, while the same run with a deliberately unresolvable backend exits with the proxy's `0xE0C40001`. The pair proves the client loads and calls the application-directory proxy. The installed directory and `localdata` were verified unchanged.
- `DllMain` only hands a body to a private thread, which Windows cannot start until the loader lock is released; a focused test asserts scheduling returns promptly, the body has not finished while blocked, its ordering ticket is later, and it ran on another thread. The body resolves the backend itself and verifies the host surface against the pinned shape.
- The verified session reads `ncm_unblock.ini` from the directory of the module the bootstrap is linked into, with defaults on absence and an attributed decline on any malformed, unknown, repeated, or out-of-range entry. Both decline paths keep forwarding.
- The census run confirms the bootstrap reaches every process role: four timelines were written from one root, two renderer, and one GPU process. Whatever runs in the render process is therefore reachable through the same boundary.

### Embedded browser and frontend

- `libcef.dll` is version `3.1916.1900` (Chromium 35) with a 2018-03-15 export time stamp and 149 exports. `cef_register_extension`, the `cef_v8*` constructors, `cef_process_message_create`, `cef_post_task`, and `cef_register_scheme_handler_factory` are all exported, so JavaScript injection and asynchronous cross-process messaging are available without hooking a callback struct.
- The client ships a privately modified Chromium: debug paths in `cloudmusic.dll` place the build under `orpheus\src\third_party\partial-chromium\src\` with its own `base/`, `net/`, `crypto/`, `spdy/`, `quic/`, and `socket/` subtrees. This originally made published CEF 3.1916 headers only a candidate for struct layouts; the measured API hashes below now establish their applicability. Reference injectors still target CEF 90 and later, so their offsets and struct definitions carry no validity here.
- The frontend package is a 100-byte header followed by a plain, unencrypted ZIP of 2,601 deflate entries. It is a hybrid of a legacy NEJ main UI under `pub/` and a newer React/webpack build under `pub/hybrid/`, served over the private `orpheus://orpheus/pub/` scheme.
- The frontend hot-updates independently of the pinned client version: the installed `package\orpheus.ntpk` is dated 2022-01-25 but `%LOCALAPPDATA%\Netease\CloudMusic\web.pack` is dated 2025-09-09 and is what loads. Comparing the two shows minified identifiers drift between builds while string literals, framework entry points, and API field names do not.
- Measured against the loaded package's `pub/core.js`: `window.nej` and `window.nm` are absent, but `window.NEJ` is present, `NEJ.P` appears 326 times, `nej.ut.j` 17 times, and `privilege` 182 times. The upstream browser port's target has an equivalent here reached as `NEJ.P('nej.ut.j')`; its specific expression does not apply unchanged.
- All three `/api/song/enhance/player/url` references dispatch through one shape, `bc.hb(path, params, callback)`. The callback accepts a response when `bm.code==200 && bm.data[0].code==200` and retries up to three times while the code is the unavailable sentinel with no data. Reading `bm.code` and `bm.data[0]` as ordinary members establishes that the response is already decrypted at this layer.
- All of the above is static structural inspection. Runtime reachability, injection success on this build, and asynchronous callback tolerance are unverified.
- The API revision is now measured rather than assumed. A fresh Win32 Release probe of the installed module reports caller-cleanup (`__cdecl`) with a `-4` stack delta, platform hash `78d4b4eb20e36e2b08572b98645dde08e987fbad`, universal hash `ce45d134468cd9bad310409c96e5108d75fac3c7`, build revision `1900`, CEF major 3 over Chrome 35.0.1916.157, and all 14 required entry points present; it exits `0` with `pinned-api-match: yes`. The commit hash is empty on this build. Focused tests build one fixture source under both x86 cleanup contracts, so the convention measurement is falsifiable rather than hardcoded, and a fixture that withholds `cef_version_info` is rejected outright instead of returning a partial revision. The same Release build passed all 12 repository tests, including the new CEF probe test.
- Whether the measured hash pair matches the published CEF 3.1916 branch is established: two independently vendored CEF 3.1916 distributions define the identical `CEF_API_HASH_UNIVERSAL` and Windows `CEF_API_HASH_PLATFORM`, while carrying different revisions (1721 and 1781 over Chromium 35.0.1916.86 and .138) than the client's 1900 over 35.0.1916.157. The C API surface is stock for the branch, NetEase's `partial-chromium` changes do not reach it, and published CEF 3.1916 headers describe this module's struct layouts.
- A process-local `--remote-debugging-port` on an isolated run opened no endpoint within 60 seconds. CEF3 starts its DevTools server from `CefSettings.remote_debugging_port` and converts settings into switches rather than reading them back, so the switch is not a route in. Live frontend exploration now depends on the same native hook injection does; conversely, code that wraps `cef_app_t` can set that setting itself.
- The ABI this project depends on is pinned in [include/ncm/cef/abi_1916.hpp](../../include/ncm/cef/abi_1916.hpp), transcribed from upstream branch `1916` rather than vendored, because the transitive include closure of `cef_app_capi.h` is most of the upstream tree and there is no meaningful subset. Only `cef_base_t`, `cef_string_utf16_t`, `cef_app_t`, and `cef_render_process_handler_t` are declared; every other CEF object stays incomplete so a member access fails to compile instead of assuming a layout. Layout assertions guard the member counts, and the probe reports `pinned-api-match` against the live module and exits non-zero on a mismatch. Provenance and the three-clause BSD notice are recorded in [the third-party notices](../../THIRD-PARTY-NOTICES.md); no CEF source or binary is redistributed.

### Retained fallback

- The launcher process primitive creates the child suspended, assigns it to an unnamed kill-on-close job before resume, preserves Windows argv values, distinguishes root exit from job-tree completion, and verifies bounded tree termination.
- The loopback-pair primitive holds distinct HTTP/HTTPS ports with exclusive binds on `127.0.0.1`, and an integration test covers the suspended-child handoff, fixed-pair collision, and post-release rebinding.
- The UNM sidecar coordinator appends the audited v0.28.0 safety arguments, prevents pass-through overrides of address/ports/strict mode, keeps lease release adjacent to process resume, and requires both listeners' owner sets to remain inside the private job before and after a complete PAC response. No real UNM artifact has been executed.
- Official v0.28.0 sources define x64 and ARM64 Windows standalone artifacts but no x86 one, and provide no dedicated health endpoint. Its bundled leaf certificate expired on 2026-07-24 and its default private key is public, so the artifact would not by itself clear an HTTPS or compatibility gate.

### Falsified routing candidates

- A controlled launch exposed the exact process-local `--proxy-server` argument on the root process, but the observer received zero events while that process established other HTTP/HTTPS connections. The Chromium command-line flag does not provide NCM-wide routing.
- A process-local `http_proxy`/`https_proxy`/`all_proxy` differential produced seven requests, all inside the startup window, and no event at all from two operator track changes. The variables reach part of the API surface and are not a routing mechanism for playback. The same run named the paths that bypassed them from the client's own diagnostics: an `http_file_down_manager` completing multi-megabyte transfers, a CEF `client_handler` completing an HTTPS album-art request, and a `SOCKS(4/4a/5) proxy node plugin` unregistered at exit. A privately maintained Chromium `net/` stack inside `cloudmusic.dll` explains why neither ambient nor command-line proxy settings apply.
- NCM persists proxy fields inside opaque private `localdata` through its own asynchronous writer, and its embedded settings controls are not exposed through UI Automation. Version-gated coordinate and window-message attempts did not persist a change, so the private-configuration path is closed rather than merely untried.
- A 600-second in-process module-load census returned its pre-registered inconclusive branch: every candidate network stack was already mapped in the root within 1.25 seconds, `audio_render` loads were startup device initialization, and 599 seconds of sign-in and playback produced no further classified load. Module-load timing carries no playback signal in this client. The census is retained as the module inventory it does establish.

### Harness

- Both client experiments share [the isolation harness](../../tools/lib/isolated-client.ps1): exact-version and signature pinning, refusal to start beside a running client, the private copy and its ACL, the `localdata` fingerprint and verified restore, and reclaim limited to images under the private run directory have one implementation.
- The loopback observer binds only to `127.0.0.1`, records fixed classifications rather than raw targets or headers, has bounded event and time limits, and rejects traffic.

## Blockers

- The asynchronous-callback gate is unproven and everything downstream of M6 depends on it. If the dispatcher's retry path fires or the UI times out before a native match can complete, the business-layer route fails and the project returns to the fallback, which is itself blocked on routing.
- The frontend updates independently of the pinned client version, so any anchor must be re-validated against `web.pack` rather than the installed package, and a shim must decline rather than half-install when an anchor is missing.
- Client experiments cannot contain client-local state by redirecting `%LOCALAPPDATA%`; the client reads the real user profile regardless. They are gated on no client process running, a private pre-run copy of `localdata`, and post-run verification. `HKCU\Software\Netease` remains outside the boundary.
- Parity is pinned to one host WinMM build (`10.0.26100.8972`). The mismatch behavior is decided and implemented, but which Windows builds share this surface is unmeasured, so the supported host set is unresolved.
- The bootstrap resolves the backend on its own thread, so the ordinary `LoadLibrary`-during-initialization hazard only applies if a thunk wins the race. That path is reachable and not separately exercised.
- Placing a proxy DLL in the installed NCM directory is not authorized during investigation. It waits on the reversible exact-version install/update/rollback contract.
- The retained fallback cannot be unblocked by any remaining proxy setting. Escalating it would mean connection-level attribution inside the client, which is a larger commitment than the business-layer route it would be replacing.

## Next action

Work the gates in order and stop at the first one that fails. Do not modify the installed directory at any step, and do not build a real matcher before M6 answers the asynchronous-callback question.

1. **M3 — prove injection.** Wrap the client's `cef_app_t` so a render-process handler can call `cef_register_extension` with a null handler, which needs no V8 handler layout. `on_web_kit_initialized` and `on_context_created` are both available on this branch; which one the shim uses is an outcome of this step, not a decision taken in advance. Set `remote_debugging_port` in the forwarded settings at the same time, because that turns every later gate into something observable rather than inferred.
2. **M4 — prove the anchors are live.** Confirm that the registry and the player-URL dispatcher read from `web.pack` are the ones present at runtime, and record interception of a real response without modifying it.
3. **M6 — answer the async question.** Return a mocked result after a controlled delay, sweep the delay, and establish the budget at which the dispatcher's three-attempt retry fires or the UI degrades. That number is the matcher's timeout.

The header set is pinned and needs no further work: [include/ncm/cef/abi_1916.hpp](../../include/ncm/cef/abi_1916.hpp) transcribes the four structures this project dereferences from upstream branch `1916`, leaves every other CEF object incomplete so a member access cannot compile, guards the member counts with layout assertions, and carries the hash pair that gates it. The command-line debugging endpoint is closed and is not retried; an endpoint remains available only as a by-product of step 1.

M5 may run alongside M4 once interception is established, because a synchronous privilege patch needs no native call and independently confirms that patching the business-layer object is honored by the UI.

Do not substitute the falsified root flag, the falsified environment variables, the closed private-configuration path, the closed `.def` forwarder, private NCM RVAs, fragile coordinate automation, system proxy, or certificate trust for the required implementation.
