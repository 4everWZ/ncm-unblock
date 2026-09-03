#include "ncm/cef_injection/app_wrapper.hpp"

#include <Windows.h>

#include <cstddef>
#include <cwchar>
#include <new>

namespace ncm::cef_injection {
namespace {

struct app_wrapper;
struct render_wrapper;
struct v8_handler_wrapper;

long g_registration_state{static_cast<long>(registration_state::not_attempted)};
long g_marker_count{};
long g_anchors_state{static_cast<long>(anchors_state::pending)};
long g_intercept_count{};

constexpr wchar_t extension_name_text[] = L"ncm/unblock/m3";
// Extension name stays `ncm/unblock/m3` for fixture stability; the IIFE now
// carries M3 marker clearance plus M4 deferred NEJ observation.
// `native function` is scoped to the surrounding function; the IIFE both
// declares and invokes natives when the extension is applied to a V8 context.
// Timer-free M4 observation. Live web.pack evidence shows player-URL posts go
// through NEJ.P("nm.x") helpers (bc.hb -> nej.j/cq.he), not nej.ut.j class
// constructors. Wrapping nej.ut.j correlated with STATUS_HEAP_CORRUPTION; this
// path only arms NEJ.P and path-filters functions on the nm.x module.
constexpr wchar_t extension_code_text[] =
    L"(function(){"
    L"native function ncmUnblock297Marker();"
    L"native function ncmUnblock297AnchorsFound();"
    L"native function ncmUnblock297AnchorsMissing();"
    L"native function ncmUnblock297Intercept();"
    L"ncmUnblock297Marker();"
    L"var PATH='/api/song/enhance/player/url';"
    L"var NS='nm.x';"
    L"var installed=0;"
    L"function wrapCb(cb){"
    L"return function(bm){"
    L"try{"
    L"if(bm&&(typeof bm.code!=='undefined'||"
    L"(bm.data&&bm.data[0]&&typeof bm.data[0].code!=='undefined')))"
    L"ncmUnblock297Intercept();"
    L"}catch(e){}"
    L"return cb.apply(this,arguments);"
    L"};"
    L"}"
    L"function wrapFn(fn){"
    L"if(typeof fn!=='function'||fn.__ncmM4)return fn;"
    L"var w=function(){"
    L"var a0=arguments[0];"
    L"if(typeof a0==='string'&&a0.indexOf(PATH)!==-1){"
    L"var args=[],i,cbIdx=-1;"
    L"for(i=0;i<arguments.length;i++)args[i]=arguments[i];"
    L"for(i=args.length-1;i>=1;i--){"
    L"if(typeof args[i]==='function'){cbIdx=i;break;}"
    L"}"
    L"if(cbIdx>=0)args[cbIdx]=wrapCb(args[cbIdx]);"
    L"return fn.apply(this,args);"
    L"}"
    L"return fn.apply(this,arguments);"
    L"};"
    L"w.__ncmM4=1;"
    L"return w;"
    L"}"
    L"function install(mod){"
    L"if(!mod||typeof mod!=='object')return;"
    L"var k,fn,n=0;"
    L"for(k in mod){"
    L"try{"
    L"fn=mod[k];"
    L"if(typeof fn!=='function'||fn.__ncmM4)continue;"
    L"mod[k]=wrapFn(fn);"
    L"n++;"
    L"}catch(e){}"
    L"}"
    L"if(n>0&&installed===0){"
    L"installed=1;"
    L"try{ncmUnblock297AnchorsFound();}catch(e){}"
    L"}"
    L"}"
    L"function wrapP(nej){"
    L"var orig=nej.P;"
    L"if(typeof orig!=='function'||orig.__ncmM4)return;"
    L"var wrapped=function(name){"
    L"var mod=orig.apply(this,arguments);"
    L"if(name===NS)install(mod);"
    L"return mod;"
    L"};"
    L"wrapped.__ncmM4=1;"
    L"try{nej.P=wrapped;}catch(e){}"
    L"}"
    L"function arm(nej){"
    L"if(!nej||typeof nej!=='object')return;"
    L"if(typeof nej.P==='function'){wrapP(nej);return;}"
    L"try{"
    L"var rawP;"
    L"Object.defineProperty(nej,'P',{"
    L"configurable:true,enumerable:true,"
    L"get:function(){return rawP;},"
    L"set:function(v){"
    L"rawP=v;"
    L"try{"
    L"delete nej.P;"
    L"}catch(e){}"
    L"try{nej.P=v;}catch(e){rawP=v;}"
    L"wrapP(nej);"
    L"}"
    L"});"
    L"}catch(e){}"
    L"}"
    L"try{"
    L"var root=this;"
    L"if(root.NEJ&&typeof root.NEJ==='object'){"
    L"arm(root.NEJ);"
    L"}else{"
    L"var rawNEJ;"
    L"Object.defineProperty(root,'NEJ',{"
    L"configurable:true,enumerable:true,"
    L"get:function(){return rawNEJ;},"
    L"set:function(v){"
    L"rawNEJ=v;"
    L"try{delete root.NEJ;}catch(e){}"
    L"try{root.NEJ=v;}catch(e){rawNEJ=v;}"
    L"arm(v);"
    L"}"
    L"});"
    L"}"
    L"}catch(e){}"
    L"})();";

using create_null_fn = cef::cef_v8value_t*(__cdecl*)();

[[nodiscard]] cef::cef_string_t static_string(
    const wchar_t* value, std::size_t length) noexcept {
  return {const_cast<wchar_t*>(value), length, nullptr};
}

[[nodiscard]] bool name_equals(
    const cef::cef_string_t* name, const wchar_t* expected) noexcept {
  if (name == nullptr || name->str == nullptr || expected == nullptr) {
    return false;
  }
  const auto expected_length = std::wcslen(expected);
  return name->length == expected_length &&
      std::wcsncmp(name->str, expected, expected_length) == 0;
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

struct v8_handler_wrapper {
  cef::cef_v8handler_t value{};
  long references{1};
};

static_assert(offsetof(app_wrapper, value) == 0);
static_assert(offsetof(render_wrapper, value) == 0);
static_assert(offsetof(v8_handler_wrapper, value) == 0);

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

[[nodiscard]] v8_handler_wrapper* handler_from(cef::cef_base_t* base) noexcept {
  return reinterpret_cast<v8_handler_wrapper*>(base);
}

[[nodiscard]] v8_handler_wrapper* handler_from(
    cef::cef_v8handler_t* value) noexcept {
  return reinterpret_cast<v8_handler_wrapper*>(value);
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

int NCM_CEF_CALLBACK handler_add_ref(cef::cef_base_t* self) {
  return static_cast<int>(InterlockedIncrement(&handler_from(self)->references));
}

int NCM_CEF_CALLBACK handler_release(cef::cef_base_t* self) {
  auto* wrapper = handler_from(self);
  if (InterlockedDecrement(&wrapper->references) != 0) {
    return 0;
  }
  delete wrapper;
  return 1;
}

int NCM_CEF_CALLBACK handler_get_refct(cef::cef_base_t* self) {
  return static_cast<int>(
      InterlockedCompareExchange(&handler_from(self)->references, 0, 0));
}

[[nodiscard]] cef::cef_v8value_t* try_create_null_value() noexcept {
  const HMODULE runtime = GetModuleHandleW(L"libcef.dll");
  if (runtime == nullptr) {
    return nullptr;
  }
  const auto create_null = reinterpret_cast<create_null_fn>(
      GetProcAddress(runtime, "cef_v8value_create_null"));
  return create_null == nullptr ? nullptr : create_null();
}

void dispatch_native(const cef::cef_string_t* name) noexcept {
  // Fixtures historically invoke execute with a null name for the marker path.
  if (name == nullptr || name->str == nullptr ||
      name_equals(name, L"ncmUnblock297Marker")) {
    InterlockedIncrement(&g_marker_count);
    return;
  }
  if (name_equals(name, L"ncmUnblock297AnchorsFound")) {
    InterlockedExchange(
        &g_anchors_state, static_cast<long>(anchors_state::found));
    return;
  }
  if (name_equals(name, L"ncmUnblock297AnchorsMissing")) {
    InterlockedCompareExchange(
        &g_anchors_state, static_cast<long>(anchors_state::missing),
        static_cast<long>(anchors_state::pending));
    return;
  }
  if (name_equals(name, L"ncmUnblock297Intercept")) {
    InterlockedIncrement(&g_intercept_count);
  }
}

int NCM_CEF_CALLBACK handler_execute(
    cef::cef_v8handler_t* self, const cef::cef_string_t* name,
    cef::cef_v8value_t*, std::size_t, cef::cef_v8value_t* const*,
    cef::cef_v8value_t** retval, cef::cef_string_t*) {
  (void)self;
  dispatch_native(name);
  if (retval != nullptr) {
    *retval = try_create_null_value();
  }
  return 1;
}

[[nodiscard]] cef::cef_v8handler_t* create_marker_handler() noexcept {
  auto* wrapper = new (std::nothrow) v8_handler_wrapper{};
  if (wrapper == nullptr) {
    return nullptr;
  }
  wrapper->value.base = {
      sizeof(cef::cef_v8handler_t),
      &handler_add_ref,
      &handler_release,
      &handler_get_refct,
  };
  wrapper->value.execute = &handler_execute;
  return &wrapper->value;
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

  auto* handler = create_marker_handler();
  if (handler == nullptr) {
    InterlockedExchange(
        &g_registration_state, static_cast<long>(registration_state::failed));
    return;
  }

  // CEF 1916 Wrap consumes one underlying reference. Start at 1, add the
  // transfer ref, then never release after register_extension returns.
  handler->base.add_ref(&handler->base);
  const auto name = static_string(extension_name_text, std::size(extension_name_text) - 1);
  const auto code = static_string(extension_code_text, std::size(extension_code_text) - 1);
  const auto succeeded = wrapper->register_extension(&name, &code, handler) != 0;
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

long current_marker_count() noexcept {
  return InterlockedCompareExchange(&g_marker_count, 0, 0);
}

anchors_state current_anchors_state() noexcept {
  return static_cast<anchors_state>(
      InterlockedCompareExchange(&g_anchors_state, 0, 0));
}

long current_intercept_count() noexcept {
  return InterlockedCompareExchange(&g_intercept_count, 0, 0);
}

}  // namespace ncm::cef_injection
