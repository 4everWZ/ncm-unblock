# Project Working Rules

- Treat `tmp/` as ignored, non-canonical input. Validate useful claims against source code, the target executable, upstream primary sources, or fresh runtime evidence before adopting them.
- Keep the product contract in `docs/specs/ncm-unblock-lite.md` and unfinished execution state in `docs/plans/ncm-unblock-lite.md`. Update them when behavior or scope changes; do not create parallel phase documents by default.
- Target Windows and NCM 2.10.12 with BetterNCM v2. Release native code is C++20, built with CMake and Visual Studio for x64.
- Keep the BetterNCM plugin, `unm-host` supervisor, UNM sidecar ownership, and packaging in separate modules. Do not add downloaders, injectors, services, settings apps, or upstream source trees before the owning milestone requires them.
- Node must not run inside `cloudmusic.exe`. Normal project development and release packaging must not require or ship a separate Node toolchain, package manager, `node_modules`, or upstream source checkout. Official UNM standalone may contain its own Node runtime and is not redistributed.
- Make inspection non-invasive by default. Do not modify the NCM installation, install certificates, change system proxy settings, inject code, capture credentials, or terminate user processes without explicit task scope and a recovery plan.
- Treat `%LOCALAPPDATA%\Netease\CloudMusic\localdata` as opaque private user state. Do not decode, log, source-control, or edit it directly. The plugin uses NCM's supported proxy configuration channel only.
- UNM cleanup is job-owned. Do not use image-name `taskkill` as the primary lifecycle. NCM stays outside the UNM job. Closing the window to tray is not session end; NCM's main `cloudmusic.exe` (install path, command line without `--type=`) is.
- Use out-of-source builds. Do not commit generated projects, build output, downloaded executables, runtime logs, or user data.
- Before editing, inspect `git status`. Preserve unrelated work and make focused commits using concise prefixes such as `docs:`, `build:`, `feat:`, `test:`, or `fix:`. Do not push unless explicitly requested.
- Define the changed claim before verification. Prefer the smallest fresh build, test, static inspection, or runtime check that can falsify it, and record material evidence boundaries in the current plan.
- When adopting third-party code or binaries, verify the authoritative upstream, version, license, redistribution terms, architecture, and runtime interface. Pin dependencies through the repository's declared workflow; do not vendor reference projects wholesale.
