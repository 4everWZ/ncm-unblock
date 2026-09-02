#pragma once

#include "ncm/config/settings.hpp"

namespace ncm::winmm_proxy {

// Outcome of the bootstrap work this proxy runs once the loader has released
// its lock.
enum class session_result {
  pending,
  // The backend exposes exactly the surface this build pins.
  verified,
  // The backend exports a surface this build does not pin, so the proxy would
  // shadow entries it cannot forward. Forwarding continues; routing does not.
  surface_mismatch,
  // The backend has no readable export directory.
  surface_unreadable,
  // No backend was resolved, so nothing was verified.
  backend_unresolved,
  // A configuration file is present but does not state a usable intent.
  // Forwarding continues; routing does not.
  configuration_invalid,
  // The verified session read a configuration that turns the feature off.
  disabled,
  // Configuration was applied and a sidecar is owned by this process. Routing
  // is the next milestone and is not installed yet.
  sidecar_ready,
  // Configuration was applied but the sidecar could not be started. Forwarding
  // continues; routing is not installed.
  sidecar_failed,
};

// Bootstrap body. Resolves the backend off the loader lock and verifies that
// the host surface is the one this build pins. Never call from `DllMain`.
void prepare_session() noexcept;

[[nodiscard]] session_result current_session_result() noexcept;

// Configuration the session applied. Meaningful once the session result is
// `disabled`, `sidecar_ready`, or `sidecar_failed`; before that it holds this
// build's defaults.
[[nodiscard]] const config::settings& session_settings() noexcept;

// Sidecar identity after `sidecar_ready`. Zero when the session does not own a
// ready sidecar.
[[nodiscard]] unsigned long session_sidecar_process_id() noexcept;
[[nodiscard]] unsigned short session_sidecar_http_port() noexcept;
[[nodiscard]] unsigned short session_sidecar_https_port() noexcept;

}  // namespace ncm::winmm_proxy
