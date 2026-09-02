#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] std::string utf8_path(const std::filesystem::path& path) {
  const auto wide = path.wstring();
  if (wide.empty()) {
    return {};
  }
  const int needed = WideCharToMultiByte(
      CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0,
      nullptr, nullptr);
  require(needed > 0, "unable to encode a path as UTF-8");
  std::string result(static_cast<std::size_t>(needed), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), result.data(),
      needed, nullptr, nullptr);
  return result;
}

class scoped_environment {
 public:
  scoped_environment(std::wstring name, const std::wstring& value)
      : name_(std::move(name)) {
    const auto required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
    if (required != 0) {
      previous_.resize(required);
      const auto written = GetEnvironmentVariableW(
          name_.c_str(), previous_.data(), static_cast<DWORD>(previous_.size()));
      require(
          written != 0 && written < previous_.size(),
          "unable to preserve an environment value");
      previous_.resize(written);
      had_previous_ = true;
    }
    require(
        SetEnvironmentVariableW(name_.c_str(), value.c_str()) != 0,
        "unable to publish the fixture backend path");
  }

  ~scoped_environment() {
    SetEnvironmentVariableW(name_.c_str(), had_previous_ ? previous_.c_str() : nullptr);
  }

  scoped_environment(const scoped_environment&) = delete;
  scoped_environment& operator=(const scoped_environment&) = delete;

 private:
  std::wstring name_;
  std::wstring previous_;
  bool had_previous_{};
};

class staged_host {
 public:
  staged_host(
      const std::filesystem::path& probe, const std::wstring& label) {
    root_ = std::filesystem::temp_directory_path() /
        (L"ncm_winmm_session_tests_" + label + L"_" +
         std::to_wstring(GetCurrentProcessId()) + L"_" +
         std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(root_);
    probe_ = root_ / probe.filename();
    std::error_code status;
    std::filesystem::copy_file(
        probe, probe_, std::filesystem::copy_options::overwrite_existing, status);
    require(!status, "unable to stage the session probe");
    report_ = root_ / L"session.txt";
  }

  ~staged_host() {
    std::error_code status;
    std::filesystem::remove_all(root_, status);
  }

  staged_host(const staged_host&) = delete;
  staged_host& operator=(const staged_host&) = delete;

  void write_settings(std::string_view text) const {
    std::ofstream stream(root_ / L"ncm_unblock.ini", std::ios::binary);
    require(static_cast<bool>(stream), "unable to write the staged configuration");
    stream << text;
  }

  [[nodiscard]] const std::filesystem::path& probe() const noexcept { return probe_; }
  [[nodiscard]] const std::filesystem::path& report() const noexcept { return report_; }
  [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

 private:
  std::filesystem::path root_;
  std::filesystem::path probe_;
  std::filesystem::path report_;
};

struct host_outcome {
  DWORD exit_code{};
  std::string report;
  DWORD process_id{};
  HANDLE process{};
};

void close_host(host_outcome& host) noexcept {
  if (host.process != nullptr) {
    TerminateProcess(host.process, 1);
    WaitForSingleObject(host.process, 5000);
    CloseHandle(host.process);
    host.process = nullptr;
  }
}

[[nodiscard]] host_outcome start_host(const staged_host& staged) {
  std::wstring command_line =
      L"\"" + staged.probe().wstring() + L"\" \"" + staged.report().wstring() + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  require(
      CreateProcessW(
          staged.probe().c_str(), command_line.data(), nullptr, nullptr, FALSE,
          CREATE_NO_WINDOW, nullptr, staged.root().c_str(), &startup, &process) != 0,
      "unable to start the session probe");
  CloseHandle(process.hThread);

  host_outcome outcome;
  outcome.process = process.hProcess;
  outcome.process_id = process.dwProcessId;

  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) {
      GetExitCodeProcess(process.hProcess, &outcome.exit_code);
      CloseHandle(process.hProcess);
      outcome.process = nullptr;
      break;
    }
    std::ifstream report(staged.report(), std::ios::binary);
    if (report) {
      outcome.report.assign(
          (std::istreambuf_iterator<char>(report)),
          std::istreambuf_iterator<char>());
      if (outcome.report.find("result=") != std::string::npos) {
        return outcome;
      }
    }
    Sleep(50);
  }

  if (outcome.report.find("result=") == std::string::npos) {
    close_host(outcome);
    throw std::runtime_error("the session probe did not publish a result");
  }
  return outcome;
}

[[nodiscard]] unsigned long field_value(
    const std::string& report, const char* name) {
  const std::string key = std::string(name) + "=";
  const auto offset = report.find(key);
  require(offset != std::string::npos, "session report is missing " + key);
  return std::stoul(report.substr(offset + key.size()));
}

void require_result(const std::string& report, const char* expected) {
  require(
      report.find(std::string("result=") + expected) != std::string::npos,
      "session result was not " + std::string(expected) + ": " + report);
}

[[nodiscard]] bool process_running(DWORD process_id) {
  const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, process_id);
  if (process == nullptr) {
    return false;
  }
  const auto state = WaitForSingleObject(process, 0);
  CloseHandle(process);
  return state == WAIT_TIMEOUT;
}

void test_ready_sidecar_is_owned_and_reclaimed_with_the_host(
    const std::filesystem::path& probe, const std::filesystem::path& backend,
    const std::filesystem::path& child) {
  staged_host host(probe, L"ready");
  host.write_settings(
      "sidecar_executable = " + utf8_path(child) +
      "\nautomatic_attempts = 1\nreadiness_timeout_ms = 5000\n");
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", backend.wstring());

  auto outcome = start_host(host);
  try {
    require_result(outcome.report, "sidecar_ready");
    const auto sidecar_pid = field_value(outcome.report, "pid");
    require(sidecar_pid != 0, "a ready sidecar published no process id");
    require(
        field_value(outcome.report, "http") != 0 &&
            field_value(outcome.report, "https") != 0 &&
            field_value(outcome.report, "http") != field_value(outcome.report, "https"),
        "a ready sidecar published an invalid port pair");
    require(process_running(sidecar_pid), "the ready sidecar was not running");

    const HANDLE sidecar = OpenProcess(SYNCHRONIZE, FALSE, sidecar_pid);
    require(sidecar != nullptr, "unable to observe the ready sidecar");
    close_host(outcome);
    const auto reclaimed = WaitForSingleObject(sidecar, 5000);
    CloseHandle(sidecar);
    require(
        reclaimed == WAIT_OBJECT_0,
        "terminating the owning process did not reclaim the sidecar");
  } catch (...) {
    close_host(outcome);
    throw;
  }
}

void test_missing_sidecar_declines_without_stopping_the_host(
    const std::filesystem::path& probe, const std::filesystem::path& backend,
    const std::filesystem::path& child) {
  staged_host host(probe, L"missing");
  host.write_settings(
      "sidecar_executable = " + utf8_path(child.parent_path() / L"absent-unm.exe") +
      "\nautomatic_attempts = 1\nreadiness_timeout_ms = 1000\n");
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", backend.wstring());

  auto outcome = start_host(host);
  try {
    require(
        outcome.process != nullptr,
        "a missing sidecar stopped the host instead of declining the feature");
    require_result(outcome.report, "sidecar_failed");
    require(
        field_value(outcome.report, "pid") == 0,
        "a failed sidecar still published a process id");
    close_host(outcome);
  } catch (...) {
    close_host(outcome);
    throw;
  }
}

void test_disabled_and_invalid_configuration_never_start_a_sidecar(
    const std::filesystem::path& probe, const std::filesystem::path& backend,
    const std::filesystem::path& child) {
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", backend.wstring());

  {
    staged_host host(probe, L"disabled");
    host.write_settings(
        "enabled = false\nsidecar_executable = " + utf8_path(child) + "\n");
    auto outcome = start_host(host);
    try {
      require_result(outcome.report, "disabled");
      require(
          field_value(outcome.report, "pid") == 0,
          "a disabled session still started a sidecar");
      close_host(outcome);
    } catch (...) {
      close_host(outcome);
      throw;
    }
  }

  {
    staged_host host(probe, L"invalid");
    host.write_settings("enabled = perhaps\n");
    auto outcome = start_host(host);
    try {
      require_result(outcome.report, "configuration_invalid");
      require(
          field_value(outcome.report, "pid") == 0,
          "an invalid configuration still started a sidecar");
      close_host(outcome);
    } catch (...) {
      close_host(outcome);
      throw;
    }
  }
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(
        argument_count == 4,
        "probe, backend fixture, and sidecar fixture paths are required");
    const auto probe = std::filesystem::canonical(arguments[1]);
    const auto backend = std::filesystem::canonical(arguments[2]);
    const auto child = std::filesystem::canonical(arguments[3]);

    test_ready_sidecar_is_owned_and_reclaimed_with_the_host(probe, backend, child);
    test_missing_sidecar_declines_without_stopping_the_host(probe, backend, child);
    test_disabled_and_invalid_configuration_never_start_a_sidecar(
        probe, backend, child);

    std::cout << "winmm session tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
