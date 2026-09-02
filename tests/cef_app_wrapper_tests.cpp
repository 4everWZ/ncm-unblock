#include "ncm/cef_injection/app_wrapper.hpp"

#include <Windows.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ncm;

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

struct fake_render {
  cef::cef_render_process_handler_t value{};
  long references{1};
  int thread_created{};
  int webkit_initialized{};
  int browser_created{};
  int browser_destroyed{};
  int load_handler{};
  int before_navigation{};
  int context_created{};
  int context_released{};
  int uncaught_exception{};
  int focused_node{};
  int process_message{};
};

struct fake_app {
  cef::cef_app_t value{};
  long references{1};
  fake_render* render{};
  int before_command_line{};
  int register_schemes{};
  int resource_handler{};
  int browser_handler{};
  int render_handler{};
};

long g_sequence{};
long g_original_webkit_sequence{};
long g_registration_sequence{};
int g_registration_calls{};

template <typename T>
[[nodiscard]] T* owner(cef::cef_base_t* self) {
  return reinterpret_cast<T*>(self);
}

int NCM_CEF_CALLBACK app_add_ref(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(&owner<fake_app>(self)->references));
}

int NCM_CEF_CALLBACK app_release(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedDecrement(&owner<fake_app>(self)->references));
}

int NCM_CEF_CALLBACK app_refct(cef::cef_base_t* self) {
  return static_cast<int>(owner<fake_app>(self)->references);
}

int NCM_CEF_CALLBACK render_add_ref(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(&owner<fake_render>(self)->references));
}

int NCM_CEF_CALLBACK render_release(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedDecrement(&owner<fake_render>(self)->references));
}

int NCM_CEF_CALLBACK render_refct(cef::cef_base_t* self) {
  return static_cast<int>(owner<fake_render>(self)->references);
}

void NCM_CEF_CALLBACK before_command_line(
    cef::cef_app_t* self, const cef::cef_string_t*, cef::cef_command_line_t*) {
  reinterpret_cast<fake_app*>(self)->before_command_line++;
}

void NCM_CEF_CALLBACK register_schemes(
    cef::cef_app_t* self, cef::cef_scheme_registrar_t*) {
  reinterpret_cast<fake_app*>(self)->register_schemes++;
}

cef::cef_resource_bundle_handler_t* NCM_CEF_CALLBACK resource_handler(
    cef::cef_app_t* self) {
  reinterpret_cast<fake_app*>(self)->resource_handler++;
  return reinterpret_cast<cef::cef_resource_bundle_handler_t*>(0x1010);
}

cef::cef_browser_process_handler_t* NCM_CEF_CALLBACK browser_handler(
    cef::cef_app_t* self) {
  reinterpret_cast<fake_app*>(self)->browser_handler++;
  return reinterpret_cast<cef::cef_browser_process_handler_t*>(0x2020);
}

cef::cef_render_process_handler_t* NCM_CEF_CALLBACK get_render_handler(
    cef::cef_app_t* self) {
  auto* app = reinterpret_cast<fake_app*>(self);
  app->render_handler++;
  app->render->value.base.add_ref(&app->render->value.base);
  return &app->render->value;
}

void NCM_CEF_CALLBACK thread_created(
    cef::cef_render_process_handler_t* self, cef::cef_list_value_t*) {
  reinterpret_cast<fake_render*>(self)->thread_created++;
}

void NCM_CEF_CALLBACK webkit_initialized(cef::cef_render_process_handler_t* self) {
  reinterpret_cast<fake_render*>(self)->webkit_initialized++;
  const auto ticket = InterlockedIncrement(&g_sequence);
  InterlockedCompareExchange(&g_original_webkit_sequence, ticket, 0);
}

void NCM_CEF_CALLBACK browser_created(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*) {
  reinterpret_cast<fake_render*>(self)->browser_created++;
}

void NCM_CEF_CALLBACK browser_destroyed(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*) {
  reinterpret_cast<fake_render*>(self)->browser_destroyed++;
}

cef::cef_load_handler_t* NCM_CEF_CALLBACK load_handler(
    cef::cef_render_process_handler_t* self) {
  reinterpret_cast<fake_render*>(self)->load_handler++;
  return reinterpret_cast<cef::cef_load_handler_t*>(0x3030);
}

int NCM_CEF_CALLBACK before_navigation(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*, cef::cef_frame_t*,
    cef::cef_request_t*, int, int) {
  reinterpret_cast<fake_render*>(self)->before_navigation++;
  return 37;
}

void NCM_CEF_CALLBACK context_created(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*, cef::cef_frame_t*,
    cef::cef_v8context_t*) {
  reinterpret_cast<fake_render*>(self)->context_created++;
}

void NCM_CEF_CALLBACK context_released(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*, cef::cef_frame_t*,
    cef::cef_v8context_t*) {
  reinterpret_cast<fake_render*>(self)->context_released++;
}

void NCM_CEF_CALLBACK uncaught_exception(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*, cef::cef_frame_t*,
    cef::cef_v8context_t*, cef::cef_v8exception_t*, cef::cef_v8stack_trace_t*) {
  reinterpret_cast<fake_render*>(self)->uncaught_exception++;
}

void NCM_CEF_CALLBACK focused_node(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*, cef::cef_frame_t*,
    cef::cef_domnode_t*) {
  reinterpret_cast<fake_render*>(self)->focused_node++;
}

int NCM_CEF_CALLBACK process_message(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t*, int,
    cef::cef_process_message_t*) {
  reinterpret_cast<fake_render*>(self)->process_message++;
  return 73;
}

int __cdecl register_extension(
    const cef::cef_string_t* name, const cef::cef_string_t* code,
    cef::cef_v8handler_t* handler) {
  g_registration_calls++;
  g_registration_sequence = InterlockedIncrement(&g_sequence);
  require(handler == nullptr, "M3 registration supplied a native handler");
  require(std::wstring_view(name->str, name->length) == L"ncm/unblock/m3",
          "M3 registration used the wrong extension name");
  const std::wstring_view source(code->str, code->length);
  require(source.find(L"__ncmUnblock297") != std::wstring_view::npos &&
              source.find(L"m3 = true") != std::wstring_view::npos,
          "M3 registration did not publish its observable marker");
  return 1;
}

int __cdecl reject_extension(
    const cef::cef_string_t*, const cef::cef_string_t*, cef::cef_v8handler_t*) {
  g_registration_calls++;
  return 0;
}

void initialize(fake_render& render, fake_app& app) {
  render.value.base = {
      sizeof(cef::cef_render_process_handler_t),
      &render_add_ref,
      &render_release,
      &render_refct,
  };
  render.value.on_render_thread_created = &thread_created;
  render.value.on_web_kit_initialized = &webkit_initialized;
  render.value.on_browser_created = &browser_created;
  render.value.on_browser_destroyed = &browser_destroyed;
  render.value.get_load_handler = &load_handler;
  render.value.on_before_navigation = &before_navigation;
  render.value.on_context_created = &context_created;
  render.value.on_context_released = &context_released;
  render.value.on_uncaught_exception = &uncaught_exception;
  render.value.on_focused_node_changed = &focused_node;
  render.value.on_process_message_received = &process_message;

  app.render = &render;
  app.value.base = {sizeof(cef::cef_app_t), &app_add_ref, &app_release, &app_refct};
  app.value.on_before_command_line_processing = &before_command_line;
  app.value.on_register_custom_schemes = &register_schemes;
  app.value.get_resource_bundle_handler = &resource_handler;
  app.value.get_browser_process_handler = &browser_handler;
  app.value.get_render_process_handler = &get_render_handler;
}

void test_rejects_an_unusable_application() {
  fake_app incomplete{};
  incomplete.value.base.size = sizeof(cef::cef_base_t);
  const auto result = cef_injection::wrap_application(&incomplete.value, &register_extension);
  require(!result.wrapped && result.application == &incomplete.value,
          "an undersized application was wrapped");
  require(cef_injection::current_registration_state() ==
              cef_injection::registration_state::not_attempted,
          "rejecting an application attempted extension registration");
}

void test_forwards_and_registers_once() {
  fake_render render{};
  fake_app app{};
  initialize(render, app);

  const auto result = cef_injection::wrap_application(&app.value, &register_extension);
  require(result.wrapped && result.application != &app.value,
          "a complete application was not wrapped");
  require(app.references == 2, "the wrapper did not retain the original application");

  auto* wrapped = result.application;
  wrapped->on_before_command_line_processing(wrapped, nullptr, nullptr);
  wrapped->on_register_custom_schemes(wrapped, nullptr);
  require(wrapped->get_resource_bundle_handler(wrapped) ==
              reinterpret_cast<cef::cef_resource_bundle_handler_t*>(0x1010),
          "the resource handler return value changed");
  require(wrapped->get_browser_process_handler(wrapped) ==
              reinterpret_cast<cef::cef_browser_process_handler_t*>(0x2020),
          "the browser handler return value changed");
  require(app.before_command_line == 1 && app.register_schemes == 1 &&
              app.resource_handler == 1 && app.browser_handler == 1,
          "an application callback was not forwarded to the original self");

  auto* wrapped_render = wrapped->get_render_process_handler(wrapped);
  require(wrapped_render != nullptr && wrapped_render != &render.value,
          "the render handler was not wrapped");
  require(render.references == 2, "the wrapper did not own the returned render reference");

  wrapped_render->on_render_thread_created(wrapped_render, nullptr);
  wrapped_render->on_web_kit_initialized(wrapped_render);
  wrapped_render->on_web_kit_initialized(wrapped_render);
  wrapped_render->on_browser_created(wrapped_render, nullptr);
  wrapped_render->on_browser_destroyed(wrapped_render, nullptr);
  require(wrapped_render->get_load_handler(wrapped_render) ==
              reinterpret_cast<cef::cef_load_handler_t*>(0x3030),
          "the load handler return value changed");
  require(wrapped_render->on_before_navigation(
              wrapped_render, nullptr, nullptr, nullptr, 0, 0) == 37,
          "the navigation result changed");
  wrapped_render->on_context_created(wrapped_render, nullptr, nullptr, nullptr);
  wrapped_render->on_context_released(wrapped_render, nullptr, nullptr, nullptr);
  wrapped_render->on_uncaught_exception(
      wrapped_render, nullptr, nullptr, nullptr, nullptr, nullptr);
  wrapped_render->on_focused_node_changed(wrapped_render, nullptr, nullptr, nullptr);
  require(wrapped_render->on_process_message_received(
              wrapped_render, nullptr, 0, nullptr) == 73,
          "the process-message result changed");

  require(render.thread_created == 1 && render.webkit_initialized == 2 &&
              render.browser_created == 1 && render.browser_destroyed == 1 &&
              render.load_handler == 1 && render.before_navigation == 1 &&
              render.context_created == 1 && render.context_released == 1 &&
              render.uncaught_exception == 1 && render.focused_node == 1 &&
              render.process_message == 1,
          "a render callback was not forwarded to the original self");
  require(g_registration_calls == 1, "the extension was registered more than once");
  require(g_original_webkit_sequence < g_registration_sequence,
          "registration ran before the original WebKit callback");
  require(cef_injection::current_registration_state() ==
              cef_injection::registration_state::succeeded,
          "successful registration was not observable");

  require(wrapped_render->base.get_refct(&wrapped_render->base) == 1,
          "the render wrapper started with the wrong reference count");
  require(wrapped_render->base.add_ref(&wrapped_render->base) == 2,
          "the render wrapper did not increment its reference count");
  require(wrapped_render->base.release(&wrapped_render->base) == 0,
          "a retained render wrapper reported destruction");
  require(wrapped_render->base.release(&wrapped_render->base) == 1,
          "the final render release did not destroy the wrapper");
  require(render.references == 1, "destroying the wrapper leaked the render reference");

  require(wrapped->base.release(&wrapped->base) == 1,
          "the initial application wrapper reference was not released");
  require(app.references == 1, "destroying the wrapper leaked the application reference");
}

void test_registration_failure_is_a_decline() {
  fake_render render{};
  fake_app app{};
  initialize(render, app);

  const auto result = cef_injection::wrap_application(&app.value, &reject_extension);
  require(result.wrapped, "a complete application was not wrapped for the failure case");
  auto* wrapped_render =
      result.application->get_render_process_handler(result.application);
  require(wrapped_render != nullptr, "the failure case returned no render wrapper");

  wrapped_render->on_web_kit_initialized(wrapped_render);
  wrapped_render->on_web_kit_initialized(wrapped_render);
  require(render.webkit_initialized == 2,
          "registration failure stopped the client's original callback");
  require(g_registration_calls == 1,
          "registration failure was retried inside the same process");
  require(cef_injection::current_registration_state() ==
              cef_injection::registration_state::failed,
          "registration failure was not published as a decline");

  wrapped_render->base.release(&wrapped_render->base);
  result.application->base.release(&result.application->base);
  require(render.references == 1 && app.references == 1,
          "the failure path leaked an original reference");
}

}  // namespace

int main(int argument_count, char** arguments) {
  try {
    require(argument_count == 2, "expected success or failure mode");
    test_rejects_an_unusable_application();
    if (std::string_view(arguments[1]) == "success") {
      test_forwards_and_registers_once();
    } else if (std::string_view(arguments[1]) == "failure") {
      test_registration_failure_is_a_decline();
    } else {
      throw std::runtime_error("unknown test mode");
    }
    std::cout << "CEF application wrapper tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
