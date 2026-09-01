#include "ncm/module_load_probe/module_load_probe.hpp"

#include <Windows.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
    throw std::runtime_error("unable to encode output as UTF-8");
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  if (WideCharToMultiByte(
          CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
          result.data(), required, nullptr, nullptr) != required) {
    throw std::runtime_error("unable to encode output as UTF-8");
  }
  return result;
}

[[nodiscard]] std::chrono::milliseconds parse_timeout(std::wstring_view value) {
  if (value.empty()) {
    throw std::invalid_argument("timeout must be an integer from 1 through 60000 milliseconds");
  }
  std::string narrow;
  narrow.reserve(value.size());
  for (const auto character : value) {
    if (character < L'0' || character > L'9') {
      throw std::invalid_argument("timeout must be an integer from 1 through 60000 milliseconds");
    }
    narrow.push_back(static_cast<char>(character));
  }
  std::uint32_t parsed{};
  const auto [end, status] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
  if (status != std::errc{} || end != narrow.data() + narrow.size() || parsed == 0 || parsed > 60000) {
    throw std::invalid_argument("timeout must be an integer from 1 through 60000 milliseconds");
  }
  return std::chrono::milliseconds(parsed);
}

void print_usage() {
  std::cerr << "usage: ncm_module_load_probe <absolute-target-exe> <module-basename> "
               "<absolute-expected-module-path> <timeout-ms>\n";
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 5) {
    print_usage();
    return 2;
  }

  try {
    ncm::module_load_probe::probe_options options;
    options.target_executable = arguments[1];
    options.module_basename = arguments[2];
    options.expected_module_path = arguments[3];
    options.timeout = parse_timeout(arguments[4]);
    options.require_target_signature = true;
    options.require_expected_signature = true;

    const auto result = ncm::module_load_probe::run(options);
    std::cout << "loaded-module: " << utf8(result.loaded_module_path.wstring()) << '\n';
    std::cout << "debug-events: " << result.event_count << '\n';
    return 0;
  } catch (const std::invalid_argument& error) {
    std::cerr << "input error: " << error.what() << '\n';
    print_usage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "probe failed: " << error.what() << '\n';
    return 1;
  }
}
