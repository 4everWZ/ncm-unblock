#include "ncm/runtime_probe/pe_image.hpp"
#include "ncm/runtime_probe/process_snapshot.hpp"

#include <Windows.h>

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] std::string utf8(std::wstring_view value) {
  if (value.empty()) {
    return {};
  }
  const auto required = WideCharToMultiByte(
      CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
      nullptr, 0, nullptr, nullptr);
  if (required <= 0) {
    throw std::runtime_error("unable to encode path as UTF-8");
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
          result.data(), required, nullptr, nullptr) != required) {
    throw std::runtime_error("unable to encode path as UTF-8");
  }
  return result;
}

void print_usage() {
  std::cerr << "usage: ncm_runtime_probe <pe-path> [--process-name <name>] [--no-processes]\n";
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count < 2) {
    print_usage();
    return 2;
  }

  try {
    const std::filesystem::path image_path(arguments[1]);
    std::wstring process_name = image_path.filename().wstring();
    bool inspect_processes = true;

    for (int index = 2; index < argument_count; ++index) {
      const std::wstring_view argument(arguments[index]);
      if (argument == L"--no-processes") {
        inspect_processes = false;
      } else if (argument == L"--process-name" && index + 1 < argument_count) {
        process_name = arguments[++index];
      } else {
        print_usage();
        return 2;
      }
    }

    const auto image = ncm::runtime_probe::inspect_pe_image(image_path);
    std::cout << "image: " << utf8(image_path.wstring()) << '\n';
    std::cout << "machine: " << ncm::runtime_probe::machine_name(image.machine)
              << " (0x" << std::hex << std::setw(4) << std::setfill('0') << image.machine
              << std::dec << ")\n";
    std::cout << "format: " << (image.pe32_plus ? "PE32+" : "PE32") << '\n';
    std::cout << "file-version: " << (image.file_version.empty() ? "unavailable" : image.file_version) << '\n';
    std::cout << "authenticode: " << (image.signature.valid ? "valid" : "not-valid")
              << " (0x" << std::hex << static_cast<unsigned long>(image.signature.status)
              << std::dec << ")\n";

    std::cout << "imports:\n";
    for (const auto& import : image.imports) {
      std::cout << "  " << import << '\n';
    }
    std::cout << "delay-imports:\n";
    for (const auto& import : image.delay_imports) {
      std::cout << "  " << import << '\n';
    }

    if (inspect_processes) {
      const auto processes = ncm::runtime_probe::find_processes(process_name);
      std::cout << "processes: " << processes.size() << '\n';
      for (const auto& process : processes) {
        std::cout << "  pid=" << process.process_id
                  << " parent=" << process.parent_process_id
                  << " ipv4-tcp=";
        if (process.ipv4_tcp_connections.has_value()) {
          std::cout << *process.ipv4_tcp_connections;
        } else {
          std::cout << "unavailable";
        }
        std::cout << " image=" << utf8(process.image_path.wstring()) << '\n';
        std::cout << "    network-modules"
                  << (process.network_modules_complete ? "" : "(partial/unavailable)") << ':';
        for (const auto& module : process.network_modules) {
          std::cout << ' ' << utf8(module);
        }
        std::cout << '\n';
      }
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
