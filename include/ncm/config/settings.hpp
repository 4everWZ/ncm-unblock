#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ncm::config {

inline constexpr wchar_t settings_file_name[] = L"ncm-unblock.ini";

struct settings {
  std::filesystem::path ncm_executable;
  std::filesystem::path unm_executable;
  std::uint16_t http_port{3412};
  std::uint16_t https_port{3413};
  std::vector<std::wstring> sources;
  std::chrono::milliseconds readiness_timeout{std::chrono::seconds(15)};
  bool write_log{true};
};

enum class load_status { loaded, invalid };

struct load_result {
  load_status status{load_status::invalid};
  settings value{};
  std::wstring diagnostic;
  unsigned line{};
};

[[nodiscard]] load_result parse_settings(
    std::string_view text, const std::filesystem::path& package_directory);
[[nodiscard]] load_result load_settings(
    const std::filesystem::path& package_directory);
[[nodiscard]] std::filesystem::path package_directory() noexcept;

}  // namespace ncm::config
