#include "ncm/bootstrap/deferred_start.hpp"
#include "ncm/winmm_proxy/session.hpp"

#include <Windows.h>

// Entry point shared by the production proxy and the parity fixture, so the
// tested DLL and the shipped DLL take the same path into the bootstrap.
//
// Nothing here may do bootstrap work: `DLL_PROCESS_ATTACH` runs under the
// loader lock, and the session body loads a module. Scheduling hands that work
// to a thread the loader cannot start until it releases the lock.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    // Forwarding must not depend on the bootstrap, so a failure to schedule is
    // reported by the bootstrap and otherwise ignored here.
    static_cast<void>(ncm::bootstrap::schedule(&ncm::winmm_proxy::prepare_session));
  }
  return TRUE;
}
