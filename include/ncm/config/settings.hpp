#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ncm::config {

// Name of the human-readable configuration file. It lives beside the bootstrap
// DLL, which is also the portable data directory for the first deliverable.
inline constexpr wchar_t settings_file_name[] = L"ncm_unblock.ini";

// Values the bootstrap needs before it can start and own a sidecar. Ports are
// empty when the file asks for automatic selection, which is the default.
struct settings {
  bool enabled{true};
  std::filesystem::path sidecar_executable;
  std::optional<std::uint16_t> http_port;
  std::optional<std::uint16_t> https_port;
  std::size_t automatic_attempts{3};
  std::chrono::milliseconds readiness_timeout{std::chrono::seconds(10)};
};

// Default sidecar executable name, resolved against the package directory when
// the file does not name one.
inline constexpr wchar_t default_sidecar_file_name[] = L"unm.exe";

enum class load_status {
  // No file was present. Defaults apply and the bootstrap continues.
  defaults_used,
  // A well-formed file was applied.
  loaded,
  // A file exists but does not state a usable intent, or could not be read.
  // The bootstrap declines the feature rather than acting on a guess.
  invalid,
};

struct load_result {
  load_status status{load_status::defaults_used};
  settings value{};
  // Empty unless `status` is `invalid`. Actionable, and free of file contents
  // beyond the offending key, so a diagnostic never leaks a user's paths.
  std::wstring diagnostic;
  // 1-based line the diagnostic refers to, or 0 when it is not line-specific.
  unsigned line{};
};

// Parses configuration text. `package_directory` resolves relative paths.
//
// Grammar: one `key = value` per line, `#` or `;` comments, blank lines
// ignored, keys case-insensitive, no sections. A duplicate key, an unknown key,
// or an out-of-range value is invalid: a partially understood file states an
// intent this build cannot honor.
[[nodiscard]] load_result parse_settings(
    std::string_view text, const std::filesystem::path& package_directory);

// Loads `settings_file_name` from `package_directory`. An absent file yields
// defaults; an unreadable one is invalid.
[[nodiscard]] load_result load_settings(
    const std::filesystem::path& package_directory);

// Directory of the module this function is linked into. Empty when it cannot be
// determined.
[[nodiscard]] std::filesystem::path package_directory() noexcept;

}  // namespace ncm::config
