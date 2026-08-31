#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ncm::runtime_probe {

struct signature_info {
  bool valid{};
  long status{};
};

struct pe_image_info {
  std::uint16_t machine{};
  bool pe32_plus{};
  std::string file_version;
  signature_info signature;
  std::vector<std::string> imports;
  std::vector<std::string> delay_imports;
};

[[nodiscard]] pe_image_info inspect_pe_image(const std::filesystem::path& path);
[[nodiscard]] std::string machine_name(std::uint16_t machine);

}  // namespace ncm::runtime_probe
