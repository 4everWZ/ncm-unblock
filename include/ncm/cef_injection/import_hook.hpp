#pragma once

namespace ncm::cef_injection {

enum class import_hook_result {
  installed,
  already_installed,
  client_not_loaded,
  cef_not_loaded,
  api_mismatch,
  export_missing,
  import_missing,
  invalid_image,
  write_failed,
};

// Replaces only cloudmusic.dll's ordinary named import of
// `cef_execute_process`. The loaded CEF module must repeat the pinned API hash
// pair and the IAT slot must still point at that module's exported function.
[[nodiscard]] import_hook_result install_loaded_import_hook() noexcept;

// Event-driven bounded wait for cloudmusic.dll/libcef.dll to load. Loader
// callbacks only signal an event; parsing and patching run after loader lock.
[[nodiscard]] import_hook_result install_loaded_import_hook(
    unsigned timeout_milliseconds) noexcept;

// Explicit-module form used by synthetic integration fixtures.
[[nodiscard]] import_hook_result install_import_hook(
    void* client_module, void* cef_module) noexcept;

[[nodiscard]] const char* describe(import_hook_result result) noexcept;

}  // namespace ncm::cef_injection
