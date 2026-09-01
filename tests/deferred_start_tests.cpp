#include "ncm/bootstrap/deferred_start.hpp"

#include <Windows.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

// The body blocks until the test releases it, so a `schedule` that ran its body
// inline would never return and the run would fail on the budget rather than
// pass by luck.
HANDLE g_release{};
unsigned long g_observed_at_body_entry{};

void body() noexcept {
  g_observed_at_body_entry = ncm::bootstrap::observe_sequence();
  WaitForSingleObject(g_release, 10000);
}

void test_body_runs_after_scheduling_returns() {
  g_release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  require(g_release != nullptr, "unable to create the release event");

  const auto before = ncm::bootstrap::observe_sequence();
  require(ncm::bootstrap::current() == ncm::bootstrap::stage::idle,
          "the bootstrap was not idle before scheduling");

  const auto started = GetTickCount64();
  require(ncm::bootstrap::schedule(&body), "the bootstrap could not be scheduled");
  const auto elapsed = GetTickCount64() - started;
  require(elapsed < 1000,
          "scheduling did not return promptly, so it did not hand the body off");

  const auto after = ncm::bootstrap::observe_sequence();
  require(after > before, "the ordering counter did not advance");

  require(!ncm::bootstrap::wait(200),
          "the body finished while it was still blocked, so it did not run where it was told to");

  SetEvent(g_release);
  require(ncm::bootstrap::wait(10000), "the body did not finish after it was released");
  require(ncm::bootstrap::current() == ncm::bootstrap::stage::finished,
          "the bootstrap did not reach its finished stage");

  require(ncm::bootstrap::body_sequence() > after,
          "the body ran before scheduling returned");
  require(g_observed_at_body_entry > after, "the body observed a stale ordering ticket");
  require(ncm::bootstrap::body_thread_id() != GetCurrentThreadId(),
          "the body ran on the scheduling thread");

  CloseHandle(g_release);
  g_release = nullptr;
}

void test_second_schedule_is_refused() {
  require(!ncm::bootstrap::schedule(&body),
          "a second bootstrap body was accepted for the same process");
  require(!ncm::bootstrap::schedule(nullptr), "a null bootstrap body was accepted");
}

}  // namespace

int wmain() {
  try {
    test_body_runs_after_scheduling_returns();
    test_second_schedule_is_refused();
    std::cout << "deferred start tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
