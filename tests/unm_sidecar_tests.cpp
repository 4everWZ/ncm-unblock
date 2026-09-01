#include "ncm/launcher/loopback_port_pair.hpp"
#include "ncm/launcher/unm_sidecar.hpp"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] ncm::launcher::unm_sidecar_options options_for(
    const std::filesystem::path& child, std::vector<std::wstring> arguments) {
  ncm::launcher::unm_sidecar_options options;
  options.executable = child;
  options.working_directory = child.parent_path();
  options.arguments = std::move(arguments);
  options.maximum_automatic_attempts = 2;
  options.readiness_timeout = std::chrono::seconds(5);
  return options;
}

void require_clean_exit(ncm::launcher::unm_sidecar& sidecar) {
  const auto exit_code = sidecar.process().wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 0,
          "ready sidecar fixture did not exit cleanly");
  require(sidecar.process().wait_for_tree(std::chrono::seconds(5)),
          "ready sidecar fixture tree did not become empty");
}

void test_automatic_launch(const std::filesystem::path& child) {
  auto sidecar = ncm::launcher::unm_sidecar::launch(
      options_for(child, {L"--unm-sidecar"}));
  require(sidecar.http_port() != 0 && sidecar.https_port() != 0 &&
              sidecar.http_port() != sidecar.https_port(),
          "automatic sidecar launch returned invalid ports");
  require(sidecar.process().contains_process(sidecar.process().process_id()),
          "ready sidecar root is outside its private job");
  require_clean_exit(sidecar);
}

void test_automatic_retry(const std::filesystem::path& child) {
  wchar_t temp_path[MAX_PATH + 1]{};
  require(GetTempPathW(MAX_PATH, temp_path) != 0, "unable to resolve temp path");
  const auto marker = std::filesystem::path(temp_path) /
      (L"ncm-unm-retry-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()) + L".tmp");
  DeleteFileW(marker.c_str());
  try {
    auto sidecar = ncm::launcher::unm_sidecar::launch(
        options_for(child, {L"--unm-fail-once", marker.wstring()}));
    require(std::filesystem::is_regular_file(marker),
            "first automatic attempt did not create its failure marker");
    require_clean_exit(sidecar);
  } catch (...) {
    DeleteFileW(marker.c_str());
    throw;
  }
  DeleteFileW(marker.c_str());
}

void test_fixed_ports_and_collision(const std::filesystem::path& child) {
  auto selection = ncm::launcher::loopback_port_pair::acquire_automatic();
  const auto http_port = selection.http_port();
  const auto https_port = selection.https_port();
  selection.release();

  auto fixed = options_for(child, {L"--unm-sidecar"});
  fixed.fixed_http_port = http_port;
  fixed.fixed_https_port = https_port;
  fixed.maximum_automatic_attempts = 0;
  auto sidecar = ncm::launcher::unm_sidecar::launch(fixed);
  require(sidecar.http_port() == http_port && sidecar.https_port() == https_port,
          "fixed sidecar launch silently changed ports");
  require_clean_exit(sidecar);

  auto collision = ncm::launcher::loopback_port_pair::acquire_fixed(http_port, https_port);
  bool rejected{};
  try {
    (void)ncm::launcher::unm_sidecar::launch(fixed);
  } catch (const std::system_error&) {
    rejected = true;
  }
  require(rejected, "fixed sidecar collision was not rejected");
}

void test_reserved_arguments_rejected(const std::filesystem::path& child) {
  for (const auto* reserved : {L"--address", L"--port=1234:1235", L"--strict=false"}) {
    bool rejected{};
    try {
      (void)ncm::launcher::unm_sidecar::launch(
          options_for(child, {L"--unm-sidecar", reserved}));
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected, "sidecar routing/safety override was accepted");
  }
}

void test_empty_pac_rejected(const std::filesystem::path& child) {
  for (const auto* mode : {L"--unm-empty-pac", L"--unm-short-pac"}) {
    auto options = options_for(child, {mode});
    options.maximum_automatic_attempts = 1;
    bool rejected{};
    try {
      (void)ncm::launcher::unm_sidecar::launch(options);
    } catch (const std::runtime_error&) {
      rejected = true;
    }
    require(rejected, "empty or truncated HTTP 200 response was accepted as PAC readiness");
  }
}

void test_pac_deadline_is_bounded(const std::filesystem::path& child) {
  auto options = options_for(child, {L"--unm-slow-pac"});
  options.maximum_automatic_attempts = 1;
  options.readiness_timeout = std::chrono::milliseconds(300);
  const auto started = std::chrono::steady_clock::now();
  bool rejected{};
  try {
    (void)ncm::launcher::unm_sidecar::launch(options);
  } catch (const std::runtime_error&) {
    rejected = true;
  }
  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(rejected, "slow PAC response was accepted past the readiness deadline");
  require(elapsed < std::chrono::seconds(2),
          "slow PAC response exceeded the bounded readiness deadline by too much");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 2, "test child path argument is missing");
    const std::filesystem::path child(arguments[1]);
    test_automatic_launch(child);
    test_automatic_retry(child);
    test_fixed_ports_and_collision(child);
    test_reserved_arguments_rejected(child);
    test_empty_pac_rejected(child);
    test_pac_deadline_is_bounded(child);
    std::cout << "UNM sidecar tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
