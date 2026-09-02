#include "ncm/bootstrap/deferred_start.hpp"
#include "ncm/network_stack_census/census.hpp"

#include <Windows.h>

// Entry point for the census-flavored WinMM proxy. It forwards the same pinned
// surface as the production proxy and differs only in the bootstrap body it
// schedules, so the client under observation is not running a second mechanism.
//
// `DLL_PROCESS_ATTACH` runs under the loader lock, and the census registers a
// loader notification and snapshots modules, so the work is handed to a thread
// the loader cannot start until it releases the lock.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    static_cast<void>(
        ncm::bootstrap::schedule(&ncm::network_stack_census::run_census));
  }
  return TRUE;
}
