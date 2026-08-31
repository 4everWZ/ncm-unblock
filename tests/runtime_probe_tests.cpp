#include "ncm/runtime_probe/pe_image.hpp"
#include "ncm/runtime_probe/process_snapshot.hpp"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path current_executable() {
  std::wstring buffer(32768, L'\0');
  const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size == 0 || size >= buffer.size()) {
    throw std::runtime_error("GetModuleFileNameW failed");
  }
  buffer.resize(size);
  return buffer;
}

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_current_image() {
  const auto path = current_executable();
  const auto info = ncm::runtime_probe::inspect_pe_image(path);
  require(info.machine == IMAGE_FILE_MACHINE_I386, "test executable is not Win32");
  require(!info.pe32_plus, "Win32 test executable reported PE32+");
  require(std::ranges::find(info.imports, "kernel32.dll") != info.imports.end(),
          "kernel32.dll import was not parsed");
}

void test_invalid_image() {
  const auto path = std::filesystem::temp_directory_path() /
      (L"ncm-runtime-probe-invalid-" + std::to_wstring(GetCurrentProcessId()) + L".bin");
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "not a PE image";
  }

  bool rejected = false;
  try {
    static_cast<void>(ncm::runtime_probe::inspect_pe_image(path));
  } catch (const std::exception&) {
    rejected = true;
  }
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(rejected, "invalid image was accepted");
}

void test_process_snapshot() {
  const auto path = current_executable();
  const auto processes = ncm::runtime_probe::find_processes(path.filename().wstring());
  const auto current_id = GetCurrentProcessId();
  const auto current = std::ranges::find_if(processes, [current_id](const auto& process) {
    return process.process_id == current_id;
  });
  require(current != processes.end(), "current process was not found in the snapshot");
  require(current->ipv4_tcp_connections.has_value(), "TCP ownership collection was unavailable");
  require(current->network_modules_complete, "module collection was incomplete");
}

}  // namespace

int main() {
  try {
    test_current_image();
    test_invalid_image();
    test_process_snapshot();
    std::cout << "runtime probe tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
