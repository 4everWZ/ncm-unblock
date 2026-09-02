#include "ncm/cef_injection/import_hook.hpp"
#include "ncm/cef_injection/app_wrapper.hpp"

#include "ncm/cef/abi_1916.hpp"

#include <Windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

struct render_fixture {
  ncm::cef::cef_render_process_handler_t value{};
  long references{1};
  long webkit_calls{};
};

struct app_fixture {
  ncm::cef::cef_app_t value{};
  long references{1};
  render_fixture* render{};
};

int NCM_CEF_CALLBACK add_app_ref(ncm::cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(
      &reinterpret_cast<app_fixture*>(self)->references));
}
int NCM_CEF_CALLBACK release_app(ncm::cef::cef_base_t* self) {
  return static_cast<int>(InterlockedDecrement(
      &reinterpret_cast<app_fixture*>(self)->references));
}
int NCM_CEF_CALLBACK app_refct(ncm::cef::cef_base_t* self) {
  return static_cast<int>(reinterpret_cast<app_fixture*>(self)->references);
}
int NCM_CEF_CALLBACK add_render_ref(ncm::cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(
      &reinterpret_cast<render_fixture*>(self)->references));
}
int NCM_CEF_CALLBACK release_render(ncm::cef::cef_base_t* self) {
  return static_cast<int>(InterlockedDecrement(
      &reinterpret_cast<render_fixture*>(self)->references));
}
int NCM_CEF_CALLBACK render_refct(ncm::cef::cef_base_t* self) {
  return static_cast<int>(reinterpret_cast<render_fixture*>(self)->references);
}
void NCM_CEF_CALLBACK on_webkit(ncm::cef::cef_render_process_handler_t* self) {
  InterlockedIncrement(&reinterpret_cast<render_fixture*>(self)->webkit_calls);
}
ncm::cef::cef_render_process_handler_t* NCM_CEF_CALLBACK get_render(
    ncm::cef::cef_app_t* self) {
  auto* app = reinterpret_cast<app_fixture*>(self);
  app->render->value.base.add_ref(&app->render->value.base);
  return &app->render->value;
}

void initialize(app_fixture& app, render_fixture& render) {
  render.value.base = {
      sizeof(render.value), &add_render_ref, &release_render, &render_refct};
  render.value.on_web_kit_initialized = &on_webkit;
  app.render = &render;
  app.value.base = {sizeof(app.value), &add_app_ref, &release_app, &app_refct};
  app.value.get_render_process_handler = &get_render;
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 4, "mode, runtime, and client paths are required");
    const std::wstring_view mode(arguments[1]);
    const HMODULE runtime = LoadLibraryW(std::filesystem::canonical(arguments[2]).c_str());
    require(runtime != nullptr, "unable to load the synthetic CEF runtime");
    const HMODULE client = LoadLibraryW(std::filesystem::canonical(arguments[3]).c_str());
    require(client != nullptr, "unable to load the synthetic client");

    using run_fn = int(__cdecl*)(ncm::cef::cef_app_t*);
    using count_fn = long(__cdecl*)();
    const auto run = reinterpret_cast<run_fn>(GetProcAddress(client, "run_cef_fixture"));
    const auto count = reinterpret_cast<count_fn>(
        GetProcAddress(runtime, "cef_fixture_registration_count"));
    const auto handler_refct = reinterpret_cast<count_fn>(
        GetProcAddress(runtime, "cef_fixture_handler_refct"));
    const auto execute_count = reinterpret_cast<count_fn>(
        GetProcAddress(runtime, "cef_fixture_execute_count"));
    require(run != nullptr && count != nullptr && handler_refct != nullptr &&
                execute_count != nullptr,
            "a fixture export is missing");

    app_fixture app{};
    render_fixture render{};
    initialize(app, render);
    const auto result = ncm::cef_injection::install_import_hook(client, runtime);
    if (mode == L"mismatch") {
      require(result == ncm::cef_injection::import_hook_result::api_mismatch,
              "a mismatched CEF API was not rejected");
      require(run(&app.value) == 29 && count() == 0 && render.webkit_calls == 1,
              "the mismatch decline changed the original client call");
    } else {
      require(mode == L"success", "unknown test mode");
      require(result == ncm::cef_injection::import_hook_result::installed,
              std::string("hook result: ") + ncm::cef_injection::describe(result));
      require(run(&app.value) == 29, "the intercepted return value changed");
      require(count() == 1 && render.webkit_calls == 1,
              "the intercepted call did not register after the original callback");
      require(handler_refct() >= 1,
              "the intercepted registration destroyed the native handler");
      require(execute_count() >= 1 &&
                  ncm::cef_injection::current_marker_count() > 0,
              "the intercepted registration did not execute the native marker");
      require(app.references == 1 && render.references == 1,
              "the intercepted call leaked a fixture reference");
    }
    std::cout << "CEF import hook tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
