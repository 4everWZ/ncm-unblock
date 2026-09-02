#include "ncm/cef_probe/api_revision.hpp"

#include "ncm/cef/abi_1916.hpp"

#include <Windows.h>

#include <exception>
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

// `cef_version_info` entry meanings, in the order the CEF C API defines them.
[[nodiscard]] const char* version_entry_label(int entry) {
  switch (entry) {
    case 0: return "cef-version-major";
    case 1: return "cef-revision";
    case 2: return "chrome-version-major";
    case 3: return "chrome-version-minor";
    case 4: return "chrome-version-build";
    case 5: return "chrome-version-patch";
    default: return "unknown";
  }
}

void print_optional(const char* label, const std::optional<std::string>& value) {
  std::cout << label << ": " << (value.has_value() ? *value : std::string("unavailable")) << '\n';
}

void print_usage() {
  std::cerr << "usage: ncm_cef_probe <libcef-path>\n";
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 2) {
    print_usage();
    return 2;
  }

  try {
    const std::filesystem::path module_path(arguments[1]);
    const auto revision = ncm::cef_probe::read_api_revision(module_path);

    std::cout << "module: " << utf8(revision.module_path.wstring()) << '\n';
    std::cout << "file-version: "
              << (revision.file_version.empty() ? "unavailable" : revision.file_version) << '\n';
    std::cout << "argument-cleanup: " << ncm::cef_probe::describe(revision.cleanup)
              << " (stack-delta " << revision.argument_stack_delta << ")\n";

    print_optional("api-hash-platform", revision.platform_hash);
    print_optional("api-hash-universal", revision.universal_hash);
    print_optional("api-hash-commit", revision.commit_hash);

    // Whether the project's pinned struct layouts describe this module. A
    // mismatch is not a warning: nothing derived from those layouts may run.
    const bool pinned = ncm::cef::matches_pinned_api(
        revision.platform_hash.value_or(std::string{}),
        revision.universal_hash.value_or(std::string{}));
    std::cout << "pinned-api-match: " << (pinned ? "yes" : "NO") << '\n';

    std::cout << "build-revision: ";
    if (revision.build_revision.has_value()) {
      std::cout << *revision.build_revision << '\n';
    } else {
      std::cout << "unavailable\n";
    }

    std::cout << "version-info:\n";
    for (const auto& field : revision.version_fields) {
      std::cout << "  " << field.entry << ' ' << version_entry_label(field.entry) << '='
                << field.value << '\n';
    }

    unsigned absent = 0;
    std::cout << "entry-points:\n";
    for (const auto& entry : revision.entry_points) {
      std::cout << "  " << (entry.present ? "present" : "ABSENT ") << ' ' << entry.name << '\n';
      if (!entry.present) {
        ++absent;
      }
    }
    std::cout << "entry-points-absent: " << absent << '\n';
    if (!pinned) {
      return 4;
    }
    return absent == 0 ? 0 : 3;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
