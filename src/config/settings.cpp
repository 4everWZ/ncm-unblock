#include "ncm/config/settings.hpp"

#include <Windows.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace ncm::config {
namespace {

constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
  const auto whitespace = [](char value) {
    return value == ' ' || value == '\t' || value == '\r';
  };
  while (!text.empty() && whitespace(text.front())) {
    text.remove_prefix(1);
  }
  while (!text.empty() && whitespace(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

[[nodiscard]] std::string lower(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
    return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
  });
  return result;
}

[[nodiscard]] std::wstring widen(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  const auto count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  if (count <= 0) {
    return {};
  }
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(
          CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
          static_cast<int>(text.size()), result.data(), count) != count) {
    return {};
  }
  return result;
}

[[nodiscard]] load_result reject(std::wstring diagnostic, unsigned line = 0) {
  load_result result;
  result.diagnostic = std::move(diagnostic);
  result.line = line;
  return result;
}

[[nodiscard]] bool parse_unsigned(std::string_view text, unsigned long& result) noexcept {
  if (text.empty() || text.size() > 10) {
    return false;
  }
  result = 0;
  for (const auto value : text) {
    if (value < '0' || value > '9') {
      return false;
    }
    result = result * 10 + static_cast<unsigned long>(value - '0');
  }
  return true;
}

[[nodiscard]] bool parse_bool(std::string_view text, bool& result) {
  const auto value = lower(text);
  if (value == "true" || value == "yes" || value == "on" || value == "1") {
    result = true;
    return true;
  }
  if (value == "false" || value == "no" || value == "off" || value == "0") {
    result = false;
    return true;
  }
  return false;
}

[[nodiscard]] std::filesystem::path resolve_path(
    std::wstring value, const std::filesystem::path& package_directory) {
  std::filesystem::path path(std::move(value));
  return (path.is_absolute() ? path : package_directory / path).lexically_normal();
}

}  // namespace

load_result parse_settings(
    std::string_view text, const std::filesystem::path& package_directory) {
  if (text.starts_with(utf8_bom)) {
    text.remove_prefix(utf8_bom.size());
  }
  if (package_directory.empty() || !package_directory.is_absolute()) {
    return reject(L"the package directory is not absolute");
  }

  settings value;
  std::string section;
  bool saw_ncm_path{};
  bool saw_unm_path{};
  bool saw_http_port{};
  bool saw_https_port{};
  bool saw_sources{};
  bool saw_timeout{};
  bool saw_write_log{};

  const auto duplicate = [](bool& seen) {
    const auto result = seen;
    seen = true;
    return result;
  };
  unsigned line_number{};
  std::size_t offset{};
  while (offset <= text.size()) {
    const auto end = text.find('\n', offset);
    const auto raw = text.substr(
        offset, end == std::string_view::npos ? text.size() - offset : end - offset);
    offset = end == std::string_view::npos ? text.size() + 1 : end + 1;
    ++line_number;
    const auto line = trim(raw);
    if (line.empty() || line.front() == '#' || line.front() == ';') {
      continue;
    }
    if (line.front() == '[') {
      if (line.size() < 3 || line.back() != ']') {
        return reject(L"a section header is malformed", line_number);
      }
      section = lower(trim(line.substr(1, line.size() - 2)));
      if (section != "ncm" && section != "unm" && section != "launcher") {
        return reject(L"the section is not supported", line_number);
      }
      continue;
    }
    if (section.empty()) {
      return reject(L"a key appears before any section", line_number);
    }
    const auto separator = line.find('=');
    if (separator == std::string_view::npos) {
      return reject(L"a line is not a 'key = value' pair", line_number);
    }
    const auto key = lower(trim(line.substr(0, separator)));
    const auto raw_value = trim(line.substr(separator + 1));
    const auto bad_value = [&] {
      return reject(L"the value of '" + widen(key) + L"' is invalid", line_number);
    };
    const auto repeated_key = [&] {
      return reject(L"'" + widen(key) + L"' appears more than once", line_number);
    };

    if (section == "ncm" && key == "path") {
      if (duplicate(saw_ncm_path)) return repeated_key();
      const auto path = widen(raw_value);
      if (path.empty()) return bad_value();
      value.ncm_executable = resolve_path(path, package_directory);
    } else if (section == "unm" && key == "path") {
      if (duplicate(saw_unm_path)) return repeated_key();
      const auto path = widen(raw_value);
      if (path.empty()) return bad_value();
      value.unm_executable = resolve_path(path, package_directory);
    } else if (section == "unm" && (key == "http_port" || key == "https_port")) {
      auto& seen = key == "http_port" ? saw_http_port : saw_https_port;
      if (duplicate(seen)) return repeated_key();
      unsigned long parsed{};
      if (!parse_unsigned(raw_value, parsed) || parsed == 0 || parsed > 65535) {
        return bad_value();
      }
      (key == "http_port" ? value.http_port : value.https_port) =
          static_cast<std::uint16_t>(parsed);
    } else if (section == "unm" && key == "sources") {
      if (duplicate(saw_sources)) return repeated_key();
      std::size_t position{};
      while (position < raw_value.size()) {
        const auto first = raw_value.find_first_not_of(" \t", position);
        if (first == std::string_view::npos) break;
        const auto last = raw_value.find_first_of(" \t", first);
        const auto source = raw_value.substr(
            first, last == std::string_view::npos ? raw_value.size() - first : last - first);
        if (!std::all_of(source.begin(), source.end(), [](unsigned char character) {
              return (character >= 'a' && character <= 'z') ||
                  (character >= '0' && character <= '9') || character == '_' || character == '-';
            })) {
          return bad_value();
        }
        value.sources.push_back(widen(source));
        position = last == std::string_view::npos ? raw_value.size() : last;
      }
    } else if (section == "unm" && key == "readiness_timeout_ms") {
      if (duplicate(saw_timeout)) return repeated_key();
      unsigned long parsed{};
      if (!parse_unsigned(raw_value, parsed) || parsed < 100 || parsed > 60000) {
        return bad_value();
      }
      value.readiness_timeout = std::chrono::milliseconds(parsed);
    } else if (section == "launcher" && key == "write_log") {
      if (duplicate(saw_write_log)) return repeated_key();
      if (!parse_bool(raw_value, value.write_log)) return bad_value();
    } else {
      return reject(L"the key is not supported in this section", line_number);
    }
  }

  if (!saw_ncm_path || !saw_unm_path) {
    return reject(L"both [ncm] path and [unm] path are required");
  }
  if (value.http_port == value.https_port) {
    return reject(L"HTTP and HTTPS ports must differ");
  }
  load_result result;
  result.status = load_status::loaded;
  result.value = std::move(value);
  return result;
}

load_result load_settings(const std::filesystem::path& package_directory) {
  const auto path = package_directory / settings_file_name;
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return reject(L"ncm-unblock.ini is missing or unreadable");
  }
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  if (stream.bad()) {
    return reject(L"ncm-unblock.ini could not be read");
  }
  return parse_settings(buffer.str(), package_directory);
}

std::filesystem::path package_directory() noexcept {
  std::wstring buffer(32768, L'\0');
  const auto length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return {};
  }
  try {
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
  } catch (...) {
    return {};
  }
}

}  // namespace ncm::config
