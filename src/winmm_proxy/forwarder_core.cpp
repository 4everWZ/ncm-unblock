#include "ncm/winmm_proxy/forwarder.hpp"

#include <Windows.h>

#include <iterator>

namespace ncm::winmm_proxy {
namespace {

// `TerminateProcess` rather than a fail-fast exception: an unmet backend
// contract is an environment error, not memory corruption, and a Windows Error
// Reporting dialog on top of a music player helps nobody. The exit code is
// distinctive enough to identify in a crash report or a test.
[[noreturn]] void fail_closed(const wchar_t* reason) noexcept {
  OutputDebugStringW(L"ncm-unblock winmm proxy: ");
  OutputDebugStringW(reason);
  OutputDebugStringW(L"\r\n");
  TerminateProcess(GetCurrentProcess(), backend_failure_exit_code);
  for (;;) {
  }
}

struct resolve_request {
  long* state;
  void** targets;
  unsigned count;
};

// Resolves the backend by absolute path only. A module name would be resolved
// through the loader's search order, which finds this proxy first whenever the
// proxy carries the backend's own file name.
[[nodiscard]] HMODULE load_distinct_backend(HMODULE self) noexcept {
  wchar_t requested[MAX_PATH]{};
  if (!ncm_winmm_backend_path(requested, static_cast<unsigned>(std::size(requested)))) {
    fail_closed(L"backend path is unavailable");
  }

  const HMODULE backend = LoadLibraryExW(requested, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (backend == nullptr) {
    fail_closed(L"backend module could not be loaded");
  }
  if (backend == self) {
    fail_closed(L"backend resolved to the proxy module itself");
  }

  wchar_t loaded[MAX_PATH]{};
  const auto written = GetModuleFileNameW(backend, loaded, static_cast<DWORD>(std::size(loaded)));
  if (written == 0 || written >= std::size(loaded)) {
    fail_closed(L"backend module path is unavailable");
  }
  if (CompareStringOrdinal(loaded, -1, requested, -1, TRUE) != CSTR_EQUAL) {
    fail_closed(L"backend module is not the requested file");
  }
  return backend;
}

BOOL CALLBACK resolve_once(PINIT_ONCE, PVOID parameter, PVOID*) noexcept {
  const auto& request = *static_cast<const resolve_request*>(parameter);

  HMODULE self{};
  if (GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(request.targets), &self) == 0 ||
      self == nullptr) {
    fail_closed(L"unable to identify the proxy module");
  }

  unsigned pinned_count{};
  const export_entry* const entries = pinned_exports(&pinned_count);
  if (entries == nullptr || pinned_count != request.count) {
    fail_closed(L"pinned export table does not match the generated thunk table");
  }

  const HMODULE backend = load_distinct_backend(self);
  for (unsigned index = 0; index < request.count; ++index) {
    const auto& entry = entries[index];
    const FARPROC address = entry.name != nullptr
        ? GetProcAddress(backend, entry.name)
        : GetProcAddress(backend, MAKEINTRESOURCEA(entry.ordinal));
    if (address == nullptr) {
      fail_closed(L"backend module is missing a pinned export");
    }
    request.targets[index] = reinterpret_cast<void*>(address);
  }

  // Every target is stored before the thunks are allowed to stop calling here.
  // The interlocked write is a full barrier, and x86 does not reorder the
  // dependent loads a thunk performs afterwards.
  InterlockedExchange(request.state, 1);
  return TRUE;
}

INIT_ONCE g_resolve_once = INIT_ONCE_STATIC_INIT;

}  // namespace

void resolve_backend(long* state, void** targets, unsigned count) noexcept {
  if (state == nullptr || targets == nullptr || count == 0) {
    fail_closed(L"thunk table is not initialized");
  }
  resolve_request request{state, targets, count};
  if (InitOnceExecuteOnce(&g_resolve_once, resolve_once, &request, nullptr) == 0) {
    fail_closed(L"backend resolution did not complete");
  }
}

}  // namespace ncm::winmm_proxy
