#include "ncm/winmm_proxy/session.hpp"

#include "ncm/cef_injection/app_wrapper.hpp"
#include "ncm/cef_injection/import_hook.hpp"
#include "ncm/winmm_proxy/forwarder.hpp"

#include <Windows.h>

#include <cstdio>
#include <filesystem>

namespace ncm::winmm_proxy {
namespace {

long g_result{static_cast<long>(session_result::pending)};
config::settings g_settings{};

[[nodiscard]] bool client_image() noexcept {
  wchar_t path[MAX_PATH]{};
  const DWORD written = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (written == 0 || written >= MAX_PATH) return false;
  const wchar_t* name = path;
  for (DWORD index = 0; index < written; ++index) {
    if (path[index] == L'\\' || path[index] == L'/') name = path + index + 1;
  }
  return _wcsicmp(name, L"cloudmusic.exe") == 0;
}

[[nodiscard]] const char* registration_name() noexcept {
  switch (cef_injection::current_registration_state()) {
    case cef_injection::registration_state::not_attempted: return "not_attempted";
    case cef_injection::registration_state::succeeded: return "succeeded";
    case cef_injection::registration_state::failed: return "failed";
  }
  return "unknown";
}

[[nodiscard]] const char* anchors_name() noexcept {
  switch (cef_injection::current_anchors_state()) {
    case cef_injection::anchors_state::pending: return "pending";
    case cef_injection::anchors_state::found: return "found";
    case cef_injection::anchors_state::missing: return "missing";
  }
  return "unknown";
}

[[nodiscard]] bool injection_reporting_enabled() noexcept {
  return GetEnvironmentVariableW(L"NCM_INJECTION_REPORT_DIR", nullptr, 0) != 0;
}

void write_injection_report(cef_injection::import_hook_result hook) noexcept {
  wchar_t directory[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(
      L"NCM_INJECTION_REPORT_DIR", directory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return;

  wchar_t path[MAX_PATH]{};
  if (_snwprintf_s(
          path, _TRUNCATE, L"%s\\injection-%lu.txt", directory,
          GetCurrentProcessId()) <= 0) return;
  const HANDLE file = CreateFileW(
      path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  char line[320]{};
  const int written = sprintf_s(
      line,
      "pid=%lu hook=%s registration=%s marker=%ld anchors=%s intercepts=%ld\n",
      GetCurrentProcessId(), cef_injection::describe(hook), registration_name(),
      cef_injection::current_marker_count(), anchors_name(),
      cef_injection::current_intercept_count());
  if (written > 0) {
    DWORD ignored{};
    WriteFile(file, line, static_cast<DWORD>(written), &ignored, nullptr);
  }
  CloseHandle(file);
}

void observe_injection(cef_injection::import_hook_result hook) noexcept {
  // Runtime polling exists only for an explicitly requested bounded
  // investigation. Normal product startup performs no observation loop.
  if (!injection_reporting_enabled()) return;
  write_injection_report(hook);
  if (hook != cef_injection::import_hook_result::installed &&
      hook != cef_injection::import_hook_result::already_installed) return;
  const auto marker_deadline = GetTickCount64() + 60000;
  while (cef_injection::current_registration_state() ==
             cef_injection::registration_state::not_attempted &&
         GetTickCount64() < marker_deadline) {
    Sleep(100);
  }
  // Registration success alone is not marker clearance. Keep polling until a
  // native Execute is observed or the same deadline expires.
  while (cef_injection::current_marker_count() <= 0 &&
         GetTickCount64() < marker_deadline) {
    Sleep(100);
  }
  write_injection_report(hook);

  // After marker observation, keep rewriting the report for a long operator-
  // play window so deferred anchors/intercepts appear without new IPC. The
  // product path remains poll-free when the investigation env var is unset.
  const auto observation_deadline = GetTickCount64() + 900000;
  while (GetTickCount64() < observation_deadline) {
    Sleep(1000);
    write_injection_report(hook);
  }
}

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
    const auto hook = client_image()
        ? cef_injection::install_loaded_import_hook(10000)
        : cef_injection::install_loaded_import_hook();
    if (hook == cef_injection::import_hook_result::installed ||
        hook == cef_injection::import_hook_result::already_installed) {
      report(L"the CEF process entry import is wrapped");
      publish(session_result::injection_installed);
    } else if (!client_image() &&
               hook == cef_injection::import_hook_result::client_not_loaded) {
      // Synthetic session hosts do not contain cloudmusic.dll. Preserve their
      // pre-M3 boundary without weakening the real-client path.
      publish(session_result::injection_pending);
    } else {
      report(L"CEF import injection declined; forwarding continues");
      publish(session_result::injection_failed);
    }
    observe_injection(hook);
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
