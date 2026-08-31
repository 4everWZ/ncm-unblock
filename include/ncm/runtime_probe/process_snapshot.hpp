#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ncm::runtime_probe {

struct process_info {
  std::uint32_t process_id{};
  std::uint32_t parent_process_id{};
  std::optional<std::uint32_t> ipv4_tcp_connections;
  std::filesystem::path image_path;
  std::vector<std::wstring> network_modules;
  bool network_modules_complete{};
};

[[nodiscard]] std::vector<process_info> find_processes(std::wstring_view executable_name);

}  // namespace ncm::runtime_probe
