Default matcher (shipped in this plugin under core/):

  core/unm-app.js

Patched UnblockNeteaseMusic/server v0.28.0 precompiled bundle (LGPL-3.0).
See core/NOTICE.md. Host launches it with system Node (node.exe on PATH,
NODE_BINARY, or common nvm-windows path). Requires Node 18+.

Optional fallback — place the official Windows x64 standalone here or under
BetterNCM data:

  UnblockNeteaseMusic.exe

Pinned release: UnblockNeteaseMusic/server v0.28.0
Asset: unblockneteasemusic-win-x64.exe (rename to UnblockNeteaseMusic.exe)

Preferred fallback placement after the plugin is installed:

  <BetterNCM data path>/UnblockLite/UnblockNeteaseMusic.exe

You may also place the official exe next to the extracted plugin under
`plugins_runtime/UnblockLite/core/`.

This package does not redistribute the official pkg binary. The official
standalone embeds its own Node runtime; the bundled JS path uses your
installed Node instead.
