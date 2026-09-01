#include "ncm/winmm_proxy/forwarder.hpp"
#include "winmm_backend_contract.hpp"

#include <Windows.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class scoped_environment {
 public:
  scoped_environment(std::wstring name, const std::wstring& value) : name_(std::move(name)) {
    const auto required = GetEnvironmentVariableW(name_.c_str(), nullptr, 0);
    if (required != 0) {
      previous_.resize(required);
      const auto written = GetEnvironmentVariableW(
          name_.c_str(), previous_.data(), static_cast<DWORD>(previous_.size()));
      require(written != 0 && written < previous_.size(), "unable to preserve an environment value");
      previous_.resize(written);
      had_previous_ = true;
    }
    require(SetEnvironmentVariableW(name_.c_str(), value.c_str()) != 0,
            "unable to publish the fixture backend path");
  }

  ~scoped_environment() {
    SetEnvironmentVariableW(name_.c_str(), had_previous_ ? previous_.c_str() : nullptr);
  }

  scoped_environment(const scoped_environment&) = delete;
  scoped_environment& operator=(const scoped_environment&) = delete;

 private:
  std::wstring name_;
  std::wstring previous_;
  bool had_previous_{};
};

// The experiment depends on which module owns the `winmm.dll` base name during
// loader search, and the searched application directory is the directory of the
// running image. Each probe therefore runs from its own staged directory with
// the fixture beside it, exactly as a proxy would sit beside `cloudmusic.exe`.
class staged_tree {
 public:
  staged_tree(const std::filesystem::path& probe, const std::filesystem::path& backend_fixture) {
    root_ = std::filesystem::temp_directory_path() /
        (L"ncm_winmm_forwarder_tests_" + std::to_wstring(GetCurrentProcessId()) + L"_" +
         std::to_wstring(GetTickCount64()));
    require(std::filesystem::create_directory(root_), "unable to create the staging directory");
    try {
      forward_probe_ = stage_probe(probe, L"forward");
      control_probe_ = stage_probe(probe, L"control");
      backend_ = root_ / L"backend" / L"winmm.dll";
      require(std::filesystem::create_directory(backend_.parent_path()),
              "unable to create the backend directory");
      copy(backend_fixture, backend_);
    } catch (...) {
      remove_all();
      throw;
    }
  }

  ~staged_tree() { remove_all(); }

  staged_tree(const staged_tree&) = delete;
  staged_tree& operator=(const staged_tree&) = delete;

  // Places a proxy beside one of the staged probes and returns its path.
  [[nodiscard]] std::filesystem::path place_proxy(
      const std::filesystem::path& source, const std::filesystem::path& probe) {
    auto destination = probe.parent_path() / L"winmm.dll";
    copy(source, destination);
    return destination;
  }

  [[nodiscard]] const std::filesystem::path& forward_probe() const noexcept { return forward_probe_; }
  [[nodiscard]] const std::filesystem::path& control_probe() const noexcept { return control_probe_; }
  [[nodiscard]] const std::filesystem::path& backend() const noexcept { return backend_; }
  [[nodiscard]] std::filesystem::path report_path(const wchar_t* name) const {
    return root_ / name;
  }

 private:
  [[nodiscard]] std::filesystem::path stage_probe(
      const std::filesystem::path& probe, const wchar_t* directory) {
    const auto target_directory = root_ / directory;
    require(std::filesystem::create_directory(target_directory),
            "unable to create a probe staging directory");
    auto destination = target_directory / probe.filename();
    copy(probe, destination);
    return destination;
  }

  void copy(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code status;
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing, status);
    require(!status, "unable to stage " + source.filename().string());
    staged_.push_back(destination);
  }

  void remove_all() noexcept {
    std::error_code status;
    for (const auto& item : staged_) {
      std::filesystem::remove(item, status);
    }
    for (const auto& directory : {L"forward", L"control", L"backend"}) {
      std::filesystem::remove(root_ / directory, status);
    }
    for (const auto& item : std::filesystem::directory_iterator(root_, status)) {
      std::filesystem::remove(item.path(), status);
    }
    std::filesystem::remove(root_, status);
  }

  std::filesystem::path root_;
  std::filesystem::path forward_probe_;
  std::filesystem::path control_probe_;
  std::filesystem::path backend_;
  std::vector<std::filesystem::path> staged_;
};

struct probe_outcome {
  DWORD exit_code{};
  std::string report;
};

[[nodiscard]] probe_outcome run_probe(
    const std::filesystem::path& probe, const std::vector<std::wstring>& arguments,
    const std::filesystem::path& report_path) {
  std::wstring command_line = L"\"" + probe.wstring() + L"\"";
  for (const auto& argument : arguments) {
    command_line += L" \"" + argument + L"\"";
  }

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  require(CreateProcessW(
              probe.wstring().c_str(), command_line.data(), nullptr, nullptr, FALSE,
              CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != 0,
          "unable to start the winmm forwarder probe");

  probe_outcome outcome;
  const auto wait = WaitForSingleObject(process.hProcess, 30000);
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 5000);
  }
  GetExitCodeProcess(process.hProcess, &outcome.exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  require(wait == WAIT_OBJECT_0, "the winmm forwarder probe did not finish within its budget");

  {
    std::ifstream report(report_path, std::ios::binary);
    if (report) {
      std::ostringstream buffer;
      buffer << report.rdbuf();
      outcome.report = buffer.str();
    }
  }
  std::error_code status;
  std::filesystem::remove(report_path, status);
  return outcome;
}

// The manifest is the parity contract; drift here would silently shrink the
// surface every generated artifact is built from.
void test_pinned_surface_is_complete() {
  unsigned count{};
  const auto* entries = ncm::winmm_proxy::pinned_exports(&count);
  require(entries != nullptr, "the pinned export table is unavailable");
  require(count == ncm::winmm_proxy_fixture::entry_count,
          "the pinned export table and the generated contract disagree on the entry count");
  require(count == 193, "the pinned WinMM surface is not the captured 193-entry surface");

  std::set<std::string> names;
  unsigned unnamed = 0;
  for (unsigned index = 0; index < count; ++index) {
    const auto& entry = entries[index];
    require(static_cast<unsigned>(entry.ordinal) == 2u + index,
            "the pinned ordinals are not consecutive from 2");
    if (entry.name == nullptr) {
      ++unnamed;
    } else {
      require(names.insert(entry.name).second, "the pinned surface repeats an export name");
    }
    if (entry.alias_of != 0) {
      require(static_cast<unsigned>(entry.alias_of) >= 2u &&
                  static_cast<unsigned>(entry.alias_of) < 2u + count,
              "a pinned alias points outside the surface");
      require(entry.alias_of != entry.ordinal, "a pinned entry aliases itself");
    }
  }

  require(entries[count - 1].ordinal == 194, "the pinned surface does not end at ordinal 194");
  require(unnamed == 1, "the pinned surface does not contain exactly one unnamed export");
  require(names.size() == 192, "the pinned surface does not contain 192 distinct names");
  require(entries[0].ordinal == 2 && entries[0].name == nullptr && entries[0].alias_of == 11,
          "the unnamed ordinal 2 no longer aliases the PlaySound entry point");
}

// The mechanism claim: a backend loaded by absolute path is a module distinct
// from a same-named proxy in the application directory, and every pinned entry
// reaches it with the x86 stdcall contract intact.
void test_absolute_path_backend_is_distinct(
    staged_tree& tree, const std::filesystem::path& proxy_fixture) {
  const auto proxy = tree.place_proxy(proxy_fixture, tree.forward_probe());
  const auto report = tree.report_path(L"forward.txt");
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", tree.backend().wstring());

  const auto outcome = run_probe(
      tree.forward_probe(),
      {L"forward", proxy.wstring(), report.wstring(), tree.backend().wstring()}, report);
  require(outcome.exit_code == 0,
          "the forwarding probe failed (" + std::to_string(outcome.exit_code) + "): " +
              outcome.report);
  require(outcome.report.find("distinct=yes") != std::string::npos,
          "the forwarding probe did not confirm a distinct backend: " + outcome.report);
  require(outcome.report.find("entries=193") != std::string::npos,
          "the forwarding probe did not cover the whole surface: " + outcome.report);
}

// The falsified alternative: a `.def` forwarder naming the module the proxy
// itself carries must never reach a separate backend.
void test_def_forwarder_cannot_reach_a_backend(
    staged_tree& tree, const std::filesystem::path& control_fixture) {
  const auto control = tree.place_proxy(control_fixture, tree.control_probe());
  const auto report = tree.report_path(L"selfforward.txt");
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", tree.backend().wstring());

  const auto outcome = run_probe(
      tree.control_probe(), {L"selfforward", control.wstring(), report.wstring()}, report);
  require(outcome.exit_code == 0,
          "the self-forward control failed (" + std::to_string(outcome.exit_code) + "): " +
              outcome.report);
  require(outcome.report.rfind("selfforward:", 0) == 0,
          "the self-forward control did not report a classification: " + outcome.report);
  require(outcome.report.find("elsewhere=0") != std::string::npos,
          "a self-forwarding proxy resolved into a module outside itself: " + outcome.report);
}

// The proxy must stop the process rather than forward a surface it could not
// verify. Without this the same code path would silently dispatch into whatever
// module happened to answer.
void test_unreachable_backend_fails_closed(staged_tree& tree) {
  const auto proxy = tree.forward_probe().parent_path() / L"winmm.dll";
  const auto report = tree.report_path(L"resolvefailure.txt");
  const scoped_environment backend_variable(
      L"NCM_WINMM_FIXTURE_BACKEND", (tree.backend().parent_path() / L"absent.dll").wstring());

  const auto outcome = run_probe(
      tree.forward_probe(), {L"resolvefailure", proxy.wstring(), report.wstring()}, report);
  require(outcome.exit_code == ncm::winmm_proxy::backend_failure_exit_code,
          "an unreachable backend did not stop the process with the documented exit code (" +
              std::to_string(outcome.exit_code) + "): " + outcome.report);
  require(outcome.report.empty(),
          "the probe kept running after an unreachable backend: " + outcome.report);
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 5,
            "probe, proxy fixture, backend fixture, and self-forward fixture paths are required");
    const auto probe = std::filesystem::canonical(arguments[1]);
    const auto proxy_fixture = std::filesystem::canonical(arguments[2]);
    const auto backend_fixture = std::filesystem::canonical(arguments[3]);
    const auto control_fixture = std::filesystem::canonical(arguments[4]);

    test_pinned_surface_is_complete();

    staged_tree tree(probe, backend_fixture);
    test_absolute_path_backend_is_distinct(tree, proxy_fixture);
    test_unreachable_backend_fails_closed(tree);
    test_def_forwarder_cannot_reach_a_backend(tree, control_fixture);

    std::cout << "winmm forwarder tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
