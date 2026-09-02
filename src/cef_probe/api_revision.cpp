#include "ncm/cef_probe/api_revision.hpp"

#include "ncm/runtime_probe/pe_image.hpp"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace ncm::cef_probe {
namespace {

#pragma warning(push)
// A naked function supplies its own prologue, epilogue, and return value, and
// reaches its parameters through the frame it sets up itself.
#pragma warning(disable : 4100 4716 4731)

// Calls a one-argument export and records how far the stack pointer moved
// across the call.
//
// The argument is pushed here and the stack pointer is restored from a saved
// copy afterwards, so either cleanup contract leaves this caller intact and the
// delta is evidence rather than a crash. A zero-argument export cannot be
// discriminated this way and does not need to be: both contracts agree when
// there is nothing to pop.
__declspec(naked) unsigned long __cdecl invoke_one_argument(
    const void* target, int argument, int* stack_delta) {
  __asm {
    push ebp
    mov  ebp, esp
    push ebx
    mov  ebx, esp
    push dword ptr [ebp + 12]
    call dword ptr [ebp + 8]
    mov  ecx, esp
    sub  ecx, ebx
    mov  edx, dword ptr [ebp + 16]
    mov  dword ptr [edx], ecx
    mov  esp, ebx
    pop  ebx
    pop  ebp
    ret
  }
}

#pragma warning(pop)

// Copies a returned C string without trusting its length or contents. The
// module owns the storage and the probe only needs a bounded printable prefix,
// so a pointer that turns out not to reference readable memory is reported as
// unavailable instead of faulting the process. No C++ object is live across the
// guarded region.
[[nodiscard]] bool copy_bounded_ascii(
    const char* source, char* destination, std::size_t capacity) noexcept {
  if (source == nullptr || destination == nullptr || capacity == 0) {
    return false;
  }
  __try {
    for (std::size_t index = 0; index + 1 < capacity; ++index) {
      const char value = source[index];
      if (value == '\0') {
        destination[index] = '\0';
        return true;
      }
      if (value < 0x20 || value > 0x7e) {
        return false;
      }
      destination[index] = value;
    }
    return false;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

[[nodiscard]] std::optional<std::string> read_hash(const void* target, int entry, int* delta) {
  const auto raw = invoke_one_argument(target, entry, delta);
  std::array<char, 128> buffer{};
  if (!copy_bounded_ascii(
          reinterpret_cast<const char*>(raw), buffer.data(), buffer.size())) {
    return std::nullopt;
  }
  std::string value(buffer.data());
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

// Folds one measured delta into the running verdict. A single observation names
// a contract; a later disagreement demotes the whole probe rather than letting
// the last call win.
void accumulate(argument_cleanup* cleanup, int observed) {
  const auto seen = observed == -4  ? argument_cleanup::caller
                  : observed == 0   ? argument_cleanup::callee
                                    : argument_cleanup::inconsistent;
  if (*cleanup == argument_cleanup::unknown) {
    *cleanup = seen;
    return;
  }
  if (*cleanup != seen) {
    *cleanup = argument_cleanup::inconsistent;
  }
}

[[nodiscard]] std::string last_error_text() {
  return std::to_string(static_cast<unsigned long>(GetLastError()));
}

}  // namespace

const std::vector<std::string>& required_entry_points() {
  // Grouped by the role each one plays in the business-layer design, so an
  // absence reads as a missing capability rather than a missing symbol.
  static const std::vector<std::string> entry_points{
      // Injection without a struct layout.
      "cef_register_extension",
      // V8 bridge construction.
      "cef_v8context_get_current_context",
      "cef_v8value_create_function",
      "cef_v8value_create_object",
      "cef_v8value_create_string",
      // Asynchronous completion across threads and processes.
      "cef_post_task",
      "cef_task_runner_get_for_thread",
      "cef_currently_on",
      "cef_process_message_create",
      // Process role and frontend delivery.
      "cef_execute_process",
      "cef_register_scheme_handler_factory",
      // Revision identification.
      "cef_api_hash",
      "cef_version_info",
      "cef_build_revision",
  };
  return entry_points;
}

std::string describe(argument_cleanup cleanup) {
  switch (cleanup) {
    case argument_cleanup::caller:
      return "caller-cleans (__cdecl)";
    case argument_cleanup::callee:
      return "callee-cleans (__stdcall)";
    case argument_cleanup::inconsistent:
      return "inconsistent";
    case argument_cleanup::unknown:
      break;
  }
  return "unknown";
}

api_revision read_api_revision(const std::filesystem::path& module_path) {
  // Architecture is checked before the load so a 64-bit module is reported as
  // the wrong architecture rather than as a module whose exports are missing.
  const auto image = runtime_probe::inspect_pe_image(module_path);
  if (image.machine != IMAGE_FILE_MACHINE_I386 || image.pe32_plus) {
    throw std::runtime_error(
        "the module is not a PE32 x86 image, so its exports cannot be called from this process");
  }

  api_revision revision;
  revision.module_path = module_path;
  revision.file_version = image.file_version;

  // The altered search path lets the module resolve its own dependencies from
  // its own directory, which is where a browser runtime's siblings live.
  const HMODULE module = LoadLibraryExW(
      module_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (module == nullptr) {
    throw std::runtime_error("the module did not load: " + last_error_text());
  }

  for (const auto& name : required_entry_points()) {
    revision.entry_points.push_back(
        {name, GetProcAddress(module, name.c_str()) != nullptr});
  }

  const auto* const version_info =
      reinterpret_cast<const void*>(GetProcAddress(module, "cef_version_info"));
  if (version_info == nullptr) {
    throw std::runtime_error(
        "the module does not export cef_version_info, so it does not identify itself");
  }

  // The cleanup contract is established from this export first, because every
  // later read depends on it holding.
  int delta = 0;
  for (int entry = 0; entry <= 5; ++entry) {
    const auto value = invoke_one_argument(version_info, entry, &delta);
    accumulate(&revision.cleanup, delta);
    revision.argument_stack_delta = delta;
    revision.version_fields.push_back({entry, static_cast<int>(value)});
  }

  if (revision.cleanup == argument_cleanup::inconsistent) {
    throw std::runtime_error(
        "cef_version_info did not follow one argument-cleanup contract, so no value read from "
        "this module is trustworthy");
  }

  if (const auto* const api_hash =
          reinterpret_cast<const void*>(GetProcAddress(module, "cef_api_hash"))) {
    revision.platform_hash = read_hash(api_hash, 0, &delta);
    accumulate(&revision.cleanup, delta);
    revision.universal_hash = read_hash(api_hash, 1, &delta);
    accumulate(&revision.cleanup, delta);
    revision.commit_hash = read_hash(api_hash, 2, &delta);
    accumulate(&revision.cleanup, delta);
  }

  // A zero-argument export cleans up identically under either contract, so it
  // is safe to call once the one-argument contract is known.
  if (const auto build_revision = GetProcAddress(module, "cef_build_revision")) {
    using revision_export = int(__cdecl*)();
    revision.build_revision = reinterpret_cast<revision_export>(build_revision)();
  }

  if (revision.cleanup == argument_cleanup::inconsistent) {
    throw std::runtime_error(
        "the module's information exports disagreed on the argument-cleanup contract");
  }

  return revision;
}

}  // namespace ncm::cef_probe
