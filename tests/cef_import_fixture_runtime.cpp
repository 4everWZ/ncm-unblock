#include "ncm/cef/abi_1916.hpp"

#include <Windows.h>

#include <string_view>

namespace {

long g_registrations{};
long g_execute_calls{};
ncm::cef::cef_v8handler_t* g_retained_handler{};

}  // namespace

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
  // Mirror CEF 1916 DLL glue: Wrap always UnderlyingRelease()s a non-null
  // handler before CefRegisterExtension accepts or rejects the registration.
  if (handler != nullptr) {
    handler->base.release(&handler->base);
  }

  const auto valid =
      name != nullptr && code != nullptr && handler != nullptr &&
      handler->execute != nullptr &&
      std::wstring_view(name->str, name->length) == L"ncm/unblock/m3" &&
      std::wstring_view(code->str, code->length).find(L"ncmUnblock297Marker") !=
          std::wstring_view::npos &&
      std::wstring_view(code->str, code->length).find(L"native function") !=
          std::wstring_view::npos;

  if (!valid) {
    // Temporary RefPtr destruction after a rejected registration.
    if (handler != nullptr) {
      handler->base.release(&handler->base);
    }
    return 0;
  }

  // V8TrackObject retain stand-in for process lifetime.
  handler->base.add_ref(&handler->base);
  g_retained_handler = handler;
  InterlockedIncrement(&g_registrations);

  // Exercise Execute without a real V8 context so ownership + marker paths
  // are observable from the import-hook fixture.
  ncm::cef::cef_v8value_t* retval = nullptr;
  handler->execute(handler, nullptr, nullptr, 0, nullptr, &retval, nullptr);
  InterlockedIncrement(&g_execute_calls);
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

long __cdecl cef_fixture_handler_refct() {
  if (g_retained_handler == nullptr ||
      g_retained_handler->base.get_refct == nullptr) {
    return 0;
  }
  return g_retained_handler->base.get_refct(&g_retained_handler->base);
}

long __cdecl cef_fixture_execute_count() {
  return InterlockedCompareExchange(&g_execute_calls, 0, 0);
}

}  // extern "C"
