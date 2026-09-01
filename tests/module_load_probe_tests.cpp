#include "ncm/module_load_probe/module_load_probe.hpp"

#include <Windows.h>
#include <objbase.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class environment_marker {
 public:
  environment_marker(std::wstring name, const std::filesystem::path& path)
      : name_(std::move(name)) {
    const auto required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
    if (required != 0) {
      previous_.resize(required);
      const auto written = GetEnvironmentVariableW(
          name_.c_str(), previous_.data(), static_cast<DWORD>(previous_.size()));
      if (written == 0 || written >= previous_.size()) {
        throw std::runtime_error("unable to preserve marker environment variable");
      }
      previous_.resize(written);
      had_previous_ = true;
    } else if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
      throw std::runtime_error("unable to inspect marker environment variable");
    }
    if (!SetEnvironmentVariableW(name_.c_str(), path.c_str())) {
      throw std::runtime_error("unable to set marker environment variable");
    }
  }

  ~environment_marker() {
    SetEnvironmentVariableW(
        name_.c_str(), had_previous_ ? previous_.c_str() : nullptr);
  }

  environment_marker(const environment_marker&) = delete;
  environment_marker& operator=(const environment_marker&) = delete;

 private:
  std::wstring name_;
  std::wstring previous_;
  bool had_previous_{};
};

struct fixture_paths {
  std::filesystem::path directory;
  std::filesystem::path executable_marker;
  std::filesystem::path dll_marker;
  std::filesystem::path unexpected_expected_module;
};

[[nodiscard]] fixture_paths make_fixture_paths(
    const std::filesystem::path& loaded_module) {
  wchar_t temporary[MAX_PATH]{};
  const auto length = GetTempPathW(static_cast<DWORD>(std::size(temporary)), temporary);
  if (length == 0 || length >= std::size(temporary)) {
    throw std::runtime_error("unable to resolve temporary directory");
  }
  fixture_paths paths;
  GUID identifier{};
  if (FAILED(CoCreateGuid(&identifier))) {
    throw std::runtime_error("unable to create fixture directory identity");
  }
  wchar_t identifier_text[64]{};
  if (StringFromGUID2(identifier, identifier_text, static_cast<int>(std::size(identifier_text))) == 0) {
    throw std::runtime_error("unable to format fixture directory identity");
  }
  paths.directory = std::filesystem::path(temporary) /
      (L"ncm_module_load_probe_tests_" + std::wstring(identifier_text));
  if (!std::filesystem::create_directory(paths.directory)) {
    throw std::runtime_error("unable to create unique fixture directory");
  }
  paths.executable_marker = paths.directory / L"exe.marker";
  paths.dll_marker = paths.directory / L"dll.marker";
  const auto alternate_directory = paths.directory / L"alternate";
  std::filesystem::create_directories(alternate_directory);
  paths.unexpected_expected_module = alternate_directory / loaded_module.filename();
  std::filesystem::copy_file(
      loaded_module, paths.unexpected_expected_module,
      std::filesystem::copy_options::overwrite_existing);
  return paths;
}

void cleanup_fixture_paths(const fixture_paths& paths) {
  const auto alternate = paths.unexpected_expected_module.parent_path();
  for (const auto& item : std::filesystem::directory_iterator(paths.directory)) {
    const auto name = item.path().filename();
    if (name != paths.executable_marker.filename() &&
        name != paths.dll_marker.filename() && name != alternate.filename()) {
      throw std::runtime_error("fixture directory contains an unknown item; preserving it");
    }
  }
  for (const auto& item : std::filesystem::directory_iterator(alternate)) {
    if (item.path().filename() != paths.unexpected_expected_module.filename()) {
      throw std::runtime_error("fixture alternate directory contains an unknown item; preserving it");
    }
  }

  std::error_code status;
  std::filesystem::remove(paths.executable_marker, status);
  if (status) {
    throw std::runtime_error("unable to remove executable marker");
  }
  std::filesystem::remove(paths.dll_marker, status);
  if (status) {
    throw std::runtime_error("unable to remove DLL marker");
  }
  if (!std::filesystem::remove(paths.unexpected_expected_module, status) || status) {
    throw std::runtime_error("unable to remove alternate expected module");
  }
  if (!std::filesystem::remove(alternate, status) || status) {
    throw std::runtime_error("unable to remove alternate fixture directory");
  }
  if (!std::filesystem::remove(paths.directory, status) || status) {
    throw std::runtime_error("unable to remove fixture directory");
  }
}

[[nodiscard]] ncm::module_load_probe::probe_options synthetic_options(
    const std::filesystem::path& target,
    const std::filesystem::path& expected) {
  ncm::module_load_probe::probe_options options;
  options.target_executable = target;
  options.module_basename = expected.filename().wstring();
  options.expected_module_path = expected;
  options.timeout = std::chrono::seconds(3);
  options.require_target_signature = false;
  options.require_expected_signature = false;
  return options;
}

void require_no_markers(const fixture_paths& paths, const char* context) {
  if (std::filesystem::exists(paths.dll_marker) ||
      std::filesystem::exists(paths.executable_marker)) {
    throw std::runtime_error(std::string(context) +
        ": fixture DllMain or executable entry point ran");
  }
}

void test_expected_path(
    const std::filesystem::path& target, const std::filesystem::path& module,
    const fixture_paths& paths) {
  const auto started = std::chrono::steady_clock::now();
  const auto result = ncm::module_load_probe::run(synthetic_options(target, module));
  const auto elapsed = std::chrono::steady_clock::now() - started;
  require(std::filesystem::equivalent(result.loaded_module_path, module),
          "probe did not return the expected module path");
  require(result.event_count > 1 && result.event_count < 512,
          "probe reported an invalid bounded event count");
  require(elapsed < std::chrono::seconds(5),
          "successful probe did not drain the terminated debuggee promptly");
  require_no_markers(paths, "expected-path probe");
}

void test_unexpected_path(
    const std::filesystem::path& target, const fixture_paths& paths) {
  auto options = synthetic_options(target, paths.unexpected_expected_module);
  bool rejected{};
  const auto started = std::chrono::steady_clock::now();
  try {
    (void)ncm::module_load_probe::run(options);
  } catch (const std::runtime_error& error) {
    rejected = std::string_view(error.what()).find("pinned expected file identity") !=
        std::string_view::npos;
  }
  require(rejected, "probe accepted a same-basename module from an unexpected path");
  require(std::chrono::steady_clock::now() - started < std::chrono::seconds(5),
          "unexpected-path probe did not drain the terminated debuggee promptly");
  require_no_markers(paths, "unexpected-path probe");
}

void test_signatures_required_for_cli_contract(
    const std::filesystem::path& target, const std::filesystem::path& module,
    const fixture_paths& paths) {
  auto options = synthetic_options(target, module);
  options.require_target_signature = true;
  bool target_rejected{};
  try {
    (void)ncm::module_load_probe::run(options);
  } catch (const std::runtime_error& error) {
    target_rejected = std::string_view(error.what()).find("target executable") !=
            std::string_view::npos &&
        std::string_view(error.what()).find("Authenticode") != std::string_view::npos;
  }
  require(target_rejected, "unsigned target passed the real-probe signature gate");

  options.require_target_signature = false;
  options.require_expected_signature = true;
  bool expected_rejected{};
  try {
    (void)ncm::module_load_probe::run(options);
  } catch (const std::runtime_error& error) {
    expected_rejected = std::string_view(error.what()).find("expected module") !=
            std::string_view::npos &&
        std::string_view(error.what()).find("Authenticode") != std::string_view::npos;
  }
  require(expected_rejected, "unsigned expected module passed the real-probe signature gate");
  require_no_markers(paths, "signature-gate probe");
}

void test_event_limit_fails_closed(
    const std::filesystem::path& target, const std::filesystem::path& module,
    const fixture_paths& paths) {
  auto options = synthetic_options(target, module);
  options.maximum_events = 1;
  bool rejected{};
  const auto started = std::chrono::steady_clock::now();
  try {
    (void)ncm::module_load_probe::run(options);
  } catch (const std::runtime_error& error) {
    rejected = std::string_view(error.what()).find("event count limit") !=
        std::string_view::npos;
  }
  require(rejected, "debug event count limit was not enforced");
  require(std::chrono::steady_clock::now() - started < std::chrono::seconds(5),
          "event-limit failure did not reclaim the debuggee promptly");
  require_no_markers(paths, "event-limit probe");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 3, "fixture target and module arguments are required");
    const std::filesystem::path target = std::filesystem::absolute(arguments[1]);
    const std::filesystem::path module = std::filesystem::absolute(arguments[2]);
    const auto paths = make_fixture_paths(module);
    environment_marker exe_marker(L"NCM_MODULE_LOAD_PROBE_EXE_MARKER", paths.executable_marker);
    environment_marker dll_marker(L"NCM_MODULE_LOAD_PROBE_DLL_MARKER", paths.dll_marker);

    try {
      test_expected_path(target, module, paths);
      test_unexpected_path(target, paths);
      test_signatures_required_for_cli_contract(target, module, paths);
      test_event_limit_fails_closed(target, module, paths);
    } catch (...) {
      cleanup_fixture_paths(paths);
      throw;
    }
    cleanup_fixture_paths(paths);
    std::cout << "module load probe tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
