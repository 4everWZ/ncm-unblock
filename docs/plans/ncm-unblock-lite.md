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
| L2: plugin | Done | Disabled/Starting/Running, exec host, PAC check, proxy write-if-needed, `--stop` | `plugin/main.js` + `manifest.json` with `ncm3-compatible`; path search covers runtime + data dir; PS `Start-Process` via `-EncodedCommand` (quote-safe through `betterncm.app.exec`); Sources sanitized to UNM ids; Disable button; no image-name `taskkill` |
| L3: build/package | Done | x64 CMake, focused tests, `UnblockLite.plugin` zip without UNM | `tools/package.ps1` builds `UnblockLite.plugin` (root entries including `certs/`) + outer release ZIP (`0.1.4`) |
| L3b: HTTPS MITM trust | Done | Packaged CA+leaf, `SIGN_*` env, Current User Root install, focused tests | `mitm_certs` + managed env overlay; host refuses start without material/trust |
| L4: lifecycle proof | In progress | Tray hide keeps UNM; tray Exit leaves 0 host/UNM/ports; NCM network works under Running | Host attach+PAC proven; cert path added in 0.1.4 — re-verify live NCM without -202; tray hide/Exit residue still to confirm |

## Next action

Install 0.1.4 plugin on a real NCM 2.10.12 session: Save & apply → Running + PAC 200 + no Chromium -202; Disable stops host. Then prove tray hide keeps UNM and tray Exit leaves 0 host/UNM/ports.
