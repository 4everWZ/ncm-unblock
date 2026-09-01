#include "ncm/winmm_proxy/forwarder.hpp"

#include <Windows.h>

namespace ncm::winmm_proxy {

// Reading an export directory needs no backend, so it lives apart from the
// resolver and can be exercised against any loaded module.
bool module_surface_shape(void* module, surface_shape* shape) noexcept {
  if (module == nullptr || shape == nullptr) {
    return false;
  }

  const auto* const base = static_cast<const unsigned char*>(module);
  const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return false;
  }
  const auto* const headers = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (headers->Signature != IMAGE_NT_SIGNATURE ||
      headers->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
      headers->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
    return false;
  }

  const auto& directory = headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
  if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) {
    return false;
  }

  // A mapped image addresses its export directory by RVA from the module base.
  const auto* const exports =
      reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
  if (exports->Base > 0xFFFF || exports->NumberOfFunctions > 0xFFFF ||
      exports->NumberOfNames > exports->NumberOfFunctions) {
    return false;
  }

  shape->ordinal_base = static_cast<unsigned short>(exports->Base);
  shape->function_count = static_cast<unsigned short>(exports->NumberOfFunctions);
  shape->name_count = static_cast<unsigned short>(exports->NumberOfNames);
  return true;
}

}  // namespace ncm::winmm_proxy
