#pragma once

#include "ncm/launcher/managed_process.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ncm::launcher {

struct unm_sidecar_options {
  std::filesystem::path executable;
  std::filesystem::path working_directory;
  std::vector<std::wstring> arguments;
  std::optional<std::uint16_t> fixed_http_port;
  std::optional<std::uint16_t> fixed_https_port;
  std::size_t maximum_automatic_attempts{3};
  std::chrono::milliseconds readiness_timeout{std::chrono::seconds(10)};
};

class unm_sidecar {
 public:
  unm_sidecar() noexcept = default;

  unm_sidecar(const unm_sidecar&) = delete;
  unm_sidecar& operator=(const unm_sidecar&) = delete;
  unm_sidecar(unm_sidecar&&) noexcept = default;
  unm_sidecar& operator=(unm_sidecar&&) noexcept = default;

  [[nodiscard]] static unm_sidecar launch(const unm_sidecar_options& options);

  [[nodiscard]] std::uint16_t http_port() const noexcept;
  [[nodiscard]] std::uint16_t https_port() const noexcept;
  [[nodiscard]] const managed_process& process() const noexcept;
  [[nodiscard]] managed_process& process() noexcept;

 private:
  unm_sidecar(
      managed_process process, std::uint16_t http_port,
      std::uint16_t https_port) noexcept;

  managed_process process_;
  std::uint16_t http_port_{};
  std::uint16_t https_port_{};
};

}  // namespace ncm::launcher
