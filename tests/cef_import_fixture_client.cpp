#include "ncm/cef/abi_1916.hpp"

extern "C" __declspec(dllimport) int __cdecl cef_execute_process(
    const ncm::cef::cef_main_args_t*, ncm::cef::cef_app_t*, void*);

extern "C" int __cdecl run_cef_fixture(ncm::cef::cef_app_t* application) {
  return cef_execute_process(nullptr, application, nullptr);
}
