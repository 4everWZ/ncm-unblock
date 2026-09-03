# NCM Unblock 2.9.7 Specification

## Intent

- **Product:** A portable, lightweight native Win32 launcher for NetEase Cloud Music (NCM) 2.9.7.199711. It starts an upstream UnblockNeteaseMusic (UNM) standalone executable, waits until the proxy is ready, starts NCM, and stops UNM after the complete NCM session exits.
- **MVP user experience:** The user configures NCM's built-in custom HTTP proxy once, then launches NCM through `ncm-unblock.exe`. The launcher and sidecar have no visible console during normal use and leave no resident service or orphan process.
- **In scope:** C++20/Win32 launcher and configuration; fixed loopback ports; sidecar process ownership, readiness, logging, and cleanup; NCM start and session-end detection; portable packaging; compatibility, recovery, and performance evidence.
- **Out of scope:** DLL proxy deployment, code injection, CEF/V8 hooks, IAT or inline patching, a native provider matcher, frontend reverse engineering, direct modification of NCM `localdata`, system proxy changes, certificate installation, a Windows service, automatic NCM restart, and a settings GUI.

UNM remains the business core for privilege and player-URL handling and alternate-source matching. The launcher does not reimplement provider APIs. Prior injection research is preserved on `research/native-injection` and is not part of the production build.

## Runtime contract

### Components and boundaries

- Release-owned code is native C++20, built with CMake and Visual Studio for Win32.
- Normal runtime consists of `ncm-unblock.exe`, `cloudmusic.exe` and its own child processes, and one independently replaceable UNM standalone executable.
- A standalone UNM executable may contain its own Node runtime. The product does not install or ship a separate Node toolchain, package manager, `node_modules`, or upstream checkout, and Node never runs inside `cloudmusic.exe`.
- The launcher changes neither system proxy nor certificate trust. It does not decode, log, or edit `%LOCALAPPDATA%\Netease\CloudMusic\localdata`.

### Configuration

The portable configuration is human-readable and stored beside the launcher. It provides at least:

```ini
[ncm]
path = C:\Program Files (x86)\Netease\CloudMusic\cloudmusic.exe

[unm]
path = core\unblockneteasemusic.exe
http_port = 3412
https_port = 3413
# Leave sources empty to use the pinned UNM release's own defaults.
sources =

[launcher]
write_log = true
```

- HTTP and HTTPS ports are explicit and stable so NCM's saved proxy setting does not change between runs. The launcher fails clearly if either configured port is unavailable; it does not silently select another port.
- When no source list is configured, the launcher omits `-o` and uses the pinned UNM release's own defaults. An explicit source list is passed to UNM in configured order. The launcher does not interpret provider APIs or assume that a historically available provider is still a valid default.
- Injection-only CEF, WinMM, shim, frontend, and matcher settings are invalid for the production configuration.

### First-time NCM setup

The user configures NCM 2.9.7 through its supported UI at **Settings → Tools → HTTP proxy → Custom proxy**, using `127.0.0.1` and the configured HTTP port. The launcher does not automate or bypass this settings UI in the MVP.

### Launch sequence

1. Validate the configuration, exact NCM executable/version, UNM executable, configured ports, and relevant existing processes.
2. If a matching NCM session is already running for the current user and target installation, fail with an instruction to exit NCM completely. The MVP does not attach to an existing session.
3. Reserve both configured ports exclusively on `127.0.0.1`.
4. Prepare UNM suspended, assign it to a private Job Object with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, redirect stdout/stderr to `logs/unm.log`, release the port leases immediately before resume, and start it without a visible window.
5. Invoke the pinned UNM interface with explicit loopback binding, the configured HTTP/HTTPS pair, and restricted mode. Append `-o SOURCES` only for an explicit non-empty source list; otherwise preserve the pinned release's defaults.
6. Declare readiness only while the managed UNM tree remains alive, both configured listeners are owned by processes in its private job, and `/proxy.pac` returns a complete valid response. Readiness is proxy initialization, not provider or playback health.
7. Start `cloudmusic.exe` normally with `CreateProcessW`. Do not inject, suspend NCM, or add ambient proxy flags; NCM uses its saved custom-proxy setting.

### Ownership and session end

- The launcher records the target executable path, root PID, and start time. It identifies a session by process identity and target path, never by image name alone.
- Closing the main window to the tray is not session end. The sidecar remains alive while any process belonging to the launched target NCM session remains.
- The MVP may use process handles, waitable events, and a low-frequency bounded process census (approximately every one to two seconds) where NCM's multi-process behavior requires it. High-frequency polling is excluded.
- When the complete NCM session ends or NCM crashes with no session process remaining, the launcher requests bounded graceful UNM shutdown where supported, terminates the private job if needed, verifies the tree is empty and ports are released, then exits. It does not restart NCM.
- If UNM exits unexpectedly, the launcher reports the failure and shuts down cleanly. The MVP does not use an unbounded restart loop.
- Closing or crashing the launcher closes the kill-on-close job so its UNM process tree cannot remain resident. NCM remains outside that job and is not terminated merely because the launcher fails. Cleanup is limited to launcher-owned sidecar processes; pre-existing or merely same-named processes are never terminated.

### Logging and privacy

- Normal logs are limited to `launcher.log` and redirected `unm.log`.
- Launcher records timestamps, launcher/NCM/UNM versions, startup and readiness results, owned PIDs, session end, and exit codes.
- Logs exclude credentials, cookies, headers, complete private requests, and decoded NCM user state.

### Packaging

- The first release is portable: launcher, configuration, documentation, and an optional `core/` artifact whose upstream identity, pinned version, license, corresponding-source obligations, architecture, runtime interface, and redistribution terms have been verified.
- An installer and shortcut replacement are post-MVP. Any later shortcut may target `ncm-unblock.exe` while using the NCM icon and must support reversible uninstall/restoration.

## Acceptance

| Observable requirement | Verification method |
|---|---|
| Invalid paths, wrong NCM version, occupied fixed ports, and an existing NCM session fail before mutation | Focused configuration and launch-preflight tests |
| UNM is job-owned, hidden, logged, and ready before NCM starts | Integration tests for listener ownership, PAC response, timeout, early exit, and log redirection |
| A configured NCM 2.9.7 session can search and play normal and UNM-supported unavailable tracks | Versioned compatibility run using the supported NCM proxy UI and pinned UNM artifact |
| Closing NCM to the tray keeps UNM alive; choosing NCM's real exit ends UNM and the launcher | Exact-client lifecycle run with path/PID evidence |
| NCM or UNM crashes, bad configuration, and port collision fail boundedly without a sidecar orphan; launcher failure reclaims its UNM tree without terminating NCM or unrelated processes | Failure-recovery integration matrix |
| Unrelated processes, system proxy, certificate trust, NCM installation, and private `localdata` remain unchanged | Negative process tests and bounded before/after inspection |
| Release contents require no separate Node installation, service, injected DLL, or development tree | Packaging manifest inspection |
| CPU, startup latency, RSS, private bytes, and commit are measured comparably for NCM alone and NCM plus launcher/UNM | Documented performance baseline |

## Deferred decisions

- Minimum supported Windows version and nested-job behavior.
- Whether the verified UNM artifact can be redistributed or must be downloaded separately.
- Whether a bounded single UNM restart is useful after the MVP; unlimited restart is not allowed.
- Installer and shortcut integration after the portable MVP is stable.
- A lighter native core only if measured resource use, rather than preference, justifies it after the MVP.
