#include "ncm/bootstrap/deferred_start.hpp"

#include <Windows.h>

namespace ncm::bootstrap {
namespace {

// One counter serves both the bootstrap and any observer, so their tickets are
// directly comparable.
long g_sequence{};

void (*g_body)() noexcept {};
long g_stage{static_cast<long>(stage::idle)};
long g_body_sequence{};
long g_body_thread{};
HANDLE g_thread{};

[[nodiscard]] unsigned long take_sequence() noexcept {
  return static_cast<unsigned long>(InterlockedIncrement(&g_sequence));
}

DWORD WINAPI run_body(LPVOID) noexcept {
  InterlockedExchange(&g_body_thread, static_cast<long>(GetCurrentThreadId()));
  InterlockedExchange(&g_body_sequence, static_cast<long>(take_sequence()));
  InterlockedExchange(&g_stage, static_cast<long>(stage::running));

  if (g_body != nullptr) {
    g_body();
  }

  InterlockedExchange(&g_stage, static_cast<long>(stage::finished));
  return 0;
}

}  // namespace

bool schedule(void (*body)() noexcept) noexcept {
  if (body == nullptr) {
    return false;
  }
  if (InterlockedCompareExchange(
          &g_stage, static_cast<long>(stage::scheduled),
          static_cast<long>(stage::idle)) != static_cast<long>(stage::idle)) {
    return false;
  }

  g_body = body;
  // The thread is created here but cannot start until the loader releases its
  // lock, which is exactly the handoff this module exists to provide.
  g_thread = CreateThread(nullptr, 0, run_body, nullptr, 0, nullptr);
  if (g_thread == nullptr) {
    InterlockedExchange(&g_stage, static_cast<long>(stage::idle));
    g_body = nullptr;
    OutputDebugStringW(L"ncm-unblock bootstrap: unable to create the bootstrap thread\r\n");
    return false;
  }
  return true;
}

stage current() noexcept {
  return static_cast<stage>(InterlockedCompareExchange(&g_stage, 0, 0));
}

bool wait(unsigned timeout_milliseconds) noexcept {
  const HANDLE thread = g_thread;
  if (thread == nullptr) {
    return current() == stage::finished;
  }
  return WaitForSingleObject(thread, timeout_milliseconds) == WAIT_OBJECT_0;
}

unsigned long observe_sequence() noexcept {
  return take_sequence();
}

unsigned long body_sequence() noexcept {
  return static_cast<unsigned long>(InterlockedCompareExchange(&g_body_sequence, 0, 0));
}

unsigned long body_thread_id() noexcept {
  return static_cast<unsigned long>(InterlockedCompareExchange(&g_body_thread, 0, 0));
}

}  // namespace ncm::bootstrap
