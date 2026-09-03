#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace ncm::launcher {

class ncm_session {
 public:
  ncm_session() noexcept = default;
  ~ncm_session();
  ncm_session(const ncm_session&) = delete;
  ncm_session& operator=(const ncm_session&) = delete;
  ncm_session(ncm_session&& other) noexcept;
  ncm_session& operator=(ncm_session&& other) noexcept;

  [[nodiscard]] static bool target_running(
      const std::filesystem::path& executable);
  [[nodiscard]] static ncm_session launch(
      const std::filesystem::path& executable);

  [[nodiscard]] bool active();
  [[nodiscard]] std::uint32_t root_process_id() const noexcept;

 private:
  struct identity {
    std::uint32_t process_id{};
    std::uint64_t creation_time{};
  };

  ncm_session(
      void* root_process, std::uint32_t root_process_id,
      std::uint64_t root_creation_time, std::filesystem::path executable);
  void reset() noexcept;

  void* root_process_{};
  std::uint32_t root_process_id_{};
  std::filesystem::path executable_;
  std::filesystem::path reporter_;
  std::vector<identity> tracked_;
};

}  // namespace ncm::launcher
