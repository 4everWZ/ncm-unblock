#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace ncm::host {

class ncm_watch {
 public:
  ncm_watch() noexcept = default;
  ~ncm_watch();

  ncm_watch(const ncm_watch&) = delete;
  ncm_watch& operator=(const ncm_watch&) = delete;
  ncm_watch(ncm_watch&& other) noexcept;
  ncm_watch& operator=(ncm_watch&& other) noexcept;

  [[nodiscard]] static std::optional<ncm_watch> attach(
      const std::filesystem::path& executable);

  [[nodiscard]] bool alive() const;
  [[nodiscard]] std::uint32_t process_id() const noexcept;
  [[nodiscard]] void* wait_handle() const noexcept;

 private:
  ncm_watch(
      void* process, std::uint32_t process_id, std::uint64_t creation_time,
      std::filesystem::path executable) noexcept;
  void reset() noexcept;

  void* process_{};
  std::uint32_t process_id_{};
  std::uint64_t creation_time_{};
  std::filesystem::path executable_;
};

}  // namespace ncm::host
