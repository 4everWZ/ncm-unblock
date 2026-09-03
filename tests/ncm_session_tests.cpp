#include "ncm/launcher/ncm_session.hpp"

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void test_root_crash_does_not_end_live_session(
    const std::filesystem::path& fixture) {
  auto session = ncm::launcher::ncm_session::launch(fixture);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  require(session.active(), "crash fixture session was not active");
  const auto root = OpenProcess(
      PROCESS_TERMINATE | SYNCHRONIZE, FALSE, session.root_process_id());
  require(root != nullptr, "unable to open crash fixture root");
  const auto terminated = TerminateProcess(root, 71) != FALSE;
  const auto stopped = WaitForSingleObject(root, 5000) == WAIT_OBJECT_0;
  CloseHandle(root);
  require(terminated && stopped, "unable to terminate crash fixture root");
  require(session.active(), "root crash ended a session with a live child");
  std::this_thread::sleep_for(std::chrono::milliseconds(1300));
  require(!session.active(), "crashed session remained after its child exited");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 2, "fixture path is missing");
    const std::filesystem::path fixture(arguments[1]);
    require(!ncm::launcher::ncm_session::target_running(fixture),
            "fixture was already running");
    auto session = ncm::launcher::ncm_session::launch(fixture);
    require(session.root_process_id() != 0, "root PID was not recorded");
    require(session.active(), "fresh session was not active");
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    require(session.active(), "session ended when only the root exited");
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    require(!session.active(), "session remained active after its full tree exited");
    require(!ncm::launcher::ncm_session::target_running(fixture),
            "exited fixture was reported as running");
    test_root_crash_does_not_end_live_session(fixture);
    std::cout << "NCM session tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
