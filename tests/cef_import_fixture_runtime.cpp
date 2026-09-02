#include "ncm/cef/abi_1916.hpp"

#include <Windows.h>

#include <string_view>

namespace {
long g_registrations{};
}

extern "C" {

const char* __cdecl cef_api_hash(int entry) {
#if defined(NCM_CEF_FIXTURE_API_MISMATCH)
  if (entry == 0) return "fixture-platform-mismatch";
#else
  if (entry == 0) return ncm::cef::pinned_api_hash_platform.data();
#endif
  if (entry == 1) return ncm::cef::pinned_api_hash_universal.data();
  return "";
}

int __cdecl cef_register_extension(
    const ncm::cef::cef_string_t* name,
    const ncm::cef::cef_string_t* code,
    ncm::cef::cef_v8handler_t* handler) {
  if (name == nullptr || code == nullptr || handler != nullptr ||
      std::wstring_view(name->str, name->length) != L"ncm/unblock/m3" ||
      std::wstring_view(code->str, code->length).find(L"__ncmUnblock297") ==
          std::wstring_view::npos) {
    return 0;
  }
  InterlockedIncrement(&g_registrations);
  return 1;
}

int __cdecl cef_execute_process(
    const ncm::cef::cef_main_args_t*, ncm::cef::cef_app_t* application, void*) {
  if (application == nullptr || application->get_render_process_handler == nullptr) {
    return 17;
  }
  auto* handler = application->get_render_process_handler(application);
  if (handler != nullptr) {
    if (handler->on_web_kit_initialized != nullptr) {
      handler->on_web_kit_initialized(handler);
    }
    handler->base.release(&handler->base);
  }
  return 29;
}

long __cdecl cef_fixture_registration_count() {
  return InterlockedCompareExchange(&g_registrations, 0, 0);
}

}  // extern "C"
