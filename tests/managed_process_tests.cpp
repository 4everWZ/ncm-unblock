#include "ncm/launcher/managed_process.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] ncm::launcher::process_spec child_spec(
    const std::filesystem::path& child, std::vector<std::wstring> arguments) {
  return {child, child.parent_path(), std::move(arguments)};
}

[[nodiscard]] std::optional<std::uint32_t> find_child_process(std::uint32_t parent_id) {
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    throw std::runtime_error("unable to create child process snapshot");
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) {
    CloseHandle(snapshot);
    throw std::runtime_error("unable to enumerate child processes");
  }

  std::optional<std::uint32_t> result;
  do {
    if (entry.th32ParentProcessID == parent_id) {
      result = entry.th32ProcessID;
      break;
    }
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);
  return result;
}

void test_exit_code(const std::filesystem::path& child) {
  auto process = ncm::launcher::managed_process::start(child_spec(child, {L"--exit", L"7"}));
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 7, "child exit code was not preserved");
  require(!process.root_running(), "exited child reported as running");
}

void test_argument_quoting(const std::filesystem::path& child) {
  auto process = ncm::launcher::managed_process::start(child_spec(
      child, {L"--check-args", L"value with spaces", L"trailing\\", L"quote\"value", L"",
              L"space trailing\\", L"two trailing\\\\", L"slash\\\"quote", L"after-empty"}));
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 0, "Windows argument quoting changed values");
}

void test_child_starts_inside_job(const std::filesystem::path& child) {
  auto process = ncm::launcher::managed_process::start(
      child_spec(child, {L"--require-job"}));
  require(process.contains_process(process.process_id()),
          "child is not contained by the launcher's private job");
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 0,
          "child user code ran without job ownership");
}

void test_timeout_and_termination(const std::filesystem::path& child) {
  auto process = ncm::launcher::managed_process::start(child_spec(child, {L"--sleep", L"30000"}));
  require(!process.wait_for_root(std::chrono::milliseconds(50)).has_value(),
          "sleeping child did not time out");
  require(process.terminate_and_wait_tree(23, std::chrono::seconds(5)),
          "job tree did not terminate within the deadline");
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 23, "job termination exit code was not preserved");
}

void test_job_close_reclaims_child(const std::filesystem::path& child) {
  HANDLE observation{};
  {
    auto process = ncm::launcher::managed_process::start(child_spec(child, {L"--sleep", L"30000"}));
    observation = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.process_id());
    require(observation != nullptr, "unable to retain child observation handle");
  }

  const auto status = WaitForSingleObject(observation, 5000);
  DWORD exit_code = STILL_ACTIVE;
  const auto queried = GetExitCodeProcess(observation, &exit_code);
  CloseHandle(observation);
  require(status == WAIT_OBJECT_0 && queried && exit_code != STILL_ACTIVE,
          "closing the job did not reclaim the child");
}

void test_unresumed_process_is_reclaimed(const std::filesystem::path& child) {
  HANDLE observation{};
  {
    auto process = ncm::launcher::managed_process::prepare(
        child_spec(child, {L"--sleep", L"30000"}));
    require(process.suspended(), "prepared fixture was not suspended");
    observation = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.process_id());
    require(observation != nullptr, "unable to retain suspended child observation handle");
  }
  const auto status = WaitForSingleObject(observation, 5000);
  CloseHandle(observation);
  require(status == WAIT_OBJECT_0, "unresumed managed process was not reclaimed");
}

void test_job_close_reclaims_tree(const std::filesystem::path& child) {
  HANDLE parent_observation{};
  HANDLE child_observation{};
  {
    auto process = ncm::launcher::managed_process::start(
        child_spec(child, {L"--spawn-child"}));
    parent_observation = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process.process_id());
    require(parent_observation != nullptr, "unable to observe managed parent");

    const auto deadline = GetTickCount64() + 5000;
    std::optional<std::uint32_t> child_id;
    do {
      child_id = find_child_process(process.process_id());
      if (child_id.has_value()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    } while (GetTickCount64() < deadline);
    require(child_id.has_value(), "managed parent did not create its child");
    child_observation = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, *child_id);
    require(child_observation != nullptr, "unable to observe managed child");
  }

  const auto parent_status = WaitForSingleObject(parent_observation, 5000);
  const auto child_status = WaitForSingleObject(child_observation, 5000);
  CloseHandle(parent_observation);
  CloseHandle(child_observation);
  require(parent_status == WAIT_OBJECT_0 && child_status == WAIT_OBJECT_0,
          "closing the job did not reclaim the process tree");
}

void test_root_exit_does_not_imply_tree_exit(const std::filesystem::path& child) {
  auto process = ncm::launcher::managed_process::start(
      child_spec(child, {L"--spawn-child-and-exit"}));
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 0, "managed root did not exit cleanly");
  require(!process.wait_for_tree(std::chrono::milliseconds(50)),
          "root exit was incorrectly treated as an empty process tree");
  require(process.terminate_and_wait_tree(23, std::chrono::seconds(5)),
          "orphaned managed descendant was not reclaimed");
}

void test_job_close_leaves_unrelated_process(const std::filesystem::path& child) {
  auto command_line = L"\"" + child.wstring() + L"\" --sleep 30000";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION unrelated{};
  require(CreateProcessW(
              child.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
              nullptr, child.parent_path().c_str(), &startup, &unrelated) != FALSE,
          "unable to start unrelated child fixture");
  CloseHandle(unrelated.hThread);

  try {
    {
      auto process = ncm::launcher::managed_process::start(
          child_spec(child, {L"--sleep", L"30000"}));
      require(process.root_running(), "managed child did not start");
    }
    require(WaitForSingleObject(unrelated.hProcess, 100) == WAIT_TIMEOUT,
            "closing the private job terminated an unrelated process");
  } catch (...) {
    TerminateProcess(unrelated.hProcess, 125);
    WaitForSingleObject(unrelated.hProcess, 5000);
    CloseHandle(unrelated.hProcess);
    throw;
  }

  TerminateProcess(unrelated.hProcess, 0);
  WaitForSingleObject(unrelated.hProcess, 5000);
  CloseHandle(unrelated.hProcess);
}

void test_relative_path_rejected() {
  bool rejected{};
  try {
    (void)ncm::launcher::managed_process::start(
        child_spec(L"managed_process_test_child.exe", {L"--exit", L"0"}));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "relative managed executable path was accepted");
}

void test_invalid_launch_inputs_rejected(const std::filesystem::path& child) {
  bool relative_working_directory_rejected{};
  try {
    auto spec = child_spec(child, {L"--exit", L"0"});
    spec.working_directory = L"relative";
    (void)ncm::launcher::managed_process::start(spec);
  } catch (const std::invalid_argument&) {
    relative_working_directory_rejected = true;
  }
  require(relative_working_directory_rejected, "relative working directory was accepted");

  bool embedded_nul_rejected{};
  try {
    std::wstring invalid{L'a', L'\0', L'b'};
    (void)ncm::launcher::managed_process::start(
        child_spec(child, {L"--exit", std::move(invalid)}));
  } catch (const std::invalid_argument&) {
    embedded_nul_rejected = true;
  }
  require(embedded_nul_rejected, "embedded NUL in an argument was accepted");

  bool missing_executable_rejected{};
  try {
    (void)ncm::launcher::managed_process::start(child_spec(
        child.parent_path() / L"missing-managed-process.exe", {L"--exit", L"0"}));
  } catch (const std::system_error&) {
    missing_executable_rejected = true;
  }
  require(missing_executable_rejected, "missing executable launch did not fail");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 2, "test child path argument is missing");
    const std::filesystem::path child(arguments[1]);
    test_exit_code(child);
    test_argument_quoting(child);
    test_child_starts_inside_job(child);
    test_timeout_and_termination(child);
    test_job_close_reclaims_child(child);
    test_unresumed_process_is_reclaimed(child);
    test_job_close_reclaims_tree(child);
    test_root_exit_does_not_imply_tree_exit(child);
    test_job_close_leaves_unrelated_process(child);
    test_relative_path_rejected();
    test_invalid_launch_inputs_rejected(child);
    std::cout << "managed process tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
