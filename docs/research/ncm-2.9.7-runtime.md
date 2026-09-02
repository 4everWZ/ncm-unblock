# NCM 2.9.7 Runtime Investigation

This report records reproducible investigation evidence. It is not the product contract; behavioral requirements remain in the [canonical specification](../specs/ncm-unblock-297.md).

## Evidence boundary

- **Target observed:** NetEase Cloud Music `2.9.7.199711` at `D:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe` on Windows.
- **Snapshot date:** 2026-09-01.
- **Methods:** The repository's Win32 `ncm_runtime_probe` and bounded loopback proxy observer, Visual Studio `dumpbin`, Windows Authenticode verification, process/module snapshots, process command lines, IPv4 TCP ownership inspection, local client-state inspection, read-only structural inspection of the shipped and hot-updated frontend packages, and upstream primary sources.
- **Controlled scope:** No installed target files, NCM proxy settings, system proxy values, or certificates were changed. Controlled NCM instances were launched and, after normal close only minimized to the tray, their recorded experiment processes were terminated under explicit authorization. Private `localdata` was snapshotted outside the repository and fully restored. The frontend packages were read without being modified, extracted into the repository, or written back. No payload content or credentials were captured or forwarded.
- **Not established:** Playback-request routing, HTTP/HTTPS proxy behavior, certificate requirements, UNM compatibility, stable loader behavior, performance, or whether the frontend tolerates an asynchronously resolved native callback.

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

This supports the narrow hypothesis that current client network activity is brokered by the root process. It does not identify which stack carries playback requests, because shared module presence is not call-path evidence and the snapshot was not synchronized to a play action. The in-process census below is the experiment that attempts that attribution.

## Loader hypotheses

- `winmm.dll` and `shlwapi.dll` are direct imports and therefore candidates for controlled DLL search/load-order investigation.
- On this host, the 32-bit loader's KnownDLL registry includes `SHLWAPI.dll` but not `WINMM.dll`. The exact root imports `timeGetTime`; `cloudmusic.dll` imports `PlaySoundW`, timer-period functions, and wave-output functions from WinMM. This removes Shlwapi from the local same-directory experiment and makes WinMM the narrower candidate, but does not prove that NCM accepts an application-directory WinMM or that a forwarding proxy is safe.
- A direct import is not evidence that a same-directory proxy is safe. Export completeness, KnownDLL behavior, load order, signature/integrity checks, child-process effects, and clean forwarding remain untested.
- `msimg32.dll` and other loaders mentioned by reference projects are not imports of the root executable and are not candidates without evidence from another early-loaded module.
- No hook library is justified by current evidence.

### Pre-entry WinMM load experiment

The dedicated Win32 investigation probe launched only isolated signed copies of the exact 2.9.7.199711 PE32 root under `DEBUG_ONLY_THIS_PROCESS`. It pinned the validated target and expected module without write/delete sharing, matched their volume/file IDs against the `CREATE_PROCESS` and `LOAD_DLL` event handles, assigned each root to a private kill-on-close Job before continuing the first debug event, and terminated the Job while the matching event was still stopped. Synthetic fixtures separately proved that neither the candidate DLL initializer nor the executable entry point ran on success, unexpected-identity failure, or bounded event-limit failure.

On this host, the control copy without a local candidate loaded `C:\Windows\SysWOW64\winmm.dll` at debug event 20. The treatment copy placed the same validly signed x86 system WinMM beside the isolated executable and loaded that application-directory copy at debug event 20. Both exact process trees exited and the allowlisted temporary material was removed. No installed file, NCM user state, system proxy, or certificate state was changed.

This is positive evidence that the current Windows build, x86 loader policy, and exact NCM root accept an application-directory WinMM before DLL initialization or application entry. It is not evidence that a custom forwarder is export-complete, that full NCM startup from an isolated directory is supported, that an installed-directory change is acceptable, or that a WinMM-loaded component can safely establish the required proxy routing. Those remain separate design and runtime gates.

### WinMM export surface

`dumpbin /exports C:\Windows\SysWOW64\winmm.dll` on this host (`10.0.26100.8972`) reports ordinal base 2, 193 functions, and 192 names. Every ordinal in `2`–`194` is populated with no holes, the single unnamed export is ordinal 2, and its RVA is the same entry point as the named `PlaySound` at ordinal 11. No entry is a PE forwarder. Offline static inspection of the root executable and the conditionally loaded NCM/CEF module closure found 48 distinct imported WinMM names and no ordinal imports.

A proxy limited to the 48 currently imported names would therefore link against today's static imports while silently breaking any `GetProcAddress` name lookup, any ordinal lookup, and the unnamed ordinal 2 alias. Export parity for a candidate proxy means all 193 ordinal slots, all 192 names, and the unnamed ordinal 2 — not the observed import subset. This inventory is pinned to one host build in `src/winmm_proxy/winmm.exports`; a supported-build portability rule is still an open design question.

### Synthetic export-parity and backend experiment

The manifest generates a proxy, a synthetic backend, and a `.def`-forwarder negative control. All three are named `winmm.dll`. `dumpbin /exports` reports ordinal base 2, 193 functions, and 192 names for the proxy and the backend, and the proxy's ordinal 2 is an unnamed entry pointing at its own thunk rather than a forwarder string.

A probe process runs from a staged directory that holds the proxy beside the executable, reproducing the position a proxy would occupy beside `cloudmusic.exe`. In that process the proxy took ownership of the `winmm.dll` base name, resolved its backend by absolute path, and `LoadLibraryW` of a same-base-name backend in a different directory returned a distinct module. All 193 entries resolved through the proxy by name or by ordinal, dispatched into the backend, and returned the fixture's contract value; the measured stack-pointer delta was zero for every call, so the bare `jmp` thunks preserved x86 `__stdcall` callee cleanup. The unnamed ordinal 2 and `PlaySound` resolved to one shared backend entry point, so the alias survived.

The negative control fails twice, independently.

- At link time, `link.exe` resolves a `.def` forwarder target through an import library keyed by name. Against the Windows SDK `winmm.lib`, 191 of the 193 entries bound; `mmsystemGetVersion` and the unnamed ordinal 2 did not. An unnamed ordinal has no name key and cannot be expressed as a forwarder at all, so the control ships 192 entries at ordinal base 3 and can never reproduce the pinned surface.
- At run time, with the control owning the `winmm.dll` base name in the probe's application directory, all 193 lookups returned null: none resolved into the control itself and none reached a separate backend. The control carries real `WINMM.<entry>` forwarder strings and imports only `KERNEL32.dll`, so the failure is the loader refusing a circular forwarder, not a missing dependency.

This closes the `.def` forwarder route on measured evidence and establishes runtime resolution by absolute path as the working mechanism. Resolution is all-or-nothing: a probe pointed at an absent backend stopped with the proxy's documented exit code before any thunk dispatched, so a partial or substituted surface is never forwarded. Its boundary is narrow: every module here is repository-built, no system WinMM was proxied, no NCM process was started, and nothing was written into an installation. Portability of the pinned surface across Windows builds, loader-lock-safe bootstrap triggering, and real-client startup remain untested.

Renaming and redistributing the host system WinMM as a differently named backend stays rejected for reasons this experiment does not touch: module identity, OS servicing and build coupling, its own dependency closure, and redistribution terms.

### Production proxy against the host system module

`tools/probe-winmm-system-backend.ps1` stages the release proxy beside a copy of the probe in a private directory and resolves it against this host's real WinMM. All 193 pinned entries are exported by the proxy and present in the host module, the two modules are distinct, and a forwarded `mmsystemGetVersion` returned the same value as the direct call. Because resolution is all-or-nothing, one successful forwarded call establishes that every pinned entry resolved.

Only `mmsystemGetVersion` is called: it takes no arguments and returns a constant, while most WinMM entry points have device or timer side effects that a probe must not trigger with fabricated arguments.

The proxy resolves `GetSystemDirectoryW`, which a 32-bit process reports as `C:\WINDOWS\system32`; WOW64 file-system redirection resolves that to the SysWOW64 binary, and the loaded module path matches the requested string exactly. A caller that disabled redirection would reach the 64-bit image, where the load fails and the proxy stops the process rather than forwarding a partial surface.

### Isolated full-start experiment

`tools/probe-ncm-winmm-proxy.ps1` copies the installed tree into a private directory, places a proxy beside the isolated `cloudmusic.exe`, starts it, and reclaims only processes whose image lives under that directory. The installed directory is never written to; it still contains 153 files and no `winmm.dll`, and `localdata` is unchanged.

Two paired runs on this host:

| Run | Proxy | Backend | Root exit | Main window |
|---|---|---|---|---|
| A | release proxy | host system WinMM | none within 30 s | reached |
| B | fixture proxy | a path that does not exist | `0xE0C40001` | never reached |

Run B is the positive control. `0xE0C40001` is the proxy's own fail-closed code, so the exact client loaded the application-directory `winmm.dll` and called through a thunk. Run A differs only in whether the backend resolves, so its normal startup is attributable to correct forwarding rather than to the proxy being ignored. Run A needed a bounded forced close because the client minimizes to the tray instead of exiting.

Method boundary: module presence is not the evidence here. `Process.Modules` reports 7 modules for the WOW64 root from a 64-bit host, far fewer than it loads, so cross-bitness enumeration cannot confirm which `winmm.dll` is mapped; the exit code can.

Isolation boundary: redirecting `%LOCALAPPDATA%` for the child does not contain this client, which read the real user profile and music library. Client-local state is instead protected by refusing to run while any client process exists, taking a private pre-run copy of `localdata`, and verifying it afterwards; every run left it byte-identical. Registry writes under `HKCU\Software\Netease` are outside the boundary.

This clears the isolated-start gate for a pure forwarder. It does not establish playback, request routing, long-run stability, or any bootstrap behavior: the proxy still does nothing but forward.

### Loader-lock handoff and host-surface verification

The proxy's `DllMain` only disables thread notifications and hands a body to a private thread. Windows does not run a thread created during `DLL_PROCESS_ATTACH` until the loader lock is released, so the body cannot execute inside the notification that scheduled it. A focused test blocks the body on an event and asserts that scheduling returns promptly, that the body has not finished while blocked, that its ordering ticket is later than one taken after scheduling returned, and that it ran on another thread; an implementation that inlined the work would fail on the budget rather than pass by chance.

The body resolves the backend itself. Doing it there rather than leaving it to the first thunk keeps the module load off whichever thread the host happens to call WinMM from, which is the one place the forwarder would otherwise call `LoadLibrary` while another module holds the loader lock. A probe that loads the proxy and calls nothing observes the backend appear in the process, so the bootstrap reaches it unaided.

It then compares the backend's export directory against the shape the manifest pins. Resolution already proves every pinned entry exists in the host, so an equal ordinal base, function count, and name count is what remains to establish that the two surfaces are the same. The repository's backend fixture reports the pinned `2/193/192`; the negative control, which is the same manifest minus the entry a `.def` forwarder cannot express, reports `3/192/192` and is classified as different.

Re-running both isolated client runs with the handoff active reproduced the earlier outcomes: the release proxy reaches the main window, and the unresolvable fixture stops the client with `0xE0C40001`. The control now proves that the client loaded the proxy and ran its bootstrap; it no longer isolates a forwarded call, because the bootstrap resolves without waiting for one. The call-through evidence remains the pre-handoff pair recorded above, where only a thunk could have triggered resolution.

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

## Playback traffic attribution

### Changed claim

One of `libcurl`, `WinHTTP`, `WinINet`, or the CEF/Chromium network stack carries NCM 2.9.7 playback requests, and the carrying stack can be named from module-load behavior observed inside the client rather than from module presence.

The existing evidence cannot decide this. The root, GPU, and renderer processes all map `WINHTTP.dll`, `WININET.dll`, `libcurl.dll`, and `libcef.dll`, so presence is not attribution, and the earlier snapshot was not synchronized to a play action. A 64-bit observer also reads only 7 modules from the WOW64 root, so the enumeration itself was never complete.

### Mechanism

The proxy already executes inside the exact x86 client, which is the missing 32-bit vantage point. A census-flavored `winmm.dll` carries the same generated thunks, the same forwarder, and the same absolute-system-path backend as the release proxy, and differs only in the bootstrap body it schedules. Staging it therefore changes the observation and nothing else about the load boundary.

The census registers `LdrRegisterDllNotification` and then snapshots the already-mapped modules. Registration comes first on purpose: a module mapped between the two steps is reported twice rather than missed, and a duplicate is visible in the report while a gap would not be. Every isolated process in the tree writes its own timeline, tagged with the role read from its `--type=` switch, so root, renderer, GPU, and utility processes are separated rather than merged.

The loader callback runs with the loader lock held, so the recording path allocates nothing, loads nothing, and writes no file: it classifies the base name against a fixed allowlist, claims a slot in fixed storage with one interlocked increment, and publishes with a second counter. Flushing the report is left to the census thread on a bounded window. That polling flush is investigation tooling and is not the event-driven target design.

### Discriminator

Each recorded transition carries elapsed milliseconds since the census started, so the timeline orders network-stack loads against `audio_render` loads (`audioses.dll`, `mmdevapi.dll`, `audioeng.dll`, `avrt.dll`, `wdmaud.drv`, `msacm32.dll`, `ksuser.dll`, `xaudio2*`), which stand in for the moment playback actually begins.

- A stack that maps lazily, in the root process, immediately before the first `audio_render` load is the strongest available attribution short of call-level tracing.
- A stack already present in the startup snapshot of every process is not attributed by this experiment at all.

### Falsifier

If every candidate stack appears in the startup snapshot and no candidate loads near the first `audio_render` transition, then module-load timing does not attribute playback and this experiment has failed on its own terms. The recorded outcome is then "inconclusive", not a default to any stack, and attribution has to escalate to a call-level or differential method — the next cheapest being a process-local `http_proxy`/`https_proxy` environment differential, which only an environment-reading stack such as libcurl would honor and which changes no system or client-persistent state.

### Safety

The census records allowlisted module base names and fixed classifications only; it never emits full paths, request targets, headers, or credentials. It routes nothing and is not part of the release surface. It runs only through the isolated-copy probe, which refuses to start while any client process exists, redirects `%LOCALAPPDATA%` into a private directory, verifies the real `localdata` unchanged, and force-closes only processes whose image lives under that run's private directory. Reports are written outside the private run directory so they survive its cleanup.

### Status

The census module, the census proxy, and the probe mode are implemented and covered by focused tests: the allowlist classifies by family and rejects a longer name that merely starts with an exact rule; a lazy `winhttp.dll` load is captured as a post-snapshot event and attributed; and the report carries the process role, the timeline, and totals with no drops inside its fixed capacity.

A 600-second run against an isolated copy of the exact client returned the pre-registered inconclusive branch. Four timelines were written — one root, two renderer, one GPU — and the root recorded 20 classified loads out of 167 observed modules with no drops, flushing its final report at the window deadline. Every candidate stack was already mapped in the root within the first 1.25 seconds: winsock (`WS2_32`, `NSI`, `mswsock`), WinHTTP, WinINet (`WININET`, `urlmon`), Schannel (`SspiCli`, `Secur32`, `schannel`, `ncrypt`, `ncryptsslp`), libcurl, and CEF (`libcef`). The `audio_render` family appears at 609–1250 ms as startup device initialization rather than as first playback, so the intended anchor is unusable. Sign-in and playback occupied the remaining 599 seconds and produced no further classified load in any process, and no OpenSSL-family module mapped anywhere.

The experiment therefore failed on its own terms and no stack is attributed. Module-load timing carries no playback signal in this client, and no further module-load observation can supply one, because every candidate is resident before the first track plays. The census is retained as the 32-bit module inventory it does establish. The escalation it names — a process-local environment-proxy differential — was run and is recorded above.

## Embedded browser and frontend layer

### Changed claim

NCM 2.9.7 resolves playable track URLs through business-layer JavaScript running in its own embedded browser, so the response can be observed and patched in an already-decrypted object without touching HTTP, TLS, or EAPI. This section records what static inspection establishes and what it does not.

### CEF module

`libcef.dll` in the installed tree reports file and product version `3.1916.1900` with a `2018-03-15` export time stamp. Branch 1916 corresponds to Chromium 35, which matches the `Chrome/35.0.1916.157` product token already observed on the client's own command lines. The module exports 149 symbols at ordinal base 1.

The exported surface includes the pieces a native integration would use:

| Capability | Exports |
|---|---|
| Process entry and lifecycle | `cef_execute_process`, `cef_initialize`, `cef_shutdown`, `cef_run_message_loop`, `cef_quit_message_loop`, `cef_do_message_loop_work` |
| JavaScript extension registration | `cef_register_extension` |
| V8 value and context construction | `cef_v8context_get_current_context`, `cef_v8context_get_entered_context`, `cef_v8context_in_context`, `cef_v8value_create_function`, `cef_v8value_create_object`, `cef_v8value_create_array`, `cef_v8value_create_string`, and the remaining `cef_v8value_create_*` constructors |
| Cross-process messaging | `cef_process_message_create` |
| Thread marshalling | `cef_post_task`, `cef_post_delayed_task`, `cef_currently_on`, `cef_task_runner_get_for_thread`, `cef_task_runner_get_for_current_thread` |
| Browser creation | `cef_browser_host_create_browser`, `cef_browser_host_create_browser_sync` |
| Scheme and request handling | `cef_register_scheme_handler_factory`, `cef_urlrequest_create`, `cef_request_create`, `cef_response_create` |
| Version identification | `cef_version_info`, `cef_build_revision`, `cef_api_hash` |

Two consequences follow. First, a native integration does not have to hook a CEF callback struct to inject JavaScript: `cef_register_extension` is an exported free function that installs an extension into every V8 context. Second, the reference Windows injectors are not transferable. Verdant and BetterNCM target CEF 90 and later, where the C API struct layouts, member ordering, and callback slots differ from branch 1916; their hook offsets and struct definitions carry no validity here.

The client is further removed from a stock CEF release than the version alone implies. Debug path strings inside `cloudmusic.dll` place the build under `orpheus\src\third_party\partial-chromium\src\`, with `base/`, `net/`, `crypto/`, `url/`, `net/spdy/`, `net/quic/`, and `net/socket/` subtrees. NetEase ships a reduced, privately modified Chromium rather than an unmodified upstream one, so published CEF 3.1916 headers are a starting point for struct layouts and not an authority.

### Measured API revision

`ncm_cef_probe` loads the client's own `libcef.dll` by absolute path and reads what the module reports about itself. Against the installed target:

| Property | Value |
|---|---|
| Argument cleanup | caller-cleans (`__cdecl`), stack delta `-4` |
| `cef_api_hash(0)` platform | `78d4b4eb20e36e2b08572b98645dde08e987fbad` |
| `cef_api_hash(1)` universal | `ce45d134468cd9bad310409c96e5108d75fac3c7` |
| `cef_api_hash(2)` commit | unavailable |
| `cef_build_revision()` | `1900` |
| `cef_version_info` 0–5 | CEF major `3`, CEF revision `1900`, Chrome `35.0.1916.157` |
| Required entry points absent | `0` of 14 |

The version string `3.1916.1900` decomposes as CEF major 3, Chromium build 1916, CEF revision 1900, which the individual `cef_version_info` entries confirm rather than merely restate.

The calling convention is measured, not assumed. The probe pushes one argument, compares the stack pointer across the call, and classifies a four-byte shortfall as caller-cleanup and a restored pointer as callee-cleanup; a delta that disagrees between calls fails the read outright. The same source is built as a `__cdecl` fixture and a `__stdcall` fixture, so a probe that hardcoded either answer would fail against the other. This matters because a wrong convention against a real module corrupts the caller's stack silently rather than returning a wrong value.

The two API hashes are the durable identity of this build's C API surface. Any struct layout the project later relies on must be reconciled against them, and the bootstrap must re-read them at startup and decline rather than proceed if they differ from the pinned pair. The commit hash is empty on this build, so it supplies no additional identity.

### The C API surface is stock

`cef_version.h` is generated at build time and is not committed to the CEF repository, so the comparison is against published built distributions of the same branch. Two independently vendored CEF 3.1916 distributions define:

```c
#define CEF_API_HASH_UNIVERSAL "ce45d134468cd9bad310409c96e5108d75fac3c7"
#define CEF_API_HASH_PLATFORM  "78d4b4eb20e36e2b08572b98645dde08e987fbad"  /* OS_WIN */
```

Both match the client's module exactly, while their version constants do not: those distributions carry `CEF_REVISION` 1721 and 1781 over Chromium 35.0.1916.86 and 35.0.1916.138, against the client's revision 1900 over 35.0.1916.157. The client is therefore a later CEF revision on the same branch, and the identical hashes are the branch's C API surface being unchanged across those revisions — which is precisely what an API hash asserts.

NetEase's `partial-chromium` modifications are therefore internal. They do not reach the CEF C boundary, and published CEF 3.1916 headers describe this module's struct layouts. This closes the question that gated every native design decision: struct layouts may be taken from that header set, provided the bootstrap asserts the hash pair at startup and declines when it differs.

### Remote debugging is not reachable from the command line

An isolated run of the exact client with a process-local `--remote-debugging-port` opened no endpoint within 60 seconds, so no debugging endpoint is available to explore the live frontend with. This is consistent with CEF3, where the DevTools HTTP server is started from `CefSettings.remote_debugging_port` and settings are converted into command-line switches for the browser process rather than read back from them. It matches the earlier finding that a root `--proxy-server` switch appears on the command line without taking effect.

The consequence is that live frontend exploration depends on the same native hook that injection does. The converse is also true and useful: code that wraps the client's `cef_app_t` in order to inject can set `remote_debugging_port` in the settings it forwards, so a development-time endpoint costs nothing extra once that hook exists.

### Exact-client extension registration

The production bootstrap now waits outside loader lock for the loaded `cloudmusic.dll` and `libcef.dll`, repeats both pinned API hashes, resolves `cef_execute_process` and `cef_register_extension`, and replaces only the named ordinary `cef_execute_process` IAT slot. Its wrapper preserves the client's application and render-handler callbacks and registers the fixed null-handler M3 extension after the client's own `on_web_kit_initialized` callback. Synthetic PE32 fixtures prove successful replacement and hash-mismatch refusal before this path is allowed near the target.

A fresh isolated run against the signed `2.9.7.199711` executable produced a main window and four live client processes. Three fixed-classification reports recorded `hook=installed`; one renderer recorded `registration=succeeded`, proving that the client called the wrapped render callback and its own CEF accepted the extension name and source. The root and another process remained `registration=not_attempted`, as expected for roles that did not reach the renderer callback during the observation window. The run reclaimed only the four processes whose executable paths were under the private experiment root, verified the real `localdata` fingerprint unchanged, and removed the experiment directory.

This result proves live IAT interception, callback forwarding, and successful extension registration. It does not directly observe the marker inside a created V8 context, so it does not yet prove extension-code execution or clear M3. A size-checked `cef_initialize` settings copy that enables a private debugging endpoint is the next observation mechanism.

The same strings identify `orpheus` as the native application framework, not the frontend: `orpheus_runner.cpp`, `orpheus_starter.cpp`, `orpheus_thread_manager.cpp`, `orpheus::OrpheusPostTask`, `orpheus::AppConfig::SaveConfigAsync`, and the switches `orpheus-startup`, `orpheus-restart-for-update`, and `orpheus-allow-mutiapp-run`. A privately maintained Chromium `net/` stack compiled into `cloudmusic.dll` is consistent with the earlier finding that neither ambient proxy environment variables nor a root `--proxy-server` switch routes client traffic.

### Frontend package format

The installed `package\orpheus.ntpk` is 19,332,902 bytes. It is not the sibling `NTPK`-magic container used by `package\native.ntpk`: the first 100 bytes are a header consisting of 37 ASCII hexadecimal characters followed by 63 bytes of binary, and a standard ZIP local file header begins at offset 100. The archive holds 2,601 entries, uses ordinary deflate, and sets no encryption flag, so entry names and contents are readable without any key.

The layout is a hybrid of two application generations under `pub/`: a legacy NEJ-based main UI (`pub/core.js`, `pub/app.html`, and 178 `pub/module/**/index.html` pages) alongside a newer webpack build under `pub/hybrid/` carrying React 16.14, `@cloudmusic-desktop`, and split chunks named `app.chunk.js`, `login.chunk.js`, `runtime~*.bundle.js`, and `vendors~*.chunk.js`.

Pages are served to the embedded browser over a private scheme rather than from disk or the network. `pub/core.js` sets `webroot:"orpheus://orpheus/pub/"` and builds resource URLs such as `orpheus://orpheus/style/res/`. The client's own storage confirms the resulting origin: `%LOCALAPPDATA%\Netease\CloudMusic\webapp\Local Storage\orpheus_orpheus_0.localstorage`. This is the consumer of the exported `cef_register_scheme_handler_factory`.

### The frontend hot-updates independently of the client version

The installed `package\orpheus.ntpk` is dated 2022-01-25, but `%LOCALAPPDATA%\Netease\CloudMusic\web.pack` is dated 2025-09-09, is 19,332,799 bytes, has the identical 100-byte-header-plus-ZIP structure and the same 2,601 entries, and is the package that actually loads. Frontend evidence must therefore be read from `web.pack`, and the pinned client version does not pin the frontend.

Comparing the two packages shows exactly which anchors survive an update. Minified identifiers drift: the same player-URL request method is `ba.dnn` calling `this.bTY(bo)` and `this.dno.bh(this,cJ)` in the shipped package, and `ba.dno` calling `this.bTZ(bo)` and `this.dnp.bh(this,cJ)` in the loaded one. String literals, framework entry points, and API field names are unchanged across the pair.

The design rule this implies is that a shim may anchor only on stable artifacts — request path literals, the NEJ module registry, and response field names — and never on a minified identifier, because a hook bound to one would fail silently at the next frontend update rather than at a client upgrade.

### Framework and hook anchors

Measured against `pub/core.js` in the loaded `web.pack` (2,251,126 bytes):

| Anchor | Occurrences | Status |
|---|---|---|
| `window.nej` | 0 | absent |
| `window.nm` | 0 | absent |
| `window.NEJ` | 3 | present |
| `NEJ.P` | 326 | present |
| `nej.ut.j` | 17 | present |
| `privilege` | 182 | present |
| `/api/song/enhance/player/url` | 3 | present |
| `orpheus://` | 53 | present |

The globals named by the upstream browser port do not exist, but the framework behind them does. Modules are resolved through the registry rather than through a global object, in the form `var bf=NEJ.P, ep=bf("nej.h"), bY=bf("nej.ut.j")`. The `nej.h` module supplies the transport factory `ep.cXs=function(){return new XMLHttpRequest}`. The upstream hook target `window.nej.j` therefore has an equivalent here, reached as `NEJ.P('nej.ut.j')`, and the upstream port's specific expression of it does not apply unchanged.

### Player URL request site

All three `/api/song/enhance/player/url` references in the loaded package go through one dispatcher shape, `bc.hb(path, params, callback)`:

```js
bc.hb("/api/song/enhance/player/url",
      {ids:[JSON.stringify(this.bTZ(bo))], br: iY*1e3},
      this.bpo.bh(this, ca, bo, jN, iY, cJ, {time:3}))
```

The callback treats a response as usable when `bm.code==200 && bm.data && bm.data[0] && bm.data[0].code==200`, and retries up to three times while `bm.code==fS.Lp && !bm.data && !bm.rawdata`. Two adjacent endpoints exist and are separate concerns: `/api/event/enhance/player/url` for event audio and `/api/cloudvideo/playurl` for video.

Because the callback reads `bm.code` and `bm.data[0]` as ordinary object members, the response reaching this layer is already decrypted, whatever EAPI handling occurs beneath it. This is the property that makes a business-layer patch possible without reimplementing EAPI, TLS interception, or a proxy.

### Evidence boundary

The package analysis in this section is static structural inspection of two package files. It establishes that the anchors exist in the code that ships and in the code that loads. The isolated experiment separately proves that `cef_register_extension` succeeds on this build, but does not establish that its marker executes in a V8 context, that the frontend anchors are live when the shim runs, that a native callback may resolve asynchronously without the retry path firing or the UI timing out, or that a patched response is honored. No package was modified.

## Upstream v0.28.0 checkpoint

The official [v0.28.0 release](https://github.com/UnblockNeteaseMusic/server/releases/tag/v0.28.0) provides Windows x64 and ARM64 standalone executables, but no x86 executable. The sidecar therefore follows OS architecture rather than the x86 client architecture. Its official [build workflow](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/.github/workflows/build-binaries.yml) uses `pkg` Node 18 targets, so the standalone contains its runtime and does not require a separately installed Node toolchain.

The official [CLI and startup code](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/src/app.js) uses separate HTTP/HTTPS ports, defaults to `8080:8081`, and does not make loopback binding the safe default. A launcher candidate must explicitly pass `-a 127.0.0.1`, an HTTP/HTTPS port pair, and `-s`. There is no dedicated health endpoint: a live child, both listening sockets, and the [HTTP `/proxy.pac` response](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/src/server.js) establish initialization only, not provider or playback health.

HTTPS is currently blocked at the trust-design level. The upstream [CONNECT path](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/src/hook.js#L515-L533) redirects selected tunnels to its HTTPS listener. The bundled [server certificate](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/server.crt) expired on 2026-07-24, before this investigation, and its default private key is public. Although custom certificate paths are supported, the project has not accepted a CA/leaf generation, per-user trust, renewal, removal, or recovery design. No certificate was installed.

The project declares `LGPL-3.0-only` in its [package metadata](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/package.json) and distributes [GPLv3](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/COPYING) and [LGPLv3](https://github.com/UnblockNeteaseMusic/server/blob/v0.28.0/COPYING.LESSER) texts. Before redistribution, this repository still needs a corresponding-source route plus a license/notice audit for the embedded Node runtime and bundled dependencies. No upstream executable was downloaded, executed, or approved for redistribution in this checkpoint.

## Remaining experiments

The frontend evidence above redirects the open questions from the network layer to the embedded browser. These are ordered so that each one can falsify the business-layer approach before the next is built.

1. Pin the actual CEF API revision by calling `cef_api_hash`, `cef_version_info`, and `cef_build_revision` from an x86 harness, and reconcile the result against published CEF 3.1916 headers before any struct layout is relied on.
2. Establish that `cef_register_extension` with a null handler injects JavaScript into this client's contexts, which is the smallest injection that depends on no struct layout at all.
3. Establish that the injected context can reach the framework registry and the player-URL dispatcher at the time the shim runs. An extension executes before page scripts, so the shim has to defer until the registry exists; which deferral is available is unmeasured.
4. Establish whether a native callback may resolve asynchronously without the dispatcher's three-attempt retry firing or the UI timing out. This is the load-bearing unknown for the whole architecture and nothing beyond a mocked matcher should be built before it is answered.
5. Only then attach a native matcher, and only then extend an isolated run from "the client reaches its UI" to search, normal-track play, unavailable-track play, and track changes.

These remain recorded but are demoted to the fallback path, and are not on the critical path while the business-layer route is open:

- Identify how 2.9.7 persists its client-local proxy state and prove an exact read/write/rollback cycle without copying private values into the repository.
- Route a controlled, non-sensitive request through the validated loopback observer to distinguish whether 2.9.7 honors its HTTP proxy for HTTPS destinations and whether it uses `CONNECT`.
- Repeat connection ownership snapshots around search, normal-track play, unavailable-track play, and track changes.
- Pin an upstream UNM Windows artifact only after verifying authenticity, target compatibility, certificate lifecycle, corresponding source, and bundled dependency notices in addition to its already-audited source, version, architecture, CLI, and initialization behavior.

Loader-candidate inspection is closed: the WinMM boundary is verified in the exact client with a positive control, and it carries the bootstrap for either path.
