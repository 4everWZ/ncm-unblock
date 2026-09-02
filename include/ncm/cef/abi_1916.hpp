#pragma once

// Pinned CEF C ABI for the exact browser runtime NCM 2.9.7.199711 ships.
//
// Provenance
// ----------
// Transcribed from the authoritative upstream CEF repository, branch `1916`:
//
//   include/internal/cef_export.h        CEF_CALLBACK
//   include/internal/cef_types.h         char16
//   include/internal/cef_string_types.h  cef_string_utf16_t
//   include/capi/cef_base_capi.h         cef_base_t
//   include/capi/cef_app_capi.h          cef_app_t
//   include/capi/cef_render_process_handler_capi.h
//                                        cef_render_process_handler_t
//
// CEF is Copyright (c) 2014 Marshall A. Greenblatt and is distributed under a
// three-clause BSD licence. These declarations are an interface description, so
// the upstream notice is reproduced with the project's third-party notices
// rather than inline in every file.
//
// Why a transcription and not a vendored tree
// -------------------------------------------
// The transitive include closure of `cef_app_capi.h` is most of the upstream
// `include/` tree, so there is no meaningful subset to vendor; it would be all
// or nothing. This project touches exactly four concrete structures and passes
// every other CEF object through as an opaque pointer it never dereferences.
// Declaring only what is dereferenced keeps the third-party surface to what the
// code actually depends on.
//
// Why this is safe to pin
// -----------------------
// `ncm_cef_probe` reads the API hashes the client's own module reports, and
// they match the published CEF 3.1916 pair exactly. Those hashes identify the C
// API surface, so the layouts below describe this module. The client's build is
// a later revision on the same branch (1900 over Chromium 35.0.1916.157 against
// the published 1721/1781), which the identical hashes establish as an
// unchanged surface rather than an assumption.
//
// Anything that relies on these layouts must verify `pinned_api_hash_platform`
// and `pinned_api_hash_universal` against the loaded module at startup and
// decline rather than proceed on a mismatch.

#include <cstddef>
#include <string_view>

namespace ncm::cef {

// The API hash pair this header's layouts belong to.
inline constexpr std::string_view pinned_api_hash_platform =
    "78d4b4eb20e36e2b08572b98645dde08e987fbad";
inline constexpr std::string_view pinned_api_hash_universal =
    "ce45d134468cd9bad310409c96e5108d75fac3c7";

// Structure member functions are `__stdcall`. The module's exported free
// functions are `__cdecl`, which is a separate contract and is measured rather
// than assumed; see `ncm::cef_probe::argument_cleanup`.
#define NCM_CEF_CALLBACK __stdcall

// `char16` is `wchar_t` where it is two bytes wide, which is every Windows
// target this project supports.
static_assert(sizeof(wchar_t) == 2, "CEF's char16 is not wchar_t on this target");
using cef_char16 = wchar_t;

// Objects this project forwards without ever dereferencing. Declaring them
// incomplete is deliberate: an accidental member access becomes a compile
// error instead of a layout assumption.
struct cef_command_line_t;
struct cef_main_args_t;
struct cef_scheme_registrar_t;
struct cef_resource_bundle_handler_t;
struct cef_browser_process_handler_t;
struct cef_browser_t;
struct cef_frame_t;
struct cef_request_t;
struct cef_list_value_t;
struct cef_load_handler_t;
struct cef_v8context_t;
struct cef_v8exception_t;
struct cef_v8stack_trace_t;
struct cef_domnode_t;
struct cef_process_message_t;
struct cef_v8handler_t;

struct cef_string_utf16_t {
  cef_char16* str;
  std::size_t length;
  void (*dtor)(cef_char16* str);
};

// The default string type for this build is UTF-16.
using cef_string_t = cef_string_utf16_t;

// Reference counting. This branch carries three functions; later CEF versions
// add `has_one_ref`, which is why a layout may not be taken from a newer
// header set.
struct cef_base_t {
  std::size_t size;
  int(NCM_CEF_CALLBACK* add_ref)(cef_base_t* self);
  int(NCM_CEF_CALLBACK* release)(cef_base_t* self);
  int(NCM_CEF_CALLBACK* get_refct)(cef_base_t* self);
};

struct cef_render_process_handler_t;

struct cef_app_t {
  cef_base_t base;
  void(NCM_CEF_CALLBACK* on_before_command_line_processing)(
      cef_app_t* self, const cef_string_t* process_type,
      cef_command_line_t* command_line);
  void(NCM_CEF_CALLBACK* on_register_custom_schemes)(
      cef_app_t* self, cef_scheme_registrar_t* registrar);
  cef_resource_bundle_handler_t*(NCM_CEF_CALLBACK* get_resource_bundle_handler)(
      cef_app_t* self);
  cef_browser_process_handler_t*(NCM_CEF_CALLBACK* get_browser_process_handler)(
      cef_app_t* self);
  cef_render_process_handler_t*(NCM_CEF_CALLBACK* get_render_process_handler)(
      cef_app_t* self);
};

// `on_web_kit_initialized` is where an extension may be registered, and
// `on_context_created` is where a per-context script may run. Both are
// available on this branch; which one the shim uses is an M3 outcome, not a
// decision this header makes.
struct cef_render_process_handler_t {
  cef_base_t base;
  void(NCM_CEF_CALLBACK* on_render_thread_created)(
      cef_render_process_handler_t* self, cef_list_value_t* extra_info);
  void(NCM_CEF_CALLBACK* on_web_kit_initialized)(cef_render_process_handler_t* self);
  void(NCM_CEF_CALLBACK* on_browser_created)(
      cef_render_process_handler_t* self, cef_browser_t* browser);
  void(NCM_CEF_CALLBACK* on_browser_destroyed)(
      cef_render_process_handler_t* self, cef_browser_t* browser);
  cef_load_handler_t*(NCM_CEF_CALLBACK* get_load_handler)(
      cef_render_process_handler_t* self);
  int(NCM_CEF_CALLBACK* on_before_navigation)(
      cef_render_process_handler_t* self, cef_browser_t* browser, cef_frame_t* frame,
      cef_request_t* request, int navigation_type, int is_redirect);
  void(NCM_CEF_CALLBACK* on_context_created)(
      cef_render_process_handler_t* self, cef_browser_t* browser, cef_frame_t* frame,
      cef_v8context_t* context);
  void(NCM_CEF_CALLBACK* on_context_released)(
      cef_render_process_handler_t* self, cef_browser_t* browser, cef_frame_t* frame,
      cef_v8context_t* context);
  void(NCM_CEF_CALLBACK* on_uncaught_exception)(
      cef_render_process_handler_t* self, cef_browser_t* browser, cef_frame_t* frame,
      cef_v8context_t* context, cef_v8exception_t* exception,
      cef_v8stack_trace_t* stack_trace);
  void(NCM_CEF_CALLBACK* on_focused_node_changed)(
      cef_render_process_handler_t* self, cef_browser_t* browser, cef_frame_t* frame,
      cef_domnode_t* node);
  int(NCM_CEF_CALLBACK* on_process_message_received)(
      cef_render_process_handler_t* self, cef_browser_t* browser, int source_process,
      cef_process_message_t* message);
};

// Layout guards for the Win32 target. A transcription slip that drops or adds a
// member changes the size, and CEF itself rejects a structure whose `size`
// field does not match what it expects, so these are the first line of defence
// against a wrong member count.
static_assert(sizeof(void*) == 4, "these sizes describe the Win32 target");
static_assert(sizeof(cef_string_utf16_t) == 12, "cef_string_utf16_t is three words");
static_assert(sizeof(cef_base_t) == 16, "cef_base_t is a size field and three functions");
static_assert(sizeof(cef_app_t) == sizeof(cef_base_t) + 5 * sizeof(void*),
              "cef_app_t carries five members on branch 1916");
static_assert(sizeof(cef_render_process_handler_t) == sizeof(cef_base_t) + 11 * sizeof(void*),
              "cef_render_process_handler_t carries eleven members on branch 1916");

// Whether a module's reported hashes are the pair these layouts belong to.
[[nodiscard]] inline bool matches_pinned_api(
    std::string_view platform_hash, std::string_view universal_hash) noexcept {
  return platform_hash == pinned_api_hash_platform &&
      universal_hash == pinned_api_hash_universal;
}

}  // namespace ncm::cef
