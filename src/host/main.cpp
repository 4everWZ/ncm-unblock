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
// Product match-order default: bodian first under FOLLOW_SOURCE_ORDER.
// migu-first pays ~10s request timeouts on misses before falling through;
// kuwo stays omitted (anonymous VIP promo clip).
constexpr std::wstring_view k_default_sources[] = {L"bodian", L"migu", L"kugou"};

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

[[nodiscard]] std::filesystem::path host_module_directory();

// One argv token per UTF-8 line. Empty lines and # comments are ignored.
// Used so the plugin can start the host with zero command-line values and avoid
// powershell.exe (BetterNCM ShellExecute re-joins args without re-quoting, so
// paths with spaces cannot safely ride on lpParameters).
[[nodiscard]] std::optional<std::vector<std::wstring>> load_args_file_tokens(
    const std::filesystem::path& path) {
  std::error_code code;
  if (path.empty() || !path.is_absolute() ||
      !std::filesystem::is_regular_file(path, code) || code) {
    return std::nullopt;
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  std::string bytes((std::istreambuf_iterator<char>(stream)),
                    std::istreambuf_iterator<char>());
  if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xef &&
      static_cast<unsigned char>(bytes[1]) == 0xbb &&
      static_cast<unsigned char>(bytes[2]) == 0xbf) {
    bytes.erase(0, 3);
  }
  if (bytes.size() > 64 * 1024) {
    host_log("args file too large");
    return std::nullopt;
  }
  std::wstring wide;
  if (!bytes.empty()) {
    const auto needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
        static_cast<int>(bytes.size()), nullptr, 0);
    if (needed <= 0) {
      host_log("args file is not valid UTF-8");
      return std::nullopt;
    }
    wide.resize(static_cast<std::size_t>(needed));
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(),
            static_cast<int>(bytes.size()), wide.data(), needed) <= 0) {
      host_log("args file UTF-8 decode failed");
      return std::nullopt;
    }
  }
  std::vector<std::wstring> tokens;
  std::wstring line;
  const auto flush_line = [&]() {
    while (!line.empty() &&
           (line.back() == L' ' || line.back() == L'\t' || line.back() == L'\r')) {
      line.pop_back();
    }
    std::size_t start = 0;
    while (start < line.size() &&
           (line[start] == L' ' || line[start] == L'\t')) {
      ++start;
    }
    if (start > 0) {
      line.erase(0, start);
    }
    if (!line.empty() && line.front() != L'#') {
      tokens.push_back(line);
    }
    line.clear();
  };
  for (const auto character : wide) {
    if (character == L'\n') {
      flush_line();
    } else {
      line.push_back(character);
    }
  }
  flush_line();
  return tokens;
}

[[nodiscard]] std::optional<options> parse_option_tokens(
    const std::vector<std::wstring>& tokens) {
  options result;
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    const std::wstring_view argument(tokens[index]);
    if (argument == L"--stop") {
      result.stop = true;
      continue;
    }
    if (argument == L"--args-file") {
      // Expanded by the caller before parse_option_tokens.
      return std::nullopt;
    }
    if (index + 1 >= tokens.size()) {
      return std::nullopt;
    }
    const std::wstring_view value(tokens[++index]);
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

[[nodiscard]] std::optional<options> parse_options(int argc, wchar_t** argv) {
  std::vector<std::wstring> tokens;
  tokens.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
  for (int index = 1; index < argc; ++index) {
    tokens.emplace_back(argv[index]);
  }

  // Exclusive --args-file <path> (optional leading/trailing --stop handled in file
  // or as a sole argv token before expansion).
  std::optional<std::filesystem::path> args_file;
  std::vector<std::wstring> without_args_file;
  without_args_file.reserve(tokens.size());
  for (std::size_t index = 0; index < tokens.size(); ++index) {
    if (tokens[index] == L"--args-file") {
      if (index + 1 >= tokens.size() || args_file.has_value()) {
        return std::nullopt;
      }
      args_file = std::filesystem::path(tokens[index + 1]).lexically_normal();
      ++index;
      continue;
    }
    without_args_file.push_back(tokens[index]);
  }

  if (args_file.has_value()) {
    if (!without_args_file.empty()) {
      // Only --stop may combine with --args-file on the process command line.
      if (without_args_file.size() != 1 || without_args_file[0] != L"--stop") {
        return std::nullopt;
      }
    }
    const auto file_tokens = load_args_file_tokens(*args_file);
    if (!file_tokens.has_value()) {
      host_log("args file load failed path=" + narrow_path(*args_file));
      return std::nullopt;
    }
    host_log("args file loaded path=" + narrow_path(*args_file) +
             " tokens=" + std::to_string(file_tokens->size()));
    auto parsed = parse_option_tokens(*file_tokens);
    if (parsed.has_value() && !without_args_file.empty()) {
      parsed->stop = true;
    }
    return parsed;
  }

  if (!without_args_file.empty()) {
    return parse_option_tokens(without_args_file);
  }

  // Plugin direct start: no lpParameters (avoids flash + quote loss). Read
  // launch.args beside unm-host.exe (plugin writes native/launch.args).
  const auto default_file = host_module_directory() / L"launch.args";
  const auto file_tokens = load_args_file_tokens(default_file);
  if (!file_tokens.has_value()) {
    host_log("default launch.args missing path=" + narrow_path(default_file));
    return std::nullopt;
  }
  host_log("default launch.args loaded path=" + narrow_path(default_file) +
           " tokens=" + std::to_string(file_tokens->size()));
  return parse_option_tokens(*file_tokens);
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

[[nodiscard]] bool path_has_extension(
    const std::filesystem::path& path, std::wstring_view extension) {
  auto actual = path.extension().wstring();
  if (actual.size() != extension.size()) {
    return false;
  }
  for (std::size_t index = 0; index < actual.size(); ++index) {
    const auto left = actual[index];
    const auto right = extension[index];
    const auto fold = [](wchar_t value) {
      return (value >= L'A' && value <= L'Z')
          ? static_cast<wchar_t>(value - L'A' + L'a')
          : value;
    };
    if (fold(left) != fold(right)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<std::filesystem::path> resolve_node_binary() {
  wchar_t override_buffer[MAX_PATH]{};
  const auto override_length = GetEnvironmentVariableW(
      L"NODE_BINARY", override_buffer, MAX_PATH);
  if (override_length > 0 && override_length < MAX_PATH) {
    std::error_code code;
    const std::filesystem::path candidate(override_buffer);
    if (std::filesystem::is_regular_file(candidate, code) && !code) {
      return candidate.lexically_normal();
    }
    host_log(
        "NODE_BINARY set but not a regular file: " + narrow_path(candidate));
  }

  wchar_t found[MAX_PATH]{};
  const auto length = SearchPathW(nullptr, L"node.exe", nullptr, MAX_PATH, found, nullptr);
  if (length > 0 && length < MAX_PATH) {
    return std::filesystem::path(found).lexically_normal();
  }

  // Common nvm-windows default install (optional fallback; PATH is preferred).
  const std::filesystem::path nvm_default = L"C:\\nvm4w\\nodejs\\node.exe";
  std::error_code code;
  if (std::filesystem::is_regular_file(nvm_default, code) && !code) {
    return nvm_default.lexically_normal();
  }
  return std::nullopt;
}

struct unm_launch_target {
  std::filesystem::path executable;
  std::filesystem::path working_directory;
  std::vector<std::wstring> prefix_arguments;
  bool js_mode{};
};

[[nodiscard]] std::optional<unm_launch_target> resolve_unm_launch_target(
    const std::filesystem::path& unm) {
  unm_launch_target target;
  target.working_directory = unm.parent_path();
  if (path_has_extension(unm, L".js")) {
    const auto node = resolve_node_binary();
    if (!node.has_value()) {
      host_log(
          "UNM entry is .js but node.exe was not found on PATH, NODE_BINARY, "
          "or C:\\nvm4w\\nodejs\\node.exe");
      return std::nullopt;
    }
    target.executable = *node;
    target.prefix_arguments = {unm.wstring()};
    target.js_mode = true;
    host_log(
        "unm mode=js node=" + narrow_path(*node) +
        " app=" + narrow_path(unm));
    return target;
  }
  target.executable = unm;
  target.js_mode = false;
  host_log("unm mode=exe path=" + narrow_path(unm));
  return target;
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

  const auto launch = resolve_unm_launch_target(settings.unm);
  if (!launch.has_value()) {
    CloseHandle(stop);
    CloseHandle(mutex);
    return 1;
  }

  ncm::launcher::unm_sidecar_options sidecar_options;
  sidecar_options.executable = launch->executable;
  sidecar_options.working_directory = launch->working_directory;
  sidecar_options.fixed_http_port = settings.http_port;
  sidecar_options.fixed_https_port = settings.https_port;
  sidecar_options.readiness_timeout = std::chrono::seconds(10);
  sidecar_options.arguments = launch->prefix_arguments;
  sidecar_options.arguments.emplace_back(L"-o");
  if (!settings.sources.empty()) {
    sidecar_options.arguments.insert(
        sidecar_options.arguments.end(), settings.sources.begin(),
        settings.sources.end());
  } else {
    for (const auto source : k_default_sources) {
      sidecar_options.arguments.emplace_back(source);
    }
  }

  const auto host_directory = host_module_directory();
  // Prefer plugin certs (host lives under native/); UNM parent is core/ or
  // user data dir — still accepted as secondary root.
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
  sidecar_options.environment.emplace_back(L"ENABLE_FLAC", L"true");
  sidecar_options.environment.emplace_back(L"FOLLOW_SOURCE_ORDER", L"true");
  // UNM hook: local red+/SVIP membership on vip/info when NCM is logged in.
  // Privilege level floors (plLevel→lossless) come from the patched JS bundle.
  sidecar_options.environment.emplace_back(L"ENABLE_LOCAL_VIP", L"svip");

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
