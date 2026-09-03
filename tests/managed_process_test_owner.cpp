#include "ncm/launcher/managed_process.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <string>

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 3) return 125;
  const std::filesystem::path child(arguments[1]);
  const std::filesystem::path marker(arguments[2]);
  try {
    auto managed = ncm::launcher::managed_process::start(
        {child, child.parent_path(), {L"--spawn-child"}});
    std::ofstream stream(marker, std::ios::binary | std::ios::trunc);
    stream << managed.process_id();
    stream.close();
    if (!stream) return 125;
    Sleep(30000);
    return 124;
  } catch (...) {
    return 125;
  }
}
