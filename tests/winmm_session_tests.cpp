#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
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
      require(written != 0 && written < previous_.size(),
              "unable to preserve an environment value");
      previous_.resize(written);
      had_previous_ = true;
    }
    require(SetEnvironmentVariableW(name_.c_str(), value.c_str()) != 0,
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
  staged_host(const std::filesystem::path& probe, const std::wstring& label) {
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
  std::string report;
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
  require(CreateProcessW(
              staged.probe().c_str(), command_line.data(), nullptr, nullptr, FALSE,
              CREATE_NO_WINDOW, nullptr, staged.root().c_str(), &startup, &process) != 0,
          "unable to start the session probe");
  CloseHandle(process.hThread);

  host_outcome outcome;
  outcome.process = process.hProcess;
  for (unsigned attempt = 0; attempt < 200; ++attempt) {
    require(WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0,
            "the bootstrap stopped its host");
    std::ifstream report(staged.report(), std::ios::binary);
    if (report) {
      outcome.report.assign(std::istreambuf_iterator<char>(report),
                            std::istreambuf_iterator<char>());
      if (outcome.report.find("result=") != std::string::npos) {
        return outcome;
      }
    }
    Sleep(50);
  }
  close_host(outcome);
  throw std::runtime_error("the session probe did not publish a result");
}

void require_result(const std::string& report, const char* expected) {
  require(report.find(std::string("result=") + expected) != std::string::npos,
          "session result was not " + std::string(expected) + ": " + report);
}

void test_enabled_session_selects_in_process_path(
    const std::filesystem::path& probe, const std::filesystem::path& backend) {
  staged_host host(probe, L"enabled");
  // These fallback settings remain valid configuration, but the production
  // bootstrap must not act on them while the in-process path is selected.
  host.write_settings(
      "sidecar_executable = definitely-absent-unm.exe\n"
      "http_port = 43181\nhttps_port = 43182\n");
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", backend.wstring());

  auto outcome = start_host(host);
  try {
    require_result(outcome.report, "injection_pending");
    close_host(outcome);
  } catch (...) {
    close_host(outcome);
    throw;
  }
}

void test_disabled_and_invalid_configuration_decline(
    const std::filesystem::path& probe, const std::filesystem::path& backend) {
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", backend.wstring());

  for (const auto& [label, settings, expected] : {
           std::tuple{L"disabled", "enabled = false\n", "disabled"},
           std::tuple{L"invalid", "enabled = perhaps\n", "configuration_invalid"}}) {
    staged_host host(probe, label);
    host.write_settings(settings);
    auto outcome = start_host(host);
    try {
      require_result(outcome.report, expected);
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
    require(argument_count == 3, "probe and backend fixture paths are required");
    const auto probe = std::filesystem::canonical(arguments[1]);
    const auto backend = std::filesystem::canonical(arguments[2]);

    test_enabled_session_selects_in_process_path(probe, backend);
    test_disabled_and_invalid_configuration_decline(probe, backend);

    std::cout << "winmm session tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
