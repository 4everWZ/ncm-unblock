# UnblockNeteaseMusic (server) — vendored JS

This directory vendors a patched copy of UnblockNeteaseMusic/server **v0.28.0**
`precompiled/app.js` for UnblockLite.

## Upstream

- Project: [UnblockNeteaseMusic/server](https://github.com/UnblockNeteaseMusic/server)
- Tag: `v0.28.0`
- File: `precompiled/app.js` (webpack bundle used by the standalone runtime)
- License: **LGPL-3.0** (upstream ships `COPYING` + `COPYING.LESSER`)

UnblockLite does **not** redistribute the official Windows pkg standalone
(`unblockneteasemusic-win-x64.exe`). The official binary remains a user-placed
fallback.

## Local files

| File | Role |
|---|---|
| `app.js.upstream` | Exact upstream `precompiled/app.js` @ v0.28.0 |
| `app.js` | Privilege-inject patched bundle shipped as plugin `core/unm-app.js` |
| `COPYING` / `COPYING.LESSER` | Upstream GPLv3 + LGPLv3 license texts @ v0.28.0 |

## Patch summary (UnblockLite)

Official UNM `hook.request.after` inject only rewrites level fields when the
value is exactly `none` → `exhigh`, and bitrate floors only when `0` → `320000`.
That leaves many VIP tracks at `plLevel=exhigh` while URL audio is already FLAC
(`br=999000`), so the NCM player quality chip can stay on「标准」/极高.

When `ENABLE_FLAC=true` (UnblockLite host always overlays this):

- If `playMaxbr` / `downloadMaxbr` exist and are `< 999000`, set them to `999000`;
  keep `pl` / `dl` ≥ those max fields (same pattern as upstream).
- Raise `plLevel` / `dlLevel` / `flLevel` to `lossless` when present and not
  already `lossless` (not only when `none`).
- Set `playMaxLevel`, `downloadMaxLevel`, `maxBrLevel`, `playMaxBrLevel`, and
  `downloadMaxBrLevel` to `lossless` **only on privilege-shaped objects** that
  already carry `plLevel` / `playMaxbr` / `downloadMaxbr` / `maxbr`. Floor
  `maxbr` to `999000` when present and lower. (0.1.10 wrote the max-level
  fields on every JSON node during `JSON.stringify` walk, which polluted
  `vip/info` and other roots and could make the PC username control full-refresh
  the page before opening the dropdown.)
- In `tryMatch` URL bodies, set `level` from the matched `br`/`type` (FLAC →
  `lossless`) and `encodeType` from `type`. Upstream left both `null`, which is
  enough for some clients to keep the player quality chip on「标准」even when
  privilege APIs already report lossless and audio is FLAC.

When `ENABLE_FLAC` is not true, upstream `none`→`exhigh` / `0`→`320000`
behavior is preserved; tryMatch still fills `level`/`encodeType` from the
matched stream.

Providers and matching order are unchanged.

## Rebuild

```powershell
# Download upstream precompiled bundle (example):
# curl -L -o third_party/unm/app.js.upstream `
#   https://raw.githubusercontent.com/UnblockNeteaseMusic/server/v0.28.0/precompiled/app.js
# Then re-apply the inject patch (see tools/vendor-unm-app.ps1 when present)
# and copy third_party/unm/app.js → core/unm-app.js before packaging.
```

LGPL-3.0 requires that recipients can obtain the corresponding source of this
LGPL-covered work. The corresponding source for the shipped bundle is this
`third_party/unm/` tree (upstream file + documented patch) in the UnblockLite
repository / release source package.
