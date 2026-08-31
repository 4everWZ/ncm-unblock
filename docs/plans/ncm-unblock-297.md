# NCM Unblock 2.9.7 MVP Work Plan

- **Goal:** Deliver and verify a portable native launcher that runs a pinned UNM sidecar only for a managed NCM 2.9.7 session.
- **Primary contract:** [NCM Unblock 2.9.7 specification](../specs/ncm-unblock-297.md)

## Changed claim

The repository will provide a reproducible, non-invasive investigation path first, then a launcher whose loopback routing and process lifecycle conform to the primary contract. Reference material under `tmp/` supplies hypotheses only.

## Work

| Task | Status | Done when | Evidence or dependency |
|---|---|---|---|
| Establish repository governance and canonical scope | Done | Root instructions, one specification, one current plan, ignore rules, and line-ending policy are committed | Initial repository commit |
| M0: implement repeatable static/runtime inspection tooling | Done | A Win32 tool can inspect PE architecture/imports and report relevant NCM processes/modules without modifying the installation | Fresh Win32 build; focused CTest; real 2.9.7 probe |
| M0: record the 2.9.7 runtime inventory | In progress | Architecture, signature, process roles, loaded network stacks, active connection ownership, and loader candidates are separated into observed facts and hypotheses | [Runtime investigation](../research/ncm-2.9.7-runtime.md); playback path and loader stability remain open |
| M0: establish proxy and HTTPS behavior | Next | Client-local proxy configuration, certificate behavior, traffic ownership, and rollback are reproducible without exposing user data | Controlled manual/runtime matrix |
| M1: pin and validate an upstream UNM executable | Pending | Version, source, license, architecture, CLI, readiness, and the normal/unavailable-track workflow are recorded | Upstream primary source and compatibility matrix |
| M2: implement the native launcher lifecycle | Pending | Port reservation, sidecar job ownership, readiness, NCM start/attach policy, cleanup, and diagnostics meet the specification | Focused unit and process integration tests |
| M3: measure the baseline | Pending | NCM-alone, standalone-UNM, and launcher-managed measurements use one documented method | Performance report |
| Package the MVP | Pending | A portable manifest contains only approved runtime artifacts and configuration | Clean release build and manifest inspection |

## Current evidence

- The local target is `cloudmusic.exe` version 2.9.7.199711 with a valid NetEase Authenticode signature and a PE32 x86 header.
- The running client has one browser/main process plus GPU, renderer, and reporter processes. The browser/main process currently owns observed TCP connections.
- The process set loads WinHTTP, WinINet, libcurl, and CEF-related modules, so static imports alone do not identify the playback routing layer.
- The Win32 runtime probe cleanly configures and builds through `tools/build.ps1`; focused PE/process tests pass, and the tool reproduces the target facts without changing NCM.
- The upstream UNM release page currently offers standalone artifacts, but exact 2.9.7 compatibility, artifact interface, license/redistribution treatment, and readiness semantics remain unverified locally.

## Blockers

- Proxy/HTTPS investigation must avoid exposing credentials or changing persistent client/system state without an explicit rollback procedure.
- An upstream executable cannot be added to packaging until its authoritative version, license, redistribution terms, architecture, and interface are verified.

## Next action

Define a reversible client-local proxy observation and run the HTTP/HTTPS behavior matrix before choosing a routing or loader mechanism.
