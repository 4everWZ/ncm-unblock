// Host for the session-owned sidecar experiment.
//
// The checks run in a dedicated process so package configuration is read from a
// staged directory and so terminating this process is terminating the exact
// NCM-session stand-in that owns the sidecar job.

#include "ncm/winmm_proxy/session.hpp"

#include <Windows.h>

#include <cstdio>
#include <string>

namespace {

[[nodiscard]] const char* result_name(ncm::winmm_proxy::session_result result) noexcept {
  switch (result) {
    case ncm::winmm_proxy::session_result::pending:
      return "pending";
    case ncm::winmm_proxy::session_result::verified:
      return "verified";
    case ncm::winmm_proxy::session_result::surface_mismatch:
      return "surface_mismatch";
    case ncm::winmm_proxy::session_result::surface_unreadable:
      return "surface_unreadable";
    case ncm::winmm_proxy::session_result::backend_unresolved:
      return "backend_unresolved";
    case ncm::winmm_proxy::session_result::configuration_invalid:
      return "configuration_invalid";
    case ncm::winmm_proxy::session_result::disabled:
      return "disabled";
    case ncm::winmm_proxy::session_result::sidecar_ready:
      return "sidecar_ready";
    case ncm::winmm_proxy::session_result::sidecar_failed:
      return "sidecar_failed";
  }
  return "unknown";
}

void write_report(const wchar_t* path) {
  char line[256]{};
  const int written = sprintf_s(
      line, "result=%s pid=%lu http=%u https=%u owner=%lu",
      result_name(ncm::winmm_proxy::current_session_result()),
      ncm::winmm_proxy::session_sidecar_process_id(),
      static_cast<unsigned>(ncm::winmm_proxy::session_sidecar_http_port()),
      static_cast<unsigned>(ncm::winmm_proxy::session_sidecar_https_port()),
      GetCurrentProcessId());
  if (written <= 0) {
    return;
  }

  const HANDLE file = CreateFileW(
      path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD ignored{};
  WriteFile(file, line, static_cast<DWORD>(written), &ignored, nullptr);
  CloseHandle(file);
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 2) {
    return 2;
  }

  ncm::winmm_proxy::prepare_session();
  write_report(arguments[1]);
  Sleep(120000);
  return 0;
}
