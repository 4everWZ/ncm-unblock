#include "ncm/network_stack_census/census.hpp"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

namespace census = ncm::network_stack_census;

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_the_allowlist_classifies_by_family() {
  struct expectation {
    const wchar_t* name;
    census::stack_class classification;
  };
  const expectation expected[]{
      {L"winhttp.dll", census::stack_class::winhttp},
      {L"WINHTTP.DLL", census::stack_class::winhttp},
      {L"wininet.dll", census::stack_class::wininet},
      {L"ws2_32.dll", census::stack_class::winsock},
      {L"schannel.dll", census::stack_class::schannel},
      {L"ncryptsslp.dll", census::stack_class::schannel},
      {L"libcurl.dll", census::stack_class::libcurl},
      {L"libcurl-x86.dll", census::stack_class::libcurl},
      {L"libcef.dll", census::stack_class::cef},
      {L"libssl-1_1.dll", census::stack_class::openssl},
      {L"audioses.dll", census::stack_class::audio_render},
      {L"XAudio2_9.dll", census::stack_class::audio_render},
      {L"winmm.dll", census::stack_class::winmm},
      {L"kernel32.dll", census::stack_class::other},
      {L"cloudmusic.exe", census::stack_class::other},
      {L"", census::stack_class::other},
      // An exact rule must not match a longer name that merely starts with it.
      {L"winhttp.dll.mui", census::stack_class::other},
  };
  for (const auto& item : expected) {
    require(
        census::classify_module(item.name) == item.classification,
        std::string("a module was misclassified: ") +
            census::stack_class_name(census::classify_module(item.name)));
  }
  require(
      census::classify_module(nullptr) == census::stack_class::other,
      "a null module name was not classified as other");
}

void test_a_lazy_load_is_captured_after_the_snapshot() {
  require(census::begin_capture(), "loader notifications could not be registered");

  const unsigned snapshot_count = census::recorded_event_count();
  // A bare console host need not map any allowlisted stack, so the snapshot's
  // recorded subset may legitimately be empty; the walk itself must not be.
  require(
      census::observed_module_count() != 0, "the startup snapshot walked no modules");
  require(
      census::observed_module_count() >= snapshot_count,
      "the observed total is smaller than the recorded subset");
  require(
      !census::recorded_event(snapshot_count, nullptr),
      "an out-of-range event was reported as readable");

  // Loaded by absolute system path so the test never depends on search order,
  // and chosen because a plain console host does not already need it.
  wchar_t system_directory[MAX_PATH]{};
  require(
      GetSystemDirectoryW(system_directory, MAX_PATH) != 0,
      "unable to read the system directory");
  const std::filesystem::path target =
      std::filesystem::path(system_directory) / L"winhttp.dll";
  const HMODULE loaded = LoadLibraryW(target.c_str());
  require(loaded != nullptr, "unable to load the observation target");

  const unsigned after_count = census::recorded_event_count();
  require(
      after_count > snapshot_count,
      "loading a module produced no loader notification");

  bool saw_winhttp_load = false;
  for (unsigned index = snapshot_count; index < after_count; ++index) {
    census::census_event event{};
    require(census::recorded_event(index, &event), "a published event was unreadable");
    if (event.classification == census::stack_class::winhttp && event.loaded) {
      saw_winhttp_load = true;
    }
  }
  require(saw_winhttp_load, "the lazy load was not attributed to winhttp");

  FreeLibrary(loaded);
}

void test_the_report_carries_the_timeline_and_totals() {
  const std::filesystem::path report =
      std::filesystem::temp_directory_path() /
      (L"ncm-census-" + std::to_wstring(GetCurrentProcessId()) + L".txt");
  require(census::write_report(report.c_str()), "the census report was not written");

  std::ifstream stream(report, std::ios::binary);
  require(static_cast<bool>(stream), "the census report could not be reopened");
  const std::string text(
      (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
  stream.close();
  std::filesystem::remove(report);

  // A console test has no CEF `--type=` switch, so it stands in for the client
  // root process.
  require(text.find("role=root") != std::string::npos, "the report has no process role");
  require(text.find("winhttp") != std::string::npos, "the report lost the observed load");
  require(text.find("totals observed=") != std::string::npos, "the report has no totals");
  require(
      text.find("dropped=0") != std::string::npos,
      "the report dropped events inside its fixed capacity");
}

}  // namespace

int wmain() {
  try {
    test_the_allowlist_classifies_by_family();
    test_a_lazy_load_is_captured_after_the_snapshot();
    test_the_report_carries_the_timeline_and_totals();
    std::cout << "network stack census tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
