#include "ncm/winmm_proxy/forwarder.hpp"

#include <Windows.h>

#include <iterator>

namespace ncm::winmm_proxy {

// The production backend is the WinMM in the 32-bit system directory. On WOW64
// `GetSystemDirectoryW` returns `SysWOW64`, which is the directory the loader
// would have used had no proxy been present.
extern "C" bool ncm_winmm_backend_path(wchar_t* buffer, unsigned count) noexcept {
  static constexpr wchar_t module_name[] = L"winmm.dll";
  static constexpr unsigned module_length = static_cast<unsigned>(std::size(module_name)) - 1;

  if (buffer == nullptr || count == 0) {
    return false;
  }

  const auto directory_length = GetSystemDirectoryW(buffer, count);
  if (directory_length == 0 || directory_length >= count) {
    return false;
  }

  auto length = directory_length;
  if (buffer[length - 1] != L'\\') {
    if (length + 1 >= count) {
      return false;
    }
    buffer[length++] = L'\\';
  }
  if (length + module_length >= count) {
    return false;
  }
  for (unsigned index = 0; index < module_length; ++index) {
    buffer[length + index] = module_name[index];
  }
  buffer[length + module_length] = L'\0';
  return true;
}

}  // namespace ncm::winmm_proxy
