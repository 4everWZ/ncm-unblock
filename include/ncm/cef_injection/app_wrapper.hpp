#pragma once

#include "ncm/cef/abi_1916.hpp"

namespace ncm::cef_injection {

using register_extension_fn = int(__cdecl*)(
    const cef::cef_string_t* extension_name,
    const cef::cef_string_t* javascript_code,
    cef::cef_v8handler_t* handler);

struct wrapped_application {
  cef::cef_app_t* application{};
  bool wrapped{};
};

enum class registration_state {
  not_attempted,
  succeeded,
  failed,
};

// Wraps an application without modifying the client-owned callback structure.
// The returned wrapper owns one reference to `original` and starts with one
// reference of its own. The caller releases that initial wrapper reference
// after the intercepted CEF entry point returns. On failure the original
// borrowed pointer is returned unchanged and `wrapped` is false.
[[nodiscard]] wrapped_application wrap_application(
    cef::cef_app_t* original, register_extension_fn register_extension) noexcept;

// Process-local M3 observation. Registration is attempted at most once from
// `on_web_kit_initialized`, after the client's original callback returns.
[[nodiscard]] registration_state current_registration_state() noexcept;

// Number of times the registered native extension handler's `execute` ran.
// Registration success alone does not increment this.
[[nodiscard]] long current_marker_count() noexcept;

// Ownership for a non-null handler passed to `register_extension_fn` follows
// CEF 1916 `CefV8HandlerCToCpp::Wrap`: construct with one reference, add one
// extra reference before the call, then never release after the call. Wrap
// always consumes the transfer with one `UnderlyingRelease`. On success CEF
// retains the remaining reference for process lifetime; on failure its
// temporary RefPtr releases the last reference and destroys the handler.

}  // namespace ncm::cef_injection
