#include "ncm/host/ncm_watch.hpp"
#include "ncm/launcher/mitm_certs.hpp"
#include "ncm/launcher/unm_sidecar.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr wchar_t k_mutex_name[] = L"Local\\UnblockNeteaseMusic-Lite";
constexpr wchar_t k_stop_name[] = L"Local\\UnblockNeteaseMusic-Lite-Stop";
constexpr std::uint16_t k_default_http = 3412;
constexpr std::uint16_t k_default_https = 3413;

void host_log(const std::string& message) {
  wchar_t temp_directory[MAX_PATH]{};
  const auto length = GetTempPathW(MAX_PATH, temp_directory);
  if (length == 0 || length >= MAX_PATH) {
    return;
  }
  const auto path =
      std::filesystem::path(temp_directory) / L"unm-host-lite.log";
  std::ofstream stream(path, std::ios::app);
  if (!stream) {
    return;
  }
  SYSTEMTIME now{};
  GetLocalTime(&now);
  stream << now.wYear << '-' << now.wMonth << '-' << now.wDay << ' '
         << now.wHour << ':' << now.wMinute << ':' << now.wSecond << '.'
         << now.wMilliseconds << ' ' << message << '\n';
}

std::string narrow_path(const std::filesystem::path& path) {
  const auto text = path.wstring();
  if (text.empty()) {
    return {};
  }
  const auto needed = WideCharToMultiByte(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
      nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(needed), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(),
      needed, nullptr, nullptr);
  return result;
}

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

[[nodiscard]] bool is_valid_source_name(std::wstring_view token) {
  if (token.empty() || token.size() > 64) {
    return false;
  }
  // Match-order source ids only (ytdl, kugou, ...). Reject hosts/IPs like 127.0.0.1.
  if (token.find(L'.') != std::wstring_view::npos ||
      token.find(L':') != std::wstring_view::npos ||
      token.find(L'/') != std::wstring_view::npos ||
      token.find(L'\\') != std::wstring_view::npos) {
    return false;
  }
  if (!((token.front() >= L'A' && token.front() <= L'Z') ||
        (token.front() >= L'a' && token.front() <= L'z'))) {
    return false;
  }
  for (const auto character : token) {
    const auto letter = (character >= L'A' && character <= L'Z') ||
        (character >= L'a' && character <= L'z');
    const auto digit = character >= L'0' && character <= L'9';
    if (!letter && !digit && character != L'_' && character != L'-') {
      return false;
    }
  }
  return true;
}

void append_sources(std::vector<std::wstring>& sources, std::wstring_view value) {
  std::wstring token;
  const auto flush = [&]() {
    if (!token.empty()) {
      if (is_valid_source_name(token)) {
        sources.push_back(token);
      } else {
        host_log("ignoring invalid source token");
      }
      token.clear();
    }
  };
  for (const auto character : value) {
    if (character == L',' || character == L';' || character == L' ') {
      flush();
    } else {
      token.push_back(character);
    }
  }
  flush();
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
    host_log("stop: event missing");
    return 0;
  }
  (void)SetEvent(event);
  CloseHandle(event);
  host_log("stop: signaled");
  return 0;
}

[[nodiscard]] std::filesystem::path host_module_directory() {
  std::wstring buffer(MAX_PATH, L'\0');
  for (;;) {
    const auto length = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
      return {};
    }
    if (length < buffer.size()) {
      buffer.resize(length);
      return std::filesystem::path(buffer).parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

int run_supervisor(const options& settings) {
  host_log(
      "supervisor ncm=" + narrow_path(settings.ncm) +
      " unm=" + narrow_path(settings.unm) +
      " http=" + std::to_string(settings.http_port) +
      " https=" + std::to_string(settings.https_port) +
      " sources=" + std::to_string(settings.sources.size()));
  const auto mutex = CreateMutexW(nullptr, TRUE, k_mutex_name);
  if (mutex == nullptr) {
    host_log("mutex create failed err=" + std::to_string(GetLastError()));
    return 1;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    host_log("mutex already held; exiting 0");
    CloseHandle(mutex);
    return 0;
  }

  const auto stop = CreateEventW(nullptr, TRUE, FALSE, k_stop_name);
  if (stop == nullptr) {
    host_log("stop event create failed err=" + std::to_string(GetLastError()));
    CloseHandle(mutex);
    return 1;
  }
  (void)ResetEvent(stop);

  auto session = ncm::host::ncm_watch::attach(settings.ncm);
  if (!session.has_value() || session->wait_handle() == nullptr) {
    host_log("attach failed");
    CloseHandle(stop);
    CloseHandle(mutex);
    return 1;
  }
  host_log("attach ok pid=" + std::to_string(session->process_id()));

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

  const auto host_directory = host_module_directory();
  const auto material = ncm::launcher::resolve_mitm_material(
      host_directory, settings.unm.parent_path());
  if (!material.has_value()) {
    host_log(
        "mitm material missing; place certs/ca.crt, server.crt, server.key "
        "next to the plugin (certs/) or beside UNM");
    CloseHandle(stop);
    CloseHandle(mutex);
    return 1;
  }
  host_log(
      "mitm material ca=" + narrow_path(material->ca_certificate) +
      " leaf=" + narrow_path(material->server_certificate));
  try {
    const auto already_trusted =
        ncm::launcher::current_user_root_contains(material->ca_certificate);
    ncm::launcher::ensure_current_user_root_trust(material->ca_certificate);
    host_log(
        already_trusted ? "mitm CA already trusted (CurrentUser Root)"
                        : "mitm CA installed into CurrentUser Root");
  } catch (const std::exception& ex) {
    host_log(std::string("mitm CA trust failed: ") + ex.what());
    CloseHandle(stop);
    CloseHandle(mutex);
    return 1;
  }
  sidecar_options.environment =
      ncm::launcher::mitm_sign_environment(*material);

  int exit_code = 0;
  try {
    auto sidecar = ncm::launcher::unm_sidecar::launch(sidecar_options);
    host_log(
        "sidecar ready http=" + std::to_string(sidecar.http_port()) +
        " https=" + std::to_string(sidecar.https_port()));
    HANDLE waits[] = {
        static_cast<HANDLE>(session->wait_handle()), stop};
    for (;;) {
      const auto status = WaitForMultipleObjects(2, waits, FALSE, 1000);
      if (status == WAIT_OBJECT_0 || status == WAIT_OBJECT_0 + 1) {
        host_log(
            status == WAIT_OBJECT_0 ? "ncm exited" : "stop requested");
        break;
      }
      if (status != WAIT_TIMEOUT) {
        host_log("wait failed status=" + std::to_string(status));
        exit_code = 1;
        break;
      }
      if (sidecar.process().wait_for_tree(std::chrono::milliseconds::zero())) {
        host_log("sidecar tree exited");
        exit_code = 1;
        break;
      }
      if (!session->alive()) {
        host_log("ncm no longer alive");
        break;
      }
    }
    (void)sidecar.process().terminate_and_wait_tree(0, std::chrono::seconds(5));
  } catch (const std::exception& ex) {
    host_log(std::string("sidecar exception: ") + ex.what());
    exit_code = 1;
  } catch (...) {
    host_log("sidecar unknown exception");
    exit_code = 1;
  }

  CloseHandle(stop);
  CloseHandle(mutex);
  host_log("supervisor exit=" + std::to_string(exit_code));
  return exit_code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  host_log("wWinMain start");
  int argc{};
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argv == nullptr) {
    host_log("CommandLineToArgvW failed");
    return 1;
  }
  {
    std::string joined = "argc=" + std::to_string(argc);
    for (int index = 0; index < argc; ++index) {
      joined.push_back(' ');
      joined += narrow_path(std::filesystem::path(argv[index]));
    }
    host_log(joined);
  }
  const auto parsed = parse_options(argc, argv);
  LocalFree(argv);
  if (!parsed.has_value()) {
    host_log("parse_options failed");
    return 1;
  }
  if (parsed->stop) {
    return request_stop();
  }
  return run_supervisor(*parsed);
}
