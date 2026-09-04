#include "ncm/host/ncm_watch.hpp"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct owned_process {
  PROCESS_INFORMATION information{};

  owned_process(
      const std::filesystem::path& executable, const std::wstring& arguments) {
    auto command_line = L"\"" + executable.wstring() + L"\"";
    if (!arguments.empty()) {
      command_line.push_back(L' ');
      command_line.append(arguments);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    require(
        CreateProcessW(
            executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
            nullptr, executable.parent_path().c_str(), &startup, &information) !=
            FALSE,
        "unable to start ncm_watch fixture");
    CloseHandle(information.hThread);
    information.hThread = nullptr;
  }

  ~owned_process() {
    if (information.hProcess != nullptr) {
      TerminateProcess(information.hProcess, 125);
      WaitForSingleObject(information.hProcess, 5000);
      CloseHandle(information.hProcess);
    }
  }

  owned_process(const owned_process&) = delete;
  owned_process& operator=(const owned_process&) = delete;
};

void test_rejects_relative_path() {
  const auto attached = ncm::host::ncm_watch::attach(L"cloudmusic.exe");
  require(!attached.has_value(), "relative NCM path was accepted");
}

void test_attaches_to_main_and_ignores_utility(
    const std::filesystem::path& client) {
  owned_process utility(client, L"--type=renderer");
  const auto ignored = ncm::host::ncm_watch::attach(client);
  require(
      !ignored.has_value() || ignored->process_id() != utility.information.dwProcessId,
      "CEF utility process was treated as NCM main");

  owned_process main_process(client, {});
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  std::optional<ncm::host::ncm_watch> attached;
  while (std::chrono::steady_clock::now() < deadline) {
    attached = ncm::host::ncm_watch::attach(client);
    if (attached.has_value() &&
        attached->process_id() == main_process.information.dwProcessId) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  require(
      attached.has_value() &&
          attached->process_id() == main_process.information.dwProcessId,
      "NCM main process was not attached");
  require(attached->alive(), "attached NCM main was reported dead");
  require(attached->wait_handle() != nullptr, "attached NCM main has no wait handle");

  require(
      TerminateProcess(main_process.information.hProcess, 0) != FALSE,
      "unable to end fixture main process");
  require(
      WaitForSingleObject(main_process.information.hProcess, 5000) == WAIT_OBJECT_0,
      "fixture main process did not exit");
  CloseHandle(main_process.information.hProcess);
  main_process.information.hProcess = nullptr;
  require(!attached->alive(), "exited NCM main was still reported alive");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 2, "test client path argument is missing");
    const std::filesystem::path client(arguments[1]);
    test_rejects_relative_path();
    test_attaches_to_main_and_ignores_utility(client);
    std::cout << "ncm watch tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
