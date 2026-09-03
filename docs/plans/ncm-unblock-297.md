# NCM Unblock 2.9.7 MVP Work Plan

- **Goal:** Deliver and verify a portable native launcher that runs a pinned UNM sidecar only for a managed NCM 2.9.7.199711 session.
- **Primary contract:** [NCM Unblock 2.9.7 specification](../specs/ncm-unblock-297.md)

## Changed claim

The production line is a fixed-port Sidecar Launcher. It starts UNM before NCM, keeps UNM for the complete tray-aware NCM session, and reclaims it afterward without injection, persistent services, system proxy changes, certificate installation, direct `localdata` modification, or a separate Node installation. DLL/CEF/V8 research is frozen on `research/native-injection`.

## Work

| Milestone | Status | Done when | Current evidence or dependency |
|---|---|---|---|
| M0: reset direction | Done | Sidecar is the canonical product contract; injection work is absent from the production line and preserved separately | `main` forks before WinMM load work; `research/native-injection` retains the former tip |
| M1: minimal launcher and configuration | Done | A real launcher executable loads the portable config, validates exact NCM and UNM paths, rejects injection-only keys, checks fixed ports, and rejects an existing target NCM session with actionable errors | Strict configuration, target inspection, fixed-port launch path, existing-session guard, and focused tests are integrated in `ncm-unblock.exe` |
| M2: UNM lifecycle and logs | Done | UNM starts hidden with stdout/stderr capture and is reclaimed when the launcher exits or crashes | Suspended launch, restricted inherited handles, redirected logs, private kill-on-close job, and bounded tree cleanup are tested; exact-client normal-exit cleanup passed |
| M3: readiness | Done | Both fixed listeners belong to the private UNM job and a complete valid `/proxy.pac` response arrives before NCM launch | Synthetic collision/timeout/ownership tests and the official v0.28.0 artifact both pass the fixed-port PAC readiness contract |
| M4: NCM launch | Done | With NCM manually configured to the fixed loopback HTTP port, launcher starts the exact client normally after readiness and the client can connect | Release launcher started signed x86 2.9.7.199711 only after UNM readiness; routed full-length playback succeeded |
| M5: tray-aware NCM session lifecycle | Done | Closing the window to tray keeps UNM alive; NCM's real exit reclaims UNM and ends the launcher | Exact-client run retained launcher, UNM, all NCM roles, and both ports after window close; tray exit reclaimed launcher/UNM and released both ports |
| M6: playback compatibility | In progress | Login, search, normal/unavailable tracks, track changes, playlist, and pause/resume behave like direct manual UNM operation | Fixed-port routing and full-length playback with upstream-default providers are proven; the complete matrix remains |
| M7: failure recovery | Done | UNM/NCM/launcher crash, occupied ports, bad config, and missing executables fail boundedly without owned sidecar orphans or unrelated-process impact | Focused Win32 tests cover forced owner termination, NCM root crash with a live child, early UNM exit, readiness timeout, fixed-port collision, invalid/missing configuration and executable, full-tree cleanup, and unrelated-process isolation |
| M8: performance | In progress | Comparable NCM-alone and launcher-plus-UNM measurements cover RSS, private bytes, commit, idle CPU, startup latency, and leaks/orphans | Two 30-second playback windows establish provisional memory/CPU cost; idle, startup, long-run, and native-launcher measurements remain |
| M9: portable release | Pending | Clean Win32 release output contains the launcher, config, docs, approved runtime artifacts, and notices only | UNM license, redistribution, corresponding source, architecture, and runtime interface must be closed |
| M10: installer and shortcut | Deferred | Reversible install/uninstall can point the NCM shortcut at the launcher while retaining the NCM icon | Begin only after the portable MVP is accepted |

## Current evidence

- The local target is signed `cloudmusic.exe` 2.9.7.199711 and PE32 x86.
- NCM has browser/root, renderer, GPU, and reporter roles; closing its main window normally leaves the tray session alive.
- NCM exposes supported custom proxy fields through its own settings UI. Direct writes to opaque `localdata`, UI-coordinate automation, process-local Chromium proxy flags, environment proxy variables, system proxy, and certificate installation are not production routes.
- `managed_process` prepares a child suspended, assigns an unnamed kill-on-close job before resume, preserves Windows arguments, distinguishes root exit from tree completion, and reclaims the owned tree without touching a same-image process outside the job.
- `loopback_port_pair` exclusively reserves distinct HTTP/HTTPS listeners on `127.0.0.1`. The fixed-pair collision and release handoff are tested.
- `unm_sidecar` applies explicit loopback/restricted arguments, keeps lease release adjacent to resume, and requires both listener owner sets to remain within its job before and after a complete PAC response. Synthetic tests cover these invariants; the real v0.28.0 artifact evidence is recorded separately below.
- The native `ncm-unblock.exe` now loads a strict colocated INI, validates the signed x86 2.9.7.199711 target, starts hidden/logged UNM on fixed ports, waits for owned-listener PAC readiness, starts NCM normally, and follows its path- and creation-time-bound multi-process session with a one-second census.
- Debug and Release builds each pass all seven focused test executables. The current Release exact-client run started launcher PID 33752, NCM root PID 36256, and UNM PID 34720. Closing the main window left three NCM roles, launcher, UNM, and both UNM-owned listeners alive. Tray exit ended the NCM session, reclaimed launcher and UNM, released both ports, and restored the private configuration snapshot byte-for-byte with its recorded metadata.
- The failure-recovery matrix now exercises Win32 termination semantics directly: terminating the process that owns a kill-on-close Job reclaims both the managed root and its descendant within five seconds, while a forced NCM root exit does not end the tracked session until its live descendant exits. Existing focused cases cover early UNM exit, readiness timeout, fixed-port collision, invalid or missing configuration/executables, cleanup of unresumed and multi-process jobs, and isolation from an unrelated same-image process.
- Official UNM v0.28.0 sources previously audited x64/ARM64 standalone artifacts and no dedicated health endpoint. Its bundled leaf certificate expired on 2026-07-24 and its default private key is public; current upstream/artifact, licensing, and NCM compatibility must be freshly verified before packaging or trust claims.
- A fresh exact-client run established that the official v0.28.0 Windows x64 standalone owns both fixed listeners, serves PAC, and routes NCM 2.9.7 traffic through the supported client proxy setting. The first run explicitly forced the non-default `kuwo` provider and every tested audio item ended after approximately 11 seconds. Official v0.28.0 and current `enhanced` both default to `kugou`, `bodian`, `migu`, and `ytdlp`; Kuwo is not a default provider, and current `enhanced` contains a post-release Kuwo request change. Repeating with `-o` omitted produced full-length playback, so product defaults must remain owned by the pinned upstream release rather than hardcode Kuwo.
- Comparable 30-second playback samples on a 16-logical-processor host measured NCM alone at 729.8 MiB average RSS, 655.1 MiB average private bytes, and 1.91% machine-normalized CPU. With UNM routing, NCM measured 714.4 MiB RSS, 619.9 MiB private bytes, and 1.58% CPU; UNM itself measured 50.1 MiB RSS, 38.7 MiB private bytes, and 0.01% CPU. The combined averages were 764.5 MiB RSS and 658.6 MiB private bytes, provisional deltas of 34.7 MiB (4.8%) and 3.5 MiB (0.5%). NCM run-to-run variation prevents attributing the lower combined CPU or the full memory delta to routing; the defensible component estimate is approximately 50 MiB RSS and 39 MiB private bytes for UNM with no CPU cost distinguishable in this window. The PowerShell experiment controller is not a native-launcher measurement.

## Blockers

- The remaining M6 compatibility cases require further bounded exact-client runs using NCM's supported settings UI and the same out-of-repository snapshot/restore protocol.
- A UNM binary cannot enter the release until authoritative upstream identity, version, license, redistribution terms, architecture, runtime interface, notices, and corresponding-source obligations are verified.

## Next action

Finish M6's login/search/track-change/playlist/pause-resume matrix and M8's idle, startup, long-run, and native-launcher measurements. Then verify the authoritative UNM release, license, redistribution and corresponding-source obligations before preparing the portable M9 artifact.
