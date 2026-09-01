#include "ncm/config/settings.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ncm::config {
namespace {

constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
  const auto is_space = [](char character) {
    return character == ' ' || character == '\t' || character == '\r';
  };
  while (!text.empty() && is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && is_space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string lowered(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
    return static_cast<char>(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
  });
  return result;
}

[[nodiscard]] std::wstring widen(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const int needed = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      result.data(), needed);
  return result;
}

[[nodiscard]] load_result reject(std::wstring diagnostic, unsigned line) {
  load_result result{};
  result.status = load_status::invalid;
  result.diagnostic = std::move(diagnostic);
  result.line = line;
  return result;
}

[[nodiscard]] bool parse_boolean(std::string_view value, bool* out) noexcept {
  const std::string text = lowered(value);
  if (text == "true" || text == "yes" || text == "1" || text == "on") {
    *out = true;
    return true;
  }
  if (text == "false" || text == "no" || text == "0" || text == "off") {
    *out = false;
    return true;
  }
  return false;
}

// Decimal only, no sign, no leading or trailing text. `auto` is handled by the
// caller because only the port keys accept it.
[[nodiscard]] bool parse_number(std::string_view value, unsigned long* out) noexcept {
  if (value.empty() || value.size() > 10) {
    return false;
  }
  unsigned long accumulated = 0;
  for (const char character : value) {
    if (character < '0' || character > '9') {
      return false;
    }
    accumulated = accumulated * 10 + static_cast<unsigned long>(character - '0');
  }
  *out = accumulated;
  return true;
}

[[nodiscard]] bool parse_port(
    std::string_view value, std::optional<std::uint16_t>* out) noexcept {
  if (lowered(value) == "auto") {
    out->reset();
    return true;
  }
  unsigned long number = 0;
  if (!parse_number(value, &number) || number < 1 || number > 65535) {
    return false;
  }
  *out = static_cast<std::uint16_t>(number);
  return true;
}

}  // namespace

load_result parse_settings(
    std::string_view text, const std::filesystem::path& package_directory) {
  if (text.starts_with(utf8_bom)) {
    text.remove_prefix(utf8_bom.size());
  }

  settings value{};
  value.sidecar_executable = package_directory / default_sidecar_file_name;

  bool saw_enabled = false;
  bool saw_sidecar = false;
  bool saw_http_port = false;
  bool saw_https_port = false;
  bool saw_attempts = false;
  bool saw_timeout = false;

  unsigned line_number = 0;
  std::size_t offset = 0;
  while (offset <= text.size()) {
    const std::size_t end = text.find('\n', offset);
    const std::string_view raw =
        text.substr(offset, end == std::string_view::npos ? text.size() - offset : end - offset);
    offset = end == std::string_view::npos ? text.size() + 1 : end + 1;
    ++line_number;

    const std::string_view line = trim(raw);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }

    const std::size_t separator = line.find('=');
    if (separator == std::string_view::npos) {
      return reject(L"a line is not a 'key = value' pair", line_number);
    }
    const std::string key = lowered(trim(line.substr(0, separator)));
    const std::string_view raw_value = trim(line.substr(separator + 1));
    if (key.empty()) {
      return reject(L"a line has an empty key", line_number);
    }

    // A repeated key states two intents; honoring the last one silently would
    // hide the contradiction.
    bool repeated = false;
    const auto duplicate = [&](bool& seen) {
      repeated = seen;
      seen = true;
      return repeated;
    };
    const auto repeated_key = [&] {
      return reject(L"'" + widen(key) + L"' appears more than once", line_number);
    };
    const auto bad_value = [&] {
      return reject(
          L"the value of '" + widen(key) + L"' is not one this build accepts",
          line_number);
    };

    if (key == "enabled") {
      if (duplicate(saw_enabled)) {
        return repeated_key();
      }
      if (!parse_boolean(raw_value, &value.enabled)) {
        return bad_value();
      }
    } else if (key == "sidecar_executable") {
      if (duplicate(saw_sidecar)) {
        return repeated_key();
      }
      const std::wstring widened = widen(raw_value);
      if (widened.empty()) {
        return bad_value();
      }
      std::filesystem::path configured(widened);
      // A relative path keeps the package portable; an absolute one is taken
      // as written so a shared sidecar can live outside the package.
      value.sidecar_executable =
          configured.is_absolute() ? configured : package_directory / configured;
    } else if (key == "http_port") {
      if (duplicate(saw_http_port)) {
        return repeated_key();
      }
      if (!parse_port(raw_value, &value.http_port)) {
        return bad_value();
      }
    } else if (key == "https_port") {
      if (duplicate(saw_https_port)) {
        return repeated_key();
      }
      if (!parse_port(raw_value, &value.https_port)) {
        return bad_value();
      }
    } else if (key == "automatic_attempts") {
      if (duplicate(saw_attempts)) {
        return repeated_key();
      }
      unsigned long number = 0;
      if (!parse_number(raw_value, &number) || number < 1 || number > 10) {
        return bad_value();
      }
      value.automatic_attempts = static_cast<std::size_t>(number);
    } else if (key == "readiness_timeout_ms") {
      if (duplicate(saw_timeout)) {
        return repeated_key();
      }
      unsigned long number = 0;
      if (!parse_number(raw_value, &number) || number < 1 || number > 600000) {
        return bad_value();
      }
      value.readiness_timeout = std::chrono::milliseconds(number);
    } else {
      return reject(
          L"'" + widen(key) + L"' is not a key this build understands", line_number);
    }
  }

  // A sidecar that redirects HTTPS CONNECT needs a distinct pair, so the two
  // ports must be configured the same way and must not collide.
  if (value.http_port.has_value() != value.https_port.has_value()) {
    return reject(
        L"'http_port' and 'https_port' must both be fixed or both be automatic",
        0);
  }
  if (value.http_port.has_value() && *value.http_port == *value.https_port) {
    return reject(L"'http_port' and 'https_port' must differ", 0);
  }

  load_result result{};
  result.status = load_status::loaded;
  result.value = std::move(value);
  return result;
}

load_result load_settings(const std::filesystem::path& package_directory) {
  const std::filesystem::path file = package_directory / settings_file_name;

  std::error_code code;
  if (!std::filesystem::exists(file, code) || code) {
    load_result result{};
    result.status = load_status::defaults_used;
    result.value.sidecar_executable = package_directory / default_sidecar_file_name;
    return result;
  }

  std::ifstream stream(file, std::ios::binary);
  if (!stream) {
    return reject(L"the configuration file exists but could not be opened", 0);
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  if (stream.bad()) {
    return reject(L"the configuration file could not be read", 0);
  }
  const std::string text = buffer.str();
  return parse_settings(text, package_directory);
}

std::filesystem::path package_directory() noexcept {
  HMODULE module{};
  if (GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&package_directory), &module) == 0) {
    return {};
  }
  wchar_t buffer[MAX_PATH]{};
  const DWORD written = GetModuleFileNameW(module, buffer, MAX_PATH);
  if (written == 0 || written >= MAX_PATH) {
    return {};
  }
  try {
    return std::filesystem::path(buffer).parent_path();
  } catch (...) {
    return {};
  }
}

}  // namespace ncm::config
