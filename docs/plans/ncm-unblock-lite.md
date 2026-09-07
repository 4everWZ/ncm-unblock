# UnblockLite Work Plan

- **Goal:** BetterNCM 2.10.12 plugin + `unm-host.exe` Job Object supervisor + UNM matcher (bundled patched JS or official standalone), with zero residue after NCM tray Exit.
- **Primary contract:** [UnblockLite specification](../specs/ncm-unblock-lite.md)

## Changed claim

Production is no longer a 2.9.7 portable launcher that starts NCM. `main` is UnblockLite: the plugin starts a supervisor that is already inside an NCM session. The 2.9.7 product remains on `ncm-2.9.7`.

## Work

| Milestone | Status | Done when | Current evidence or dependency |
|---|---|---|---|
| L0: archive and reset | Done | `ncm-2.9.7` holds the launcher line; `main` is the lite tree | Branch `ncm-2.9.7` retains the 2.9.7 launcher; `main` is UnblockLite |
| L1: host supervisor | Done | Mutex, NCM-main attach, job-owned UNM, PAC ready, wait main/stop, reclaim | `unm-host` + `ncm_watch`; x64 focused tests passed |
| L2: plugin | Done | Disabled/Starting/Running, exec host, PAC check, proxy write-if-needed, `--stop` | `plugin/main.js` + `manifest.json` with `ncm3-compatible`; path search covers runtime + data dir; PS `Start-Process` via `-EncodedCommand` (quote-safe through `betterncm.app.exec`); Sources sanitized to UNM ids; default Sources `bodian,migu,kugou`; Disable button; no image-name `taskkill` |
| L3: build/package | Done | x64 CMake, focused tests, `UnblockLite.plugin` zip without official pkg | `tools/package.ps1` builds `UnblockLite.plugin` (root entries including `certs/` + `core/unm-app.js`) + outer release ZIP |
| L3b: HTTPS MITM trust | Done | Packaged CA+leaf, `SIGN_*` env, Current User Root install, focused tests | `mitm_certs` + managed env overlay; host refuses start without material/trust |
| L3c: match quality defaults | Done | Empty Sources → `-o bodian migu kugou`; host overlays `ENABLE_FLAC=true` + `FOLLOW_SOURCE_ORDER=true` | Dropped anonymous `kuwo` (VIP promo clip); bodian first so FOLLOW does not pay migu miss timeouts; plugin DEFAULTS/copy aligned; no QQ_COOKIE UI |
| L3d: local VIP UI env | Done | Host overlays `ENABLE_LOCAL_VIP=svip` | Live env confirmed; player/url FLAC `br=999000`; privilege APIs `pl/playMaxbr=999000`, `maxBrLevel=lossless`. Official inject still only lifted `none`→`exhigh`. Anonymous vip/info is 301; red+/SVIP patch needs a logged-in NCM session |
| L3e: privilege JS fork | Done (ship 0.1.9) | Bundled patched `core/unm-app.js`; host JS→node launch; plugin prefers JS; package includes NOTICE; live `plLevel=lossless` | Vendored v0.28.0 under `third_party/unm/`; inject floors levels/maxbr under `ENABLE_FLAC`; host `unm mode=js`; official exe still fallback |
| L3f: URL level for quality chip | Done (ship 0.1.10) | `tryMatch` fills `data[].level`/`encodeType` from matched FLAC; privilege also floors `playMaxBrLevel`/`downloadMaxBrLevel`/`maxbr` | Live URL had `level:null`/`encodeType:null` while privilege was already lossless — PC chip can stay「标准」on null level. Reinstall 0.1.10 and check chip; if still「标准」, check 设置 → 播放 → 在线音质 |
| L4: lifecycle proof | In progress | Tray hide keeps UNM; tray Exit leaves 0 host/UNM/ports; NCM network works under Running | Host attach+PAC proven; match + VIP + privilege/URL fork in 0.1.10 — confirm chip after login/restart; tray hide/Exit residue still to confirm |

## Next action

On a logged-in NCM 2.10.12 session with **0.1.10**: Save & apply / restart so host runs `node …\core\unm-app.js`; confirm `player/url` `level=lossless` + `encodeType=flac`, privilege `plLevel=lossless`, and whether the quality chip leaves “标准” (check 设置 → 播放 → 在线音质 if not). Then prove tray hide keeps UNM and tray Exit leaves 0 host/UNM/ports.
