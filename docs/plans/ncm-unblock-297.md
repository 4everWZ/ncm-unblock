# NCM Unblock 2.9.7 MVP Work Plan

- **Goal:** Deliver and verify a portable native launcher that runs a pinned UNM sidecar only for a managed NCM 2.9.7 session.
- **Primary contract:** [NCM Unblock 2.9.7 specification](../specs/ncm-unblock-297.md)

## Changed claim

The repository will provide a reproducible, non-invasive investigation path first, then a launcher whose loopback routing and process lifecycle conform to the primary contract. Reference material under `tmp/` supplies hypotheses only.

## Work

| Task | Done when | Evidence or dependency |
|---|---|---|
| Establish repository governance and canonical scope | Root instructions, one specification, one current plan, ignore rules, and line-ending policy are committed | Initial repository commit |
| M0: implement repeatable static/runtime inspection tooling | A Win32 tool can inspect PE architecture/imports and report relevant NCM processes/modules without modifying the installation | Fresh Win32 build and focused tests |
| M0: record the 2.9.7 runtime inventory | Architecture, signature, process roles, loaded network stacks, active connection ownership, and loader candidates are separated into observed facts and hypotheses | `docs/research/ncm-2.9.7-runtime.md` |
| M0: establish proxy and HTTPS behavior | Client-local proxy configuration, certificate behavior, traffic ownership, and rollback are reproducible without exposing user data | Controlled manual/runtime matrix |
| M1: pin and validate an upstream UNM executable | Version, source, license, architecture, CLI, readiness, and the normal/unavailable-track workflow are recorded | Upstream primary source and compatibility matrix |
| M2: implement the native launcher lifecycle | Port reservation, sidecar job ownership, readiness, NCM start/attach policy, cleanup, and diagnostics meet the specification | Focused unit and process integration tests |
| M3: measure the baseline | NCM-alone, standalone-UNM, and launcher-managed measurements use one documented method | Performance report |
| Package the MVP | A portable manifest contains only approved runtime artifacts and configuration | Clean release build and manifest inspection |

## Current evidence

- The local target is `cloudmusic.exe` version 2.9.7.199711 with a valid NetEase Authenticode signature and a PE32 x86 header.
- The running client has one browser/main process plus GPU, renderer, and reporter processes. The browser/main process currently owns observed TCP connections.
- The process set loads WinHTTP, WinINet, libcurl, and CEF-related modules, so static imports alone do not identify the playback routing layer.
- The upstream UNM release page currently offers standalone artifacts, but exact 2.9.7 compatibility, artifact interface, license/redistribution treatment, and readiness semantics remain unverified locally.

## Blockers

- Proxy/HTTPS investigation must avoid exposing credentials or changing persistent client/system state without an explicit rollback procedure.
- An upstream executable cannot be added to packaging until its authoritative version, license, redistribution terms, architecture, and interface are verified.

## Next action

Implement and test the M0 inspection tool, then use it to produce the runtime inventory before choosing a proxy or loader mechanism.
