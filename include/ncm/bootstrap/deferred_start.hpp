#pragma once

namespace ncm::bootstrap {

// Where a scheduled bootstrap body has got to.
enum class stage {
  idle,
  // The body has been handed to a private thread that has not started it yet.
  scheduled,
  running,
  finished,
};

// Runs `body` on a private thread once the loader has released its lock.
//
// Safe to call from `DllMain`: it creates the thread and returns without
// waiting. Windows does not run a thread created during `DLL_PROCESS_ATTACH`
// until the loader lock is released, so the body never executes inside the
// notification that scheduled it. At most one body may be scheduled per
// process; a second call fails.
//
// Returns false when the thread could not be created. Callers must keep
// working without the bootstrap in that case rather than failing the host.
[[nodiscard]] bool schedule(void (*body)() noexcept) noexcept;

[[nodiscard]] stage current() noexcept;

// Waits for the body to finish. Never call this from `DllMain`: the body runs
// on a thread the loader cannot start until the notification returns, so a
// wait inside `DLL_PROCESS_ATTACH` deadlocks.
[[nodiscard]] bool wait(unsigned timeout_milliseconds) noexcept;

// Ordering tickets drawn from the counter the bootstrap itself uses. A caller
// that takes one after `schedule` returns can prove the body did not run
// inside the notification by comparing it against `body_sequence`.
[[nodiscard]] unsigned long observe_sequence() noexcept;
[[nodiscard]] unsigned long body_sequence() noexcept;
[[nodiscard]] unsigned long body_thread_id() noexcept;

}  // namespace ncm::bootstrap
