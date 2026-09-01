#include "ncm/winmm_proxy/forwarder.hpp"

#include <Windows.h>

// Backend location for the synthetic parity fixture. The test supplies the full
// path of a repository-built backend so export parity, module identity, and the
// x86 calling convention can be validated without involving the system module.
// Production resolves the system directory instead; there is no switch between
// the two, only a different translation unit in a different DLL.
namespace ncm::winmm_proxy {

extern "C" bool ncm_winmm_backend_path(wchar_t* buffer, unsigned count) noexcept {
  if (buffer == nullptr || count == 0) {
    return false;
  }
  const auto written = GetEnvironmentVariableW(
      L"NCM_WINMM_FIXTURE_BACKEND", buffer, count);
  return written != 0 && written < count;
}

}  // namespace ncm::winmm_proxy
