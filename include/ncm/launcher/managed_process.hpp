#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ncm::launcher {

struct process_spec {
  std::filesystem::path executable;
  std::filesystem::path working_directory;
  std::vector<std::wstring> arguments;
  std::optional<std::filesystem::path> output_file;
  // When non-empty, merge these variables over the current process environment
  // and pass a private Unicode block to CreateProcessW. Empty keeps inherit.
  std::vector<std::pair<std::wstring, std::wstring>> environment;
  bool no_window{};
};

class managed_process {
 public:
  managed_process() noexcept = default;
  ~managed_process();

  managed_process(const managed_process&) = delete;
  managed_process& operator=(const managed_process&) = delete;
  managed_process(managed_process&& other) noexcept;
  managed_process& operator=(managed_process&& other) noexcept;

  [[nodiscard]] static managed_process prepare(const process_spec& spec);
  [[nodiscard]] static managed_process start(const process_spec& spec);

  void resume();
  [[nodiscard]] bool suspended() const noexcept;
  [[nodiscard]] std::uint32_t process_id() const noexcept;
  [[nodiscard]] bool root_running() const;
  [[nodiscard]] std::optional<std::uint32_t> wait_for_root(
      std::chrono::milliseconds timeout) const;
  [[nodiscard]] bool contains_process(std::uint32_t process_id) const;
  [[nodiscard]] bool wait_for_tree(std::chrono::milliseconds timeout) const;
  [[nodiscard]] bool terminate_and_wait_tree(
      std::uint32_t exit_code, std::chrono::milliseconds timeout) const;

 private:
  managed_process(
      void* job, void* process, void* thread, void* completion_port,
      std::uint32_t process_id) noexcept;
  void reset() noexcept;

  void* job_{};
  void* process_{};
  void* thread_{};
  void* completion_port_{};
  std::uint32_t process_id_{};
};

}  // namespace ncm::launcher
