#include "ncm/host/ncm_watch.hpp"
#include "ncm/launcher/unm_sidecar.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t k_mutex_name[] = L"Local\\UnblockNeteaseMusic-Lite";
constexpr wchar_t k_stop_name[] = L"Local\\UnblockNeteaseMusic-Lite-Stop";
constexpr std::uint16_t k_default_http = 3412;
constexpr std::uint16_t k_default_https = 3413;

struct options {
  bool stop{};
  std::filesystem::path ncm;
  std::filesystem::path unm;
  std::uint16_t http_port{k_default_http};
  std::uint16_t https_port{k_default_https};
  std::vector<std::wstring> sources;
};

[[nodiscard]] std::optional<std::uint16_t> parse_port(std::wstring_view text) {
  if (text.empty()) {
    return std::nullopt;
  }
  wchar_t* end{};
  const auto value = std::wcstoul(std::wstring(text).c_str(), &end, 10);
  if (end == nullptr || *end != L'\0' || value == 0 || value > 65535) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(value);
}

void append_sources(std::vector<std::wstring>& sources, std::wstring_view value) {
  std::wstring token;
  for (const auto character : value) {
    if (character == L',' || character == L';' || character == L' ') {
      if (!token.empty()) {
        sources.push_back(token);
        token.clear();
      }
    } else {
      token.push_back(character);
    }
  }
  if (!token.empty()) {
    sources.push_back(token);
  }
}

[[nodiscard]] std::optional<options> parse_options(int argc, wchar_t** argv) {
  options result;
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view argument(argv[index]);
    if (argument == L"--stop") {
      result.stop = true;
      continue;
    }
    if (index + 1 >= argc) {
      return std::nullopt;
    }
    const std::wstring_view value(argv[++index]);
    if (argument == L"--ncm") {
      result.ncm = std::filesystem::path(value).lexically_normal();
    } else if (argument == L"--unm") {
      result.unm = std::filesystem::path(value).lexically_normal();
    } else if (argument == L"--http") {
      const auto port = parse_port(value);
      if (!port.has_value()) {
        return std::nullopt;
      }
      result.http_port = *port;
    } else if (argument == L"--https") {
      const auto port = parse_port(value);
      if (!port.has_value()) {
        return std::nullopt;
      }
      result.https_port = *port;
    } else if (argument == L"--sources") {
      result.sources.clear();
      append_sources(result.sources, value);
    } else {
      return std::nullopt;
    }
  }
  if (result.stop) {
    return result;
  }
  std::error_code code;
  if (result.ncm.empty() || !result.ncm.is_absolute() ||
      result.unm.empty() || !result.unm.is_absolute() ||
      result.http_port == 0 || result.https_port == 0 ||
      result.http_port == result.https_port ||
      !std::filesystem::is_regular_file(result.ncm, code) || code ||
      !std::filesystem::is_regular_file(result.unm, code) || code) {
    return std::nullopt;
  }
  return result;
}

int request_stop() {
  const auto event = OpenEventW(EVENT_MODIFY_STATE, FALSE, k_stop_name);
  if (event == nullptr) {
    return 0;
  }
  (void)SetEvent(event);
  CloseHandle(event);
  return 0;
}

int run_supervisor(const options& settings) {
  const auto mutex = CreateMutexW(nullptr, TRUE, k_mutex_name);
  if (mutex == nullptr) {
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return 0;
  }

  const auto stop = CreateEventW(nullptr, TRUE, FALSE, k_stop_name);
  if (stop == nullptr) {
    CloseHandle(mutex);
    return 1;
  }
  (void)ResetEvent(stop);

  auto session = ncm::host::ncm_watch::attach(settings.ncm);
  if (!session.has_value() || session->wait_handle() == nullptr) {
    CloseHandle(stop);
    CloseHandle(mutex);
    return 1;
  }

  ncm::launcher::unm_sidecar_options sidecar_options;
  sidecar_options.executable = settings.unm;
  sidecar_options.working_directory = settings.unm.parent_path();
  sidecar_options.fixed_http_port = settings.http_port;
  sidecar_options.fixed_https_port = settings.https_port;
  sidecar_options.readiness_timeout = std::chrono::seconds(10);
  if (!settings.sources.empty()) {
    sidecar_options.arguments.emplace_back(L"-o");
    sidecar_options.arguments.insert(
        sidecar_options.arguments.end(), settings.sources.begin(),
        settings.sources.end());
  }

  int exit_code = 0;
  try {
    auto sidecar = ncm::launcher::unm_sidecar::launch(sidecar_options);
    HANDLE waits[] = {
        static_cast<HANDLE>(session->wait_handle()), stop};
    for (;;) {
      const auto status = WaitForMultipleObjects(2, waits, FALSE, 1000);
      if (status == WAIT_OBJECT_0 || status == WAIT_OBJECT_0 + 1) {
        break;
      }
      if (status != WAIT_TIMEOUT) {
        exit_code = 1;
        break;
      }
      if (sidecar.process().wait_for_tree(std::chrono::milliseconds::zero())) {
        exit_code = 1;
        break;
      }
      if (!session->alive()) {
        break;
      }
    }
    (void)sidecar.process().terminate_and_wait_tree(0, std::chrono::seconds(5));
  } catch (...) {
    exit_code = 1;
  }

  CloseHandle(stop);
  CloseHandle(mutex);
  return exit_code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int argc{};
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    return 1;
  }
  const auto parsed = parse_options(argc, argv);
  LocalFree(argv);
  if (!parsed.has_value()) {
    return 1;
  }
  if (parsed->stop) {
    return request_stop();
  }
  return run_supervisor(*parsed);
}
