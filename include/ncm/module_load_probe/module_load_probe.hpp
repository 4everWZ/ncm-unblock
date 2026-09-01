#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace ncm::module_load_probe {

struct probe_options {
  std::filesystem::path target_executable;
  std::wstring module_basename;
  std::filesystem::path expected_module_path;
  std::chrono::milliseconds timeout{};
  std::size_t maximum_events{512};
  bool require_target_signature{true};
  bool require_expected_signature{true};
};

struct probe_result {
  std::filesystem::path loaded_module_path;
  std::size_t event_count{};
};

// Launches the target under the Windows debug API and always terminates its
// private job before returning or throwing once process creation succeeds.
[[nodiscard]] probe_result run(const probe_options& options);

}  // namespace ncm::module_load_probe
