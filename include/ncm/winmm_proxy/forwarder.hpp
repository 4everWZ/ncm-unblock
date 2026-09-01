#pragma once

namespace ncm::winmm_proxy {

// One pinned entry of the proxied module's export surface.
struct export_entry {
  unsigned short ordinal;
  // Exported name, or nullptr when the entry is exported by ordinal only.
  const char* name;
  // Ordinal whose entry point this entry shares, or 0 when it is distinct.
  unsigned short alias_of;
};

// The pinned surface generated from the committed export manifest.
[[nodiscard]] const export_entry* pinned_exports(unsigned* count) noexcept;

// Absolute path of the backend module to forward to. Each hosting DLL supplies
// its own implementation so the core never guesses a location, and so a test
// fixture can point at a synthetic backend without a production switch.
// Returns false when no backend location can be determined.
extern "C" bool ncm_winmm_backend_path(wchar_t* buffer, unsigned count) noexcept;

// Exit code used when the backend contract cannot be met. Partial forwarding
// would corrupt the caller's stack, so the process is stopped instead.
inline constexpr unsigned long backend_failure_exit_code = 0xE0C40001UL;

// Loads the backend by absolute path and stores one address per pinned entry
// into `targets`, then publishes `*state` as non-zero. Runs at most once.
//
// Terminates the process with `backend_failure_exit_code` when the backend
// cannot be loaded, when it resolves to the calling module (a self-forward),
// when the loaded module is not the requested file, or when any pinned entry is
// missing. The reason is written to the debugger before the process ends; a
// user-facing diagnostic belongs to the bootstrap that owns the proxy.
void resolve_backend(long* state, void** targets, unsigned count) noexcept;

}  // namespace ncm::winmm_proxy
