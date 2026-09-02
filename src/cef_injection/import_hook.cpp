#include "ncm/cef_injection/import_hook.hpp"

#include "ncm/cef/abi_1916.hpp"
#include "ncm/cef_injection/app_wrapper.hpp"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string_view>

namespace ncm::cef_injection {
namespace {

struct unicode_string_view {
  unsigned short length;
  unsigned short maximum_length;
  wchar_t* buffer;
};
struct loader_notification_data {
  unsigned long flags;
  const unicode_string_view* full_name;
  const unicode_string_view* base_name;
  void* module;
  unsigned long image_size;
};
using loader_notification = void(CALLBACK*)(
    unsigned long, const loader_notification_data*, void*);
using register_notification_fn = long(NTAPI*)(
    unsigned long, loader_notification, void*, void**);
using unregister_notification_fn = long(NTAPI*)(void*);

using execute_process_fn = int(__cdecl*)(
    const cef::cef_main_args_t*, cef::cef_app_t*, void*);
using api_hash_fn = const char*(__cdecl*)(int);

execute_process_fn g_original_execute{};
register_extension_fn g_register_extension{};

int __cdecl intercepted_execute_process(
    const cef::cef_main_args_t* arguments, cef::cef_app_t* application,
    void* sandbox_info) {
  const auto wrapped = wrap_application(application, g_register_extension);
  const int result = g_original_execute(arguments, wrapped.application, sandbox_info);
  if (wrapped.wrapped) {
    wrapped.application->base.release(&wrapped.application->base);
  }
  return result;
}

[[nodiscard]] bool equal_ascii_case_insensitive(
    const char* left, std::string_view right) noexcept {
  if (left == nullptr) {
    return false;
  }
  for (std::size_t index = 0; index < right.size(); ++index) {
    char value = left[index];
    if (value >= 'A' && value <= 'Z') {
      value = static_cast<char>(value + ('a' - 'A'));
    }
    if (value != right[index]) {
      return false;
    }
  }
  return left[right.size()] == '\0';
}

[[nodiscard]] bool in_image(
    std::uintptr_t base, std::size_t image_size, std::uintptr_t address,
    std::size_t length) noexcept {
  return address >= base && length <= image_size &&
      address - base <= image_size - length;
}

void CALLBACK signal_module_change(
    unsigned long reason, const loader_notification_data*, void* context) {
  if (reason == 1 && context != nullptr) {
    SetEvent(static_cast<HANDLE>(context));
  }
}

}  // namespace

import_hook_result install_import_hook(void* client_module, void* cef_module) noexcept {
  if (g_original_execute != nullptr) {
    return import_hook_result::already_installed;
  }
  if (client_module == nullptr) {
    return import_hook_result::client_not_loaded;
  }
  if (cef_module == nullptr) {
    return import_hook_result::cef_not_loaded;
  }

  const auto api_hash = reinterpret_cast<api_hash_fn>(
      GetProcAddress(static_cast<HMODULE>(cef_module), "cef_api_hash"));
  const auto execute = reinterpret_cast<execute_process_fn>(
      GetProcAddress(static_cast<HMODULE>(cef_module), "cef_execute_process"));
  const auto register_extension = reinterpret_cast<register_extension_fn>(
      GetProcAddress(static_cast<HMODULE>(cef_module), "cef_register_extension"));
  if (api_hash == nullptr || execute == nullptr || register_extension == nullptr) {
    return import_hook_result::export_missing;
  }
  const char* const platform = api_hash(0);
  const char* const universal = api_hash(1);
  if (platform == nullptr || universal == nullptr ||
      !cef::matches_pinned_api(platform, universal)) {
    return import_hook_result::api_mismatch;
  }

  auto* const base = static_cast<std::byte*>(client_module);
  const auto base_address = reinterpret_cast<std::uintptr_t>(base);
  const auto* const dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
    return import_hook_result::invalid_image;
  }
  const auto* const nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      base + static_cast<std::size_t>(dos->e_lfanew));
  if (nt->Signature != IMAGE_NT_SIGNATURE ||
      nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
    return import_hook_result::invalid_image;
  }
  const std::size_t image_size = nt->OptionalHeader.SizeOfImage;
  const auto directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
      !in_image(base_address, image_size, base_address + directory.VirtualAddress,
                directory.Size)) {
    return import_hook_result::invalid_image;
  }

  auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
      base + directory.VirtualAddress);
  const std::size_t descriptor_limit = directory.Size / sizeof(*descriptor);
  for (std::size_t descriptor_index = 0; descriptor_index < descriptor_limit;
       ++descriptor_index, ++descriptor) {
    if (descriptor->Name == 0) {
      break;
    }
    if (!in_image(base_address, image_size, base_address + descriptor->Name, 1)) {
      return import_hook_result::invalid_image;
    }
    const auto* module_name = reinterpret_cast<const char*>(base + descriptor->Name);
    if (!equal_ascii_case_insensitive(module_name, "libcef.dll")) {
      continue;
    }
    if (descriptor->OriginalFirstThunk == 0 || descriptor->FirstThunk == 0) {
      return import_hook_result::invalid_image;
    }
    auto* names = reinterpret_cast<IMAGE_THUNK_DATA32*>(
        base + descriptor->OriginalFirstThunk);
    auto* slots = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + descriptor->FirstThunk);
    for (std::size_t index = 0; index < 4096; ++index) {
      if (!in_image(base_address, image_size,
                    reinterpret_cast<std::uintptr_t>(&names[index]), sizeof(names[index])) ||
          !in_image(base_address, image_size,
                    reinterpret_cast<std::uintptr_t>(&slots[index]), sizeof(slots[index]))) {
        return import_hook_result::invalid_image;
      }
      if (names[index].u1.AddressOfData == 0) {
        return import_hook_result::import_missing;
      }
      if (IMAGE_SNAP_BY_ORDINAL32(names[index].u1.Ordinal)) {
        continue;
      }
      const auto name_address = base_address + names[index].u1.AddressOfData;
      if (!in_image(base_address, image_size, name_address,
                    sizeof(IMAGE_IMPORT_BY_NAME))) {
        return import_hook_result::invalid_image;
      }
      const auto* imported = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(name_address);
      if (std::strcmp(reinterpret_cast<const char*>(imported->Name),
                      "cef_execute_process") != 0) {
        continue;
      }
      auto* const slot = reinterpret_cast<void**>(&slots[index].u1.Function);
      if (*slot != reinterpret_cast<void*>(execute)) {
        return import_hook_result::write_failed;
      }
      DWORD previous{};
      if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &previous)) {
        return import_hook_result::write_failed;
      }
      g_register_extension = register_extension;
      g_original_execute = execute;
      InterlockedExchangePointer(
          slot, reinterpret_cast<void*>(&intercepted_execute_process));
      DWORD ignored{};
      VirtualProtect(slot, sizeof(*slot), previous, &ignored);
      return import_hook_result::installed;
    }
    return import_hook_result::import_missing;
  }
  return import_hook_result::import_missing;
}

import_hook_result install_loaded_import_hook() noexcept {
  return install_import_hook(
      GetModuleHandleW(L"cloudmusic.dll"), GetModuleHandleW(L"libcef.dll"));
}

import_hook_result install_loaded_import_hook(unsigned timeout_milliseconds) noexcept {
  auto result = install_loaded_import_hook();
  if (result != import_hook_result::client_not_loaded &&
      result != import_hook_result::cef_not_loaded) {
    return result;
  }

  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto register_notification = ntdll == nullptr
      ? nullptr
      : reinterpret_cast<register_notification_fn>(
            GetProcAddress(ntdll, "LdrRegisterDllNotification"));
  const auto unregister_notification = ntdll == nullptr
      ? nullptr
      : reinterpret_cast<unregister_notification_fn>(
            GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
  if (register_notification == nullptr || unregister_notification == nullptr) {
    return result;
  }
  const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (event == nullptr) {
    return result;
  }
  void* cookie{};
  if (register_notification(0, &signal_module_change, event, &cookie) < 0) {
    CloseHandle(event);
    return result;
  }

  const unsigned long long deadline = GetTickCount64() + timeout_milliseconds;
  // Recheck after registration so a load between the first check and callback
  // installation becomes a duplicate wakeup rather than a missed module.
  for (;;) {
    result = install_loaded_import_hook();
    if (result != import_hook_result::client_not_loaded &&
        result != import_hook_result::cef_not_loaded) {
      break;
    }
    const auto now = GetTickCount64();
    if (now >= deadline) {
      break;
    }
    const auto remaining = deadline - now;
    if (WaitForSingleObject(
            event, static_cast<DWORD>(remaining > MAXDWORD ? MAXDWORD : remaining)) !=
        WAIT_OBJECT_0) {
      break;
    }
  }
  unregister_notification(cookie);
  CloseHandle(event);
  return result;
}

const char* describe(import_hook_result result) noexcept {
  switch (result) {
    case import_hook_result::installed: return "installed";
    case import_hook_result::already_installed: return "already_installed";
    case import_hook_result::client_not_loaded: return "client_not_loaded";
    case import_hook_result::cef_not_loaded: return "cef_not_loaded";
    case import_hook_result::api_mismatch: return "api_mismatch";
    case import_hook_result::export_missing: return "export_missing";
    case import_hook_result::import_missing: return "import_missing";
    case import_hook_result::invalid_image: return "invalid_image";
    case import_hook_result::write_failed: return "write_failed";
  }
  return "unknown";
}

}  // namespace ncm::cef_injection
