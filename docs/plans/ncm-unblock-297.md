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
| M6: playback compatibility | Pending | Login, search, normal/unavailable tracks, track changes, playlist, and pause/resume behave like direct manual UNM operation | Requires a pinned artifact and supported NCM UI proxy setup |
| M7: failure recovery | Pending | UNM/NCM/launcher crash, occupied ports, bad config, and missing executables fail boundedly without owned orphans or unrelated-process impact | Extend focused integration tests, then run the exact-client matrix |
| M8: performance | Pending | Comparable NCM-alone and launcher-plus-UNM measurements cover RSS, private bytes, commit, idle CPU, startup latency, and leaks/orphans | Run after functional compatibility is stable |
| M9: portable release | Pending | Clean Win32 release output contains the launcher, config, docs, approved runtime artifacts, and notices only | UNM license, redistribution, corresponding source, architecture, and runtime interface must be closed |
| M10: installer and shortcut | Deferred | Reversible install/uninstall can point the NCM shortcut at the launcher while retaining the NCM icon | Begin only after the portable MVP is accepted |

## Current evidence

- The local target is signed `cloudmusic.exe` 2.9.7.199711 and PE32 x86.
- NCM has browser/root, renderer, GPU, and reporter roles; closing its main window normally leaves the tray session alive.
- NCM exposes supported custom proxy fields through its own settings UI. Direct writes to opaque `localdata`, UI-coordinate automation, process-local Chromium proxy flags, environment proxy variables, system proxy, and certificate installation are not production routes.
- `managed_process` prepares a child suspended, assigns an unnamed kill-on-close job before resume, preserves Windows arguments, distinguishes root exit from tree completion, and reclaims the owned tree without touching a same-image process outside the job.
- `loopback_port_pair` exclusively reserves distinct HTTP/HTTPS listeners on `127.0.0.1`. The fixed-pair collision and release handoff are tested.
- `unm_sidecar` applies explicit loopback/restricted arguments, keeps lease release adjacent to resume, and requires both listener owner sets to remain within its job before and after a complete PAC response. These checks currently use a synthetic sidecar; no real UNM artifact has been executed.
- Official UNM v0.28.0 sources previously audited x64/ARM64 standalone artifacts and no dedicated health endpoint. Its bundled leaf certificate expired on 2026-07-24 and its default private key is public; current upstream/artifact, licensing, and NCM compatibility must be freshly verified before packaging or trust claims.

## Blockers

- M4–M6 require an authorized bounded run using NCM's supported settings UI and an out-of-repository private snapshot while NCM is fully stopped. The run must restore the original UI configuration and verify recovery in the same run.
- A UNM binary cannot enter the release until authoritative upstream identity, version, license, redistribution terms, architecture, runtime interface, notices, and corresponding-source obligations are verified.
- The production fixed-port contract differs from the current reusable primitive's optional automatic mode. M1/M3 must remove that mode from the product-facing configuration and launcher path; internal reservation mechanics may remain if they do not change fixed-port behavior.

## Next action

Implement M1 as the first production slice: add the `ncm-unblock.exe` entry point and configuration module, require explicit fixed ports, validate exact paths/version and existing target processes, and connect those checks to the existing sidecar primitive. Add only focused tests that falsify the preflight and configuration contract; do not add NCM launch until those checks pass.
