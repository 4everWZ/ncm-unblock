# NCM Unblock 2.9.7 MVP Work Plan

- **Goal:** Deliver and verify a portable native launcher that runs a pinned UNM sidecar only for a managed NCM 2.9.7.199711 session.
- **Primary contract:** [NCM Unblock 2.9.7 specification](../specs/ncm-unblock-297.md)

## Changed claim

The production line is a fixed-port Sidecar Launcher. It starts UNM before NCM, keeps UNM for the complete tray-aware NCM session, and reclaims it afterward without injection, persistent services, system proxy changes, certificate installation, direct `localdata` modification, or a separate Node installation. DLL/CEF/V8 research is frozen on `research/native-injection`.

## Work

| Milestone | Status | Done when | Current evidence or dependency |
|---|---|---|---|
| M0: reset direction | Done | Sidecar is the canonical product contract; injection work is absent from the production line and preserved separately | `main` forks before WinMM load work; `research/native-injection` retains the former tip |
| M1: minimal launcher and configuration | In progress | A real launcher executable loads the portable config, validates exact NCM and UNM paths, rejects injection-only keys, checks fixed ports, and rejects an existing target NCM session with actionable errors | PE/process inspection, managed-process, fixed-pair, and sidecar libraries exist; executable/config integration remains |
| M2: UNM lifecycle and logs | In progress | UNM starts hidden with stdout/stderr capture and is reclaimed when the launcher exits or crashes | Suspended child, private kill-on-close Job Object, tree wait/termination, argument preservation, and unrelated-process isolation are tested; log capture remains |
| M3: readiness | In progress | Both fixed listeners belong to the private UNM job and a complete valid `/proxy.pac` response arrives before NCM launch | Synthetic listener ownership, PAC validation, timeout, early-exit, and collision primitives exist; remove automatic-port production behavior and validate a pinned real artifact |
| M4: NCM launch | Pending | With NCM manually configured to the fixed loopback HTTP port, launcher starts the exact client normally after readiness and the client can connect | Requires M1–M3 and an authorized exact-client run |
| M5: tray-aware NCM session lifecycle | Pending | Closing the window to tray keeps UNM alive; NCM's real exit reclaims UNM and ends the launcher | Process roles and tray behavior are observed; path-bound session ownership/detection remains |
| M6: playback compatibility | In progress | Login, search, normal/unavailable tracks, track changes, playlist, and pause/resume behave like direct manual UNM operation | Fixed-port routing and full-length playback with upstream-default providers are proven; the complete matrix remains |
| M7: failure recovery | Pending | UNM/NCM/launcher crash, occupied ports, bad config, and missing executables fail boundedly without owned orphans or unrelated-process impact | Extend focused integration tests, then run the exact-client matrix |
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
- Official UNM v0.28.0 sources previously audited x64/ARM64 standalone artifacts and no dedicated health endpoint. Its bundled leaf certificate expired on 2026-07-24 and its default private key is public; current upstream/artifact, licensing, and NCM compatibility must be freshly verified before packaging or trust claims.
- A fresh exact-client run established that the official v0.28.0 Windows x64 standalone owns both fixed listeners, serves PAC, and routes NCM 2.9.7 traffic through the supported client proxy setting. The first run explicitly forced the non-default `kuwo` provider and every tested audio item ended after approximately 11 seconds. Official v0.28.0 and current `enhanced` both default to `kugou`, `bodian`, `migu`, and `ytdlp`; Kuwo is not a default provider, and current `enhanced` contains a post-release Kuwo request change. Repeating with `-o` omitted produced full-length playback, so product defaults must remain owned by the pinned upstream release rather than hardcode Kuwo.
- Comparable 30-second playback samples on a 16-logical-processor host measured NCM alone at 729.8 MiB average RSS, 655.1 MiB average private bytes, and 1.91% machine-normalized CPU. With UNM routing, NCM measured 714.4 MiB RSS, 619.9 MiB private bytes, and 1.58% CPU; UNM itself measured 50.1 MiB RSS, 38.7 MiB private bytes, and 0.01% CPU. The combined averages were 764.5 MiB RSS and 658.6 MiB private bytes, provisional deltas of 34.7 MiB (4.8%) and 3.5 MiB (0.5%). NCM run-to-run variation prevents attributing the lower combined CPU or the full memory delta to routing; the defensible component estimate is approximately 50 MiB RSS and 39 MiB private bytes for UNM with no CPU cost distinguishable in this window. The PowerShell experiment controller is not a native-launcher measurement.

## Blockers

- M4–M6 require an authorized bounded run using NCM's supported settings UI and an out-of-repository private snapshot while NCM is fully stopped. The run must restore the original UI configuration and verify recovery in the same run.
- A UNM binary cannot enter the release until authoritative upstream identity, version, license, redistribution terms, architecture, runtime interface, notices, and corresponding-source obligations are verified.
- The production fixed-port contract differs from the current reusable primitive's optional automatic mode. M1/M3 must remove that mode from the product-facing configuration and launcher path; internal reservation mechanics may remain if they do not change fixed-port behavior.

## Next action

Implement M1's launcher entry point and configuration integration using fixed ports and upstream-owned provider defaults. Preserve the bounded experiment controller as the exact-client compatibility harness, then extend M6 only with the remaining login/search/track-change/playlist/pause-resume cases. Complete M8 later with idle, startup, long-run, and native-launcher measurements; do not infer those from the two playback windows.
