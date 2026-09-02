#include "ncm/cef_probe/api_revision.hpp"

#include "ncm/cef/abi_1916.hpp"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] const ncm::cef_probe::entry_point_status& find_entry_point(
    const ncm::cef_probe::api_revision& revision, const std::string& name) {
  const auto match = std::ranges::find_if(
      revision.entry_points, [&name](const auto& entry) { return entry.name == name; });
  require(match != revision.entry_points.end(), name + " is not among the reported entry points");
  return *match;
}

// The probe must read the fixture's synthetic values exactly, and must name the
// contract the fixture was built with.
void test_reads_caller_cleanup_fixture(const std::filesystem::path& fixture) {
  const auto revision = ncm::cef_probe::read_api_revision(fixture);

  require(revision.cleanup == ncm::cef_probe::argument_cleanup::caller,
          "a __cdecl fixture was not reported as caller-cleans");
  require(revision.argument_stack_delta == -4,
          "a __cdecl fixture did not leave its argument on the stack");

  require(revision.version_fields.size() == 6, "cef_version_info entries 0 through 5 were not read");
  for (const auto& field : revision.version_fields) {
    require(field.value == field.entry * 100 + 7,
            "cef_version_info entry " + std::to_string(field.entry) + " returned " +
                std::to_string(field.value));
  }

  require(revision.platform_hash == "fixture-platform-0", "the platform hash was not read");
  require(revision.universal_hash == "fixture-universal-1", "the universal hash was not read");
  require(revision.commit_hash == "fixture-commit-2", "the commit hash was not read");
  require(revision.build_revision == 1750, "cef_build_revision was not read");
}

// The same source built as __stdcall. This is what distinguishes a measured
// contract from an assumed one: the values must still be read correctly and the
// verdict must change.
void test_detects_callee_cleanup_fixture(const std::filesystem::path& fixture) {
  const auto revision = ncm::cef_probe::read_api_revision(fixture);

  require(revision.cleanup == ncm::cef_probe::argument_cleanup::callee,
          "a __stdcall fixture was not reported as callee-cleans");
  require(revision.argument_stack_delta == 0,
          "a __stdcall fixture did not pop its own argument");

  require(revision.version_fields.size() == 6, "cef_version_info entries were not read");
  for (const auto& field : revision.version_fields) {
    require(field.value == field.entry * 100 + 7,
            "a callee-cleans call returned the wrong value for entry " +
                std::to_string(field.entry));
  }
  require(revision.build_revision == 1750,
          "a zero-argument export was not readable under the callee-cleans contract");
}

// A module that does not identify itself must be rejected outright rather than
// returned with the fields it happened to answer.
void test_requires_version_info(const std::filesystem::path& fixture) {
  bool rejected = false;
  try {
    static_cast<void>(ncm::cef_probe::read_api_revision(fixture));
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "a module without cef_version_info was accepted");
}

// Absence has to be reported as absence. The fixture exports none of the
// capability entry points, so every one of them must come back missing while
// the identification exports come back present.
void test_reports_absent_entry_points(const std::filesystem::path& fixture) {
  const auto revision = ncm::cef_probe::read_api_revision(fixture);

  require(!find_entry_point(revision, "cef_register_extension").present,
          "an entry point the fixture does not export was reported present");
  require(!find_entry_point(revision, "cef_post_task").present,
          "an entry point the fixture does not export was reported present");
  require(find_entry_point(revision, "cef_version_info").present,
          "an entry point the fixture does export was reported absent");
  require(find_entry_point(revision, "cef_api_hash").present,
          "an entry point the fixture does export was reported absent");

  const auto absent = std::ranges::count_if(
      revision.entry_points, [](const auto& entry) { return !entry.present; });
  require(absent == static_cast<std::ptrdiff_t>(revision.entry_points.size()) - 3,
          "the fixture reported an unexpected number of absent entry points");
}

void test_rejects_non_pe_image() {
  const auto path = std::filesystem::temp_directory_path() /
      (L"ncm-cef-probe-not-pe-" + std::to_wstring(GetCurrentProcessId()) + L".bin");
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "not a PE image";
  }

  bool rejected = false;
  try {
    static_cast<void>(ncm::cef_probe::read_api_revision(path));
  } catch (const std::exception&) {
    rejected = true;
  }

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  require(rejected, "a file that is not a PE image was accepted");
}

// The pinned pair gates every struct layout in `abi_1916.hpp`, so the match has
// to be exact in both directions. A fixture reporting synthetic hashes must not
// be mistaken for the module those layouts describe.
void test_pinned_api_match() {
  require(ncm::cef::matches_pinned_api(
              "78d4b4eb20e36e2b08572b98645dde08e987fbad",
              "ce45d134468cd9bad310409c96e5108d75fac3c7"),
          "the measured CEF 3.1916 hash pair was not recognised as the pinned pair");

  require(!ncm::cef::matches_pinned_api("fixture-platform-0", "fixture-universal-1"),
          "synthetic hashes were accepted as the pinned pair");
  require(!ncm::cef::matches_pinned_api(
              "78d4b4eb20e36e2b08572b98645dde08e987fbad", "ce45d134468cd9bad310409c96e5108d75fac3c8"),
          "a universal hash differing in one character was accepted");
  require(!ncm::cef::matches_pinned_api(
              "da45b2e3054ef869d85e805ae5789db575c766b5",
              "ce45d134468cd9bad310409c96e5108d75fac3c7"),
          "another platform's hash from the same branch was accepted");
  require(!ncm::cef::matches_pinned_api("", ""),
          "a module that reported no hashes was accepted");
}

// The probe reads the fixture's hashes, and the fixture is not the pinned
// module, so the two must disagree end to end rather than only in isolation.
void test_fixture_is_not_the_pinned_module(const std::filesystem::path& fixture) {
  const auto revision = ncm::cef_probe::read_api_revision(fixture);
  require(ncm::cef::matches_pinned_api(
              revision.platform_hash.value_or(std::string{}),
              revision.universal_hash.value_or(std::string{})) == false,
          "the synthetic fixture was reported as the pinned browser runtime");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 4) {
    std::cerr << "usage: cef_probe_tests <cdecl-fixture> <stdcall-fixture> <incomplete-fixture>\n";
    return 2;
  }

  try {
    const std::filesystem::path caller_fixture(arguments[1]);
    const std::filesystem::path callee_fixture(arguments[2]);
    const std::filesystem::path incomplete_fixture(arguments[3]);

    test_reads_caller_cleanup_fixture(caller_fixture);
    test_detects_callee_cleanup_fixture(callee_fixture);
    test_requires_version_info(incomplete_fixture);
    test_reports_absent_entry_points(caller_fixture);
    test_rejects_non_pe_image();
    test_pinned_api_match();
    test_fixture_is_not_the_pinned_module(caller_fixture);

    std::cout << "cef probe tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
