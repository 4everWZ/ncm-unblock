# UnblockLite Work Plan

- **Goal:** BetterNCM 2.10.12 plugin + `unm-host.exe` Job Object supervisor + official UNM standalone, with zero residue after NCM tray Exit.
- **Primary contract:** [UnblockLite specification](../specs/ncm-unblock-lite.md)

## Changed claim

Production is no longer a 2.9.7 portable launcher that starts NCM. `main` is UnblockLite: the plugin starts a supervisor that is already inside an NCM session. The 2.9.7 product remains on `ncm-2.9.7`.

## Work

| Milestone | Status | Done when | Current evidence or dependency |
|---|---|---|---|
| L0: archive and reset | Done | `ncm-2.9.7` holds the launcher line; `main` is the lite tree | Branch `ncm-2.9.7` retains the 2.9.7 launcher; `main` is UnblockLite |
| L1: host supervisor | Done | Mutex, NCM-main attach, job-owned UNM, PAC ready, wait main/stop, reclaim | `unm-host` + `ncm_watch`; x64 focused tests passed |
| L2: plugin | Done | Disabled/Starting/Running, exec host, PAC check, proxy write-if-needed, `--stop` | `plugin/main.js` + `manifest.json`; no image-name `taskkill` |
| L3: build/package | Done | x64 CMake, focused tests, ZIP without UNM | `x64-debug` tests 4/4; `tools/package.ps1` lists plugin+host+notice |
| L4: lifecycle proof | Not started | Tray hide keeps UNM; tray Exit leaves 0 host/UNM/ports | Needs NCM 2.10.12 + BetterNCM + placed UNM v0.28.0 |

## Next action

Prove start / tray hide / tray Exit on a real NCM 2.10.12 session with BetterNCM and official UNM v0.28.0 placed at `UnblockLite/core/UnblockNeteaseMusic.exe`.
