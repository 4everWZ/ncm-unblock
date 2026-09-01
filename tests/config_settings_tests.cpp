#include "ncm/config/settings.hpp"

#include <Windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

const std::filesystem::path package{L"C:\\package"};

ncm::config::load_result parse(std::string_view text) {
  return ncm::config::parse_settings(text, package);
}

void test_empty_and_comment_only_files_are_defaults() {
  for (const std::string_view text :
       {std::string_view{""}, std::string_view{"\n"},
        std::string_view{"# a comment\r\n; another\r\n   \r\n"}}) {
    const auto result = parse(text);
    require(result.status == ncm::config::load_status::loaded,
            "a comment-only file was not accepted");
    require(result.value.enabled, "the default is not enabled");
    require(!result.value.http_port.has_value() && !result.value.https_port.has_value(),
            "the default port selection is not automatic");
    require(result.value.automatic_attempts == 3, "the default attempt budget changed");
    require(result.value.readiness_timeout == std::chrono::seconds(10),
            "the default readiness timeout changed");
    require(result.value.sidecar_executable ==
                package / ncm::config::default_sidecar_file_name,
            "the default sidecar path is not beside the package");
  }
}

void test_a_utf8_bom_is_accepted() {
  const auto result = parse("\xEF\xBB\xBF" "enabled = false\n");
  require(result.status == ncm::config::load_status::loaded,
          "a file with a UTF-8 byte-order mark was rejected");
  require(!result.value.enabled, "the value after the byte-order mark was not read");
}

void test_every_key_is_applied() {
  const auto result = parse(
      "Enabled = YES\r\n"
      "sidecar_executable = bin/unm-custom.exe\r\n"
      "http_port = 18080\r\n"
      "https_port=18081\r\n"
      "automatic_attempts = 5\r\n"
      "readiness_timeout_ms = 2500\r\n");
  require(result.status == ncm::config::load_status::loaded, "a valid file was rejected");
  require(result.value.enabled, "'enabled' was not applied");
  require(result.value.sidecar_executable == package / L"bin/unm-custom.exe",
          "a relative sidecar path was not resolved against the package");
  require(result.value.http_port == 18080 && result.value.https_port == 18081,
          "the fixed ports were not applied");
  require(result.value.automatic_attempts == 5, "'automatic_attempts' was not applied");
  require(result.value.readiness_timeout == std::chrono::milliseconds(2500),
          "'readiness_timeout_ms' was not applied");
}

void test_an_absolute_sidecar_path_is_taken_as_written() {
  const auto result = parse("sidecar_executable = D:\\shared\\unm.exe\n");
  require(result.status == ncm::config::load_status::loaded,
          "an absolute sidecar path was rejected");
  require(result.value.sidecar_executable == std::filesystem::path{L"D:\\shared\\unm.exe"},
          "an absolute sidecar path was rewritten");
}

void test_unusable_files_are_rejected() {
  const std::string_view rejected[]{
      "enabled\n",                                   // not a pair
      " = true\n",                                   // empty key
      "enabled = maybe\n",                           // unusable value
      "source_order = kuwo\n",                       // unknown key
      "enabled = true\nenabled = false\n",           // repeated key
      "http_port = 0\nhttps_port = 1\n",             // out of range
      "http_port = 65536\nhttps_port = 1\n",         // out of range
      "http_port = 18080\n",                         // one fixed, one automatic
      "https_port = 18080\n",                        // one fixed, one automatic
      "http_port = 18080\nhttps_port = 18080\n",     // colliding pair
      "automatic_attempts = 0\n",                    // out of range
      "automatic_attempts = 11\n",                   // out of range
      "readiness_timeout_ms = 0\n",                  // out of range
      "readiness_timeout_ms = 600001\n",             // out of range
  };
  for (const std::string_view text : rejected) {
    const auto result = parse(text);
    require(result.status == ncm::config::load_status::invalid,
            "an unusable configuration was accepted: " + std::string(text));
    require(!result.diagnostic.empty(),
            "a rejection carried no diagnostic: " + std::string(text));
  }
}

void test_a_rejection_names_the_offending_line() {
  const auto result = parse("# comment\n\nenabled = true\nsource_order = kuwo\n");
  require(result.status == ncm::config::load_status::invalid,
          "an unknown key was accepted");
  require(result.line == 4, "the rejection did not name the offending line");
}

std::filesystem::path make_temporary_directory() {
  wchar_t base[MAX_PATH]{};
  require(GetTempPathW(MAX_PATH, base) != 0, "unable to read the temporary path");
  GUID id{};
  require(SUCCEEDED(CoCreateGuid(&id)), "unable to name a temporary directory");
  wchar_t name[64]{};
  swprintf_s(name, L"ncm-config-%08lX%04hX", id.Data1, id.Data2);
  const std::filesystem::path directory = std::filesystem::path(base) / name;
  std::filesystem::create_directories(directory);
  return directory;
}

void test_an_absent_file_uses_defaults_and_a_present_one_is_read() {
  const std::filesystem::path directory = make_temporary_directory();

  const auto absent = ncm::config::load_settings(directory);
  require(absent.status == ncm::config::load_status::defaults_used,
          "an absent configuration file was not treated as defaults");
  require(absent.value.enabled, "an absent file did not leave the feature enabled");
  require(absent.value.sidecar_executable ==
              directory / ncm::config::default_sidecar_file_name,
          "an absent file did not resolve the default sidecar beside the package");

  const std::filesystem::path file = directory / ncm::config::settings_file_name;
  {
    std::ofstream stream(file, std::ios::binary);
    stream << "enabled = false\n";
  }
  const auto present = ncm::config::load_settings(directory);
  require(present.status == ncm::config::load_status::loaded,
          "a present configuration file was not loaded");
  require(!present.value.enabled, "the present file's value was not applied");

  {
    std::ofstream stream(file, std::ios::binary);
    stream << "enabled = perhaps\n";
  }
  const auto invalid = ncm::config::load_settings(directory);
  require(invalid.status == ncm::config::load_status::invalid,
          "an unusable present file was not rejected");

  std::filesystem::remove_all(directory);
}

void test_the_package_directory_is_this_module_s_directory() {
  const std::filesystem::path directory = ncm::config::package_directory();
  require(!directory.empty(), "the package directory could not be determined");

  wchar_t own[MAX_PATH]{};
  require(GetModuleFileNameW(nullptr, own, MAX_PATH) != 0,
          "unable to read this executable's path");
  require(directory == std::filesystem::path(own).parent_path(),
          "the package directory is not the directory of the linked-in module");
}

}  // namespace

int wmain() {
  try {
    test_empty_and_comment_only_files_are_defaults();
    test_a_utf8_bom_is_accepted();
    test_every_key_is_applied();
    test_an_absolute_sidecar_path_is_taken_as_written();
    test_unusable_files_are_rejected();
    test_a_rejection_names_the_offending_line();
    test_an_absent_file_uses_defaults_and_a_present_one_is_read();
    test_the_package_directory_is_this_module_s_directory();
    std::cout << "configuration tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
