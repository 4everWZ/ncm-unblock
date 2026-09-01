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
  // The surface is verified and configuration was applied. Sidecar startup and
  // routing are the next milestones and are not installed yet.
  configured,
};

// Bootstrap body. Resolves the backend off the loader lock and verifies that
// the host surface is the one this build pins. Never call from `DllMain`.
void prepare_session() noexcept;

[[nodiscard]] session_result current_session_result() noexcept;

// Configuration the session applied. Meaningful only once the session result is
// `disabled` or `configured`; before that it holds this build's defaults.
[[nodiscard]] const config::settings& session_settings() noexcept;

}  // namespace ncm::winmm_proxy
