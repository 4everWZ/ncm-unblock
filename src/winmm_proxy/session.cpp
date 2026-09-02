#include "ncm/winmm_proxy/session.hpp"

#include "ncm/winmm_proxy/forwarder.hpp"

#include <Windows.h>

#include <cstdio>
#include <filesystem>

namespace ncm::winmm_proxy {
namespace {

long g_result{static_cast<long>(session_result::pending)};
config::settings g_settings{};

void publish(session_result result) noexcept {
  InterlockedExchange(&g_result, static_cast<long>(result));
}

void report(const wchar_t* text) noexcept {
  OutputDebugStringW(L"ncm-unblock winmm proxy: ");
  OutputDebugStringW(text);
  OutputDebugStringW(L"\r\n");
}

void report_shape(const wchar_t* label, const surface_shape& shape) noexcept {
  wchar_t line[128]{};
  if (swprintf_s(
          line, L"%s base=%u functions=%u names=%u", label,
          static_cast<unsigned>(shape.ordinal_base),
          static_cast<unsigned>(shape.function_count),
          static_cast<unsigned>(shape.name_count)) > 0) {
    report(line);
  }
}

void report_configuration_error(const config::load_result& loaded) noexcept {
  wchar_t line[512]{};
  // Truncate rather than fault: a diagnostic is never worth an invalid-parameter
  // handler inside the host process.
  if (loaded.line == 0) {
    if (_snwprintf_s(
            line, _TRUNCATE, L"configuration: %s", loaded.diagnostic.c_str()) != 0) {
      report(line);
    }
    return;
  }
  if (_snwprintf_s(
          line, _TRUNCATE, L"configuration line %u: %s", loaded.line,
          loaded.diagnostic.c_str()) != 0) {
    report(line);
  }
}

// Reads the package configuration once the surface is verified. An absent file
// means "run with this build's defaults"; a present but unusable file means the
// user stated an intent this build cannot honor, so the feature is declined
// rather than approximated. Neither outcome may stop a working client.
void apply_configuration() noexcept {
  try {
    const std::filesystem::path package = config::package_directory();
    if (package.empty()) {
      report(L"the package directory could not be determined,"
             L" so no configuration was applied");
      publish(session_result::configuration_invalid);
      return;
    }

    const config::load_result loaded = config::load_settings(package);
    if (loaded.status == config::load_status::invalid) {
      report(L"the configuration file was rejected; forwarding continues but no"
             L" routing is installed");
      report_configuration_error(loaded);
      publish(session_result::configuration_invalid);
      return;
    }

    g_settings = loaded.value;
    if (loaded.status == config::load_status::defaults_used) {
      report(L"no configuration file was found; this build's defaults apply");
    }
    if (!g_settings.enabled) {
      report(L"the configuration turns the feature off; forwarding continues"
             L" but no routing is installed");
      publish(session_result::disabled);
      return;
    }

    // The sidecar coordinator remains available to an explicitly selected
    // fallback, but it is no longer the production bootstrap's default action.
    // Publishing this boundary makes the unfinished M3 state explicit and
    // guarantees that the primary path starts no external runtime.
    report(L"configuration accepted; in-process injection is pending");
    publish(session_result::injection_pending);
  } catch (...) {
    // Configuration handling allocates, and a bootstrap failure must never
    // become a host failure.
    report(L"the configuration could not be processed;"
           L" forwarding continues but no routing is installed");
    publish(session_result::configuration_invalid);
  }
}

}  // namespace

session_result current_session_result() noexcept {
  return static_cast<session_result>(InterlockedCompareExchange(&g_result, 0, 0));
}

const config::settings& session_settings() noexcept {
  return g_settings;
}

void prepare_session() noexcept {
  // Resolving here rather than from a thunk keeps the module load off whatever
  // thread the host happens to call WinMM from first. A missing or substituted
  // backend still stops the process inside this call.
  ensure_backend_resolved();

  void* const backend = resolved_backend();
  if (backend == nullptr) {
    report(L"the backend was not resolved, so the host surface was not verified");
    publish(session_result::backend_unresolved);
    return;
  }

  surface_shape host{};
  if (!module_surface_shape(backend, &host)) {
    report(L"the backend has no readable export directory");
    publish(session_result::surface_unreadable);
    return;
  }

  const surface_shape pinned = pinned_shape();
  if (host.ordinal_base != pinned.ordinal_base ||
      host.function_count != pinned.function_count ||
      host.name_count != pinned.name_count) {
    // Resolution already proved every pinned entry exists in the backend, so a
    // differing count means the host exports entries this build does not, and
    // this proxy would shadow them. Report it and leave the feature off rather
    // than stop a client that is otherwise working.
    report(L"the host WinMM surface differs from the surface this build pins;"
           L" forwarding continues but no routing is installed");
    report_shape(L"pinned", pinned);
    report_shape(L"host", host);
    publish(session_result::surface_mismatch);
    return;
  }

  publish(session_result::verified);
  apply_configuration();
}

}  // namespace ncm::winmm_proxy
