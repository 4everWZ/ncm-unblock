# Third-Party Notices

Provenance for every third-party artifact this project depends on or derives
from. An entry is required before the material enters the repository, and again
with redistribution terms resolved before it enters a release.

## Chromium Embedded Framework — interface declarations

| Field | Value |
|---|---|
| Upstream | `https://bitbucket.org/chromiumembedded/cef`, GitHub mirror `chromiumembedded/cef` |
| Branch | `1916` |
| Files consulted | `include/internal/cef_export.h`, `include/internal/cef_types.h`, `include/internal/cef_string_types.h`, `include/capi/cef_base_capi.h`, `include/capi/cef_app_capi.h`, `include/capi/cef_render_process_handler_capi.h`, `include/capi/cef_v8_capi.h` |
| Licence | Three-clause BSD |
| Copyright | Copyright (c) 2014 Marshall A. Greenblatt |
| Used in | [include/ncm/cef/abi_1916.hpp](include/ncm/cef/abi_1916.hpp) |
| Redistributed | No binary, source tree, or header file from CEF is redistributed |

[include/ncm/cef/abi_1916.hpp](include/ncm/cef/abi_1916.hpp) transcribes the
structure layouts and calling convention of five CEF types so this project can
interoperate with the browser runtime the target client already ships. No CEF
source file is copied into the repository and no CEF binary is redistributed;
the client supplies its own `libcef.dll`.

The three-clause BSD licence requires that redistributions of source retain the
copyright notice, the conditions, and the disclaimer. The header names the
upstream, the branch, the files, and the copyright holder. Should a release ever
carry CEF material rather than an interface description of it, the full licence
text must accompany it.

Applicability is verified rather than assumed: `ncm_cef_probe` reads the API
hashes the client's own module reports and confirms they are the published CEF
3.1916 pair, and any code relying on these layouts must repeat that check at
startup and decline on a mismatch.

## UnblockNeteaseMusic server — retained fallback

| Field | Value |
|---|---|
| Upstream | `https://github.com/UnblockNeteaseMusic/server` |
| Version audited | v0.28.0 |
| Licence declared | `LGPL-3.0-only`, with GPLv3 and LGPLv3 texts distributed |
| Redistributed | No |

No upstream artifact has been downloaded, executed, or approved. Before any
release could carry one, this entry needs artifact authenticity, target
compatibility, a corresponding-source route, a certificate lifecycle design, and
a licence and notice audit covering the embedded Node runtime and bundled
dependencies. The bundled leaf certificate expired on 2026-07-24 and its default
private key is public, so the artifact does not clear the trust gate as shipped.
