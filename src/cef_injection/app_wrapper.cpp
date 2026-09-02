#include "ncm/cef_injection/app_wrapper.hpp"

#include <Windows.h>

#include <cstddef>
#include <new>

namespace ncm::cef_injection {
namespace {

struct app_wrapper;
struct render_wrapper;

long g_registration_state{static_cast<long>(registration_state::not_attempted)};

constexpr wchar_t extension_name_text[] = L"ncm/unblock/m3";
constexpr wchar_t extension_code_text[] =
    L"if (!this.__ncmUnblock297) this.__ncmUnblock297 = {};"
    L"this.__ncmUnblock297.m3 = true;";

[[nodiscard]] cef::cef_string_t static_string(
    const wchar_t* value, std::size_t length) noexcept {
  return {const_cast<wchar_t*>(value), length, nullptr};
}

struct app_wrapper {
  cef::cef_app_t value{};
  long references{1};
  cef::cef_app_t* original{};
  register_extension_fn register_extension{};
};

struct render_wrapper {
  cef::cef_render_process_handler_t value{};
  long references{1};
  // Owns the reference returned by the original application's handler getter.
  cef::cef_render_process_handler_t* original{};
  register_extension_fn register_extension{};
};

static_assert(offsetof(app_wrapper, value) == 0);
static_assert(offsetof(render_wrapper, value) == 0);

[[nodiscard]] app_wrapper* app_from(cef::cef_base_t* base) noexcept {
  return reinterpret_cast<app_wrapper*>(base);
}

[[nodiscard]] app_wrapper* app_from(cef::cef_app_t* value) noexcept {
  return reinterpret_cast<app_wrapper*>(value);
}

[[nodiscard]] render_wrapper* render_from(cef::cef_base_t* base) noexcept {
  return reinterpret_cast<render_wrapper*>(base);
}

[[nodiscard]] render_wrapper* render_from(
    cef::cef_render_process_handler_t* value) noexcept {
  return reinterpret_cast<render_wrapper*>(value);
}

int NCM_CEF_CALLBACK app_add_ref(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(&app_from(self)->references));
}

int NCM_CEF_CALLBACK app_release(cef::cef_base_t* self) {
  auto* wrapper = app_from(self);
  if (InterlockedDecrement(&wrapper->references) != 0) {
    return 0;
  }
  auto* original = wrapper->original;
  delete wrapper;
  original->base.release(&original->base);
  return 1;
}

int NCM_CEF_CALLBACK app_get_refct(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedCompareExchange(&app_from(self)->references, 0, 0));
}

void NCM_CEF_CALLBACK app_before_command_line(
    cef::cef_app_t* self, const cef::cef_string_t* process_type,
    cef::cef_command_line_t* command_line) {
  auto* original = app_from(self)->original;
  if (original->on_before_command_line_processing != nullptr) {
    original->on_before_command_line_processing(original, process_type, command_line);
  }
}

void NCM_CEF_CALLBACK app_register_schemes(
    cef::cef_app_t* self, cef::cef_scheme_registrar_t* registrar) {
  auto* original = app_from(self)->original;
  if (original->on_register_custom_schemes != nullptr) {
    original->on_register_custom_schemes(original, registrar);
  }
}

cef::cef_resource_bundle_handler_t* NCM_CEF_CALLBACK app_resource_handler(
    cef::cef_app_t* self) {
  auto* original = app_from(self)->original;
  return original->get_resource_bundle_handler == nullptr
      ? nullptr
      : original->get_resource_bundle_handler(original);
}

cef::cef_browser_process_handler_t* NCM_CEF_CALLBACK app_browser_handler(
    cef::cef_app_t* self) {
  auto* original = app_from(self)->original;
  return original->get_browser_process_handler == nullptr
      ? nullptr
      : original->get_browser_process_handler(original);
}

int NCM_CEF_CALLBACK render_add_ref(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(&render_from(self)->references));
}

int NCM_CEF_CALLBACK render_release(cef::cef_base_t* self) {
  auto* wrapper = render_from(self);
  if (InterlockedDecrement(&wrapper->references) != 0) {
    return 0;
  }
  auto* original = wrapper->original;
  delete wrapper;
  original->base.release(&original->base);
  return 1;
}

int NCM_CEF_CALLBACK render_get_refct(cef::cef_base_t* self) {
  return static_cast<int>(
      InterlockedCompareExchange(&render_from(self)->references, 0, 0));
}

void NCM_CEF_CALLBACK render_thread_created(
    cef::cef_render_process_handler_t* self, cef::cef_list_value_t* extra_info) {
  auto* original = render_from(self)->original;
  if (original->on_render_thread_created != nullptr) {
    original->on_render_thread_created(original, extra_info);
  }
}

void NCM_CEF_CALLBACK render_webkit_initialized(
    cef::cef_render_process_handler_t* self) {
  auto* wrapper = render_from(self);
  auto* original = wrapper->original;
  if (original->on_web_kit_initialized != nullptr) {
    original->on_web_kit_initialized(original);
  }

  if (InterlockedCompareExchange(
          &g_registration_state, static_cast<long>(registration_state::failed),
          static_cast<long>(registration_state::not_attempted)) !=
      static_cast<long>(registration_state::not_attempted)) {
    return;
  }

  const auto name = static_string(extension_name_text, std::size(extension_name_text) - 1);
  const auto code = static_string(extension_code_text, std::size(extension_code_text) - 1);
  const auto succeeded = wrapper->register_extension(&name, &code, nullptr) != 0;
  InterlockedExchange(
      &g_registration_state,
      static_cast<long>(succeeded ? registration_state::succeeded
                                  : registration_state::failed));
}

void NCM_CEF_CALLBACK render_browser_created(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser) {
  auto* original = render_from(self)->original;
  if (original->on_browser_created != nullptr) {
    original->on_browser_created(original, browser);
  }
}

void NCM_CEF_CALLBACK render_browser_destroyed(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser) {
  auto* original = render_from(self)->original;
  if (original->on_browser_destroyed != nullptr) {
    original->on_browser_destroyed(original, browser);
  }
}

cef::cef_load_handler_t* NCM_CEF_CALLBACK render_load_handler(
    cef::cef_render_process_handler_t* self) {
  auto* original = render_from(self)->original;
  return original->get_load_handler == nullptr ? nullptr
                                                : original->get_load_handler(original);
}

int NCM_CEF_CALLBACK render_before_navigation(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser,
    cef::cef_frame_t* frame, cef::cef_request_t* request, int navigation_type,
    int is_redirect) {
  auto* original = render_from(self)->original;
  return original->on_before_navigation == nullptr
      ? 0
      : original->on_before_navigation(
            original, browser, frame, request, navigation_type, is_redirect);
}

void NCM_CEF_CALLBACK render_context_created(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser,
    cef::cef_frame_t* frame, cef::cef_v8context_t* context) {
  auto* original = render_from(self)->original;
  if (original->on_context_created != nullptr) {
    original->on_context_created(original, browser, frame, context);
  }
}

void NCM_CEF_CALLBACK render_context_released(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser,
    cef::cef_frame_t* frame, cef::cef_v8context_t* context) {
  auto* original = render_from(self)->original;
  if (original->on_context_released != nullptr) {
    original->on_context_released(original, browser, frame, context);
  }
}

void NCM_CEF_CALLBACK render_uncaught_exception(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser,
    cef::cef_frame_t* frame, cef::cef_v8context_t* context,
    cef::cef_v8exception_t* exception, cef::cef_v8stack_trace_t* stack_trace) {
  auto* original = render_from(self)->original;
  if (original->on_uncaught_exception != nullptr) {
    original->on_uncaught_exception(
        original, browser, frame, context, exception, stack_trace);
  }
}

void NCM_CEF_CALLBACK render_focused_node_changed(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser,
    cef::cef_frame_t* frame, cef::cef_domnode_t* node) {
  auto* original = render_from(self)->original;
  if (original->on_focused_node_changed != nullptr) {
    original->on_focused_node_changed(original, browser, frame, node);
  }
}

int NCM_CEF_CALLBACK render_process_message(
    cef::cef_render_process_handler_t* self, cef::cef_browser_t* browser,
    int source_process, cef::cef_process_message_t* message) {
  auto* original = render_from(self)->original;
  return original->on_process_message_received == nullptr
      ? 0
      : original->on_process_message_received(
            original, browser, source_process, message);
}

[[nodiscard]] cef::cef_render_process_handler_t* wrap_render_handler(
    cef::cef_render_process_handler_t* original,
    register_extension_fn register_extension) noexcept {
  if (original == nullptr) {
    return nullptr;
  }
  if (original->base.size < sizeof(cef::cef_render_process_handler_t) ||
      original->base.release == nullptr) {
    return original;
  }

  auto* wrapper = new (std::nothrow) render_wrapper{};
  if (wrapper == nullptr) {
    return original;
  }
  wrapper->original = original;
  wrapper->register_extension = register_extension;
  wrapper->value.base = {
      sizeof(cef::cef_render_process_handler_t),
      &render_add_ref,
      &render_release,
      &render_get_refct,
  };
  wrapper->value.on_render_thread_created = &render_thread_created;
  wrapper->value.on_web_kit_initialized = &render_webkit_initialized;
  wrapper->value.on_browser_created = &render_browser_created;
  wrapper->value.on_browser_destroyed = &render_browser_destroyed;
  wrapper->value.get_load_handler = &render_load_handler;
  wrapper->value.on_before_navigation = &render_before_navigation;
  wrapper->value.on_context_created = &render_context_created;
  wrapper->value.on_context_released = &render_context_released;
  wrapper->value.on_uncaught_exception = &render_uncaught_exception;
  wrapper->value.on_focused_node_changed = &render_focused_node_changed;
  wrapper->value.on_process_message_received = &render_process_message;
  return &wrapper->value;
}

cef::cef_render_process_handler_t* NCM_CEF_CALLBACK app_render_handler(
    cef::cef_app_t* self) {
  auto* wrapper = app_from(self);
  auto* original = wrapper->original;
  if (original->get_render_process_handler == nullptr) {
    return nullptr;
  }
  return wrap_render_handler(
      original->get_render_process_handler(original), wrapper->register_extension);
}

}  // namespace

wrapped_application wrap_application(
    cef::cef_app_t* original, register_extension_fn register_extension) noexcept {
  if (original == nullptr || register_extension == nullptr ||
      original->base.size < sizeof(cef::cef_app_t) ||
      original->base.add_ref == nullptr || original->base.release == nullptr ||
      original->get_render_process_handler == nullptr) {
    return {original, false};
  }

  auto* wrapper = new (std::nothrow) app_wrapper{};
  if (wrapper == nullptr) {
    return {original, false};
  }
  original->base.add_ref(&original->base);
  wrapper->original = original;
  wrapper->register_extension = register_extension;
  wrapper->value.base = {
      sizeof(cef::cef_app_t), &app_add_ref, &app_release, &app_get_refct};
  wrapper->value.on_before_command_line_processing = &app_before_command_line;
  wrapper->value.on_register_custom_schemes = &app_register_schemes;
  wrapper->value.get_resource_bundle_handler = &app_resource_handler;
  wrapper->value.get_browser_process_handler = &app_browser_handler;
  wrapper->value.get_render_process_handler = &app_render_handler;
  return {&wrapper->value, true};
}

registration_state current_registration_state() noexcept {
  return static_cast<registration_state>(
      InterlockedCompareExchange(&g_registration_state, 0, 0));
}

}  // namespace ncm::cef_injection
