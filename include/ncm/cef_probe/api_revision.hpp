#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ncm::cef_probe {

// How a probed one-argument export cleaned up its argument.
//
// On x86 this is measurable rather than assumed: the probe pushes one argument
// and compares the stack pointer across the call. A callee that leaves it four
// bytes below the call site did not pop the argument and follows `__cdecl`; one
// that restores the call site popped it and follows `__stdcall`. Any other
// delta, or a delta that changes between calls, means the module does not
// follow either contract and nothing read through it may be trusted.
enum class argument_cleanup { unknown, caller, callee, inconsistent };

struct version_field {
  int entry{};
  int value{};
};

struct entry_point_status {
  std::string name;
  bool present{};
};

// What a module reports about the API revision it was built from.
struct api_revision {
  std::filesystem::path module_path;
  std::string file_version;

  argument_cleanup cleanup{argument_cleanup::unknown};
  int argument_stack_delta{};

  // `cef_api_hash` entries 0, 1, and 2. Absent when the export is missing or
  // returns nothing for that entry.
  std::optional<std::string> platform_hash;
  std::optional<std::string> universal_hash;
  std::optional<std::string> commit_hash;

  // `cef_version_info` entries 0 through 5.
  std::vector<version_field> version_fields;
  std::optional<int> build_revision;

  std::vector<entry_point_status> entry_points;
};

// Entry points the business-layer design depends on. Reported present or absent
// by name resolution only; none of them is called, because every one of them
// has process-wide side effects.
[[nodiscard]] const std::vector<std::string>& required_entry_points();

// Loads the module by absolute path and reads the revision it reports about
// itself.
//
// Throws when the file is not a PE32 x86 image, when it cannot be loaded, when
// `cef_version_info` is absent, or when the measured cleanup contract is not
// one the probe can call safely. A build the probe cannot identify must not be
// reported as a partially filled revision, because the whole purpose of this
// milestone is to decide whether a struct layout may be trusted.
//
// The module is deliberately not released. Unloading a browser runtime after
// its static initializers have run is not a supported operation, and this probe
// exits immediately afterwards.
[[nodiscard]] api_revision read_api_revision(const std::filesystem::path& module_path);

[[nodiscard]] std::string describe(argument_cleanup cleanup);

}  // namespace ncm::cef_probe
