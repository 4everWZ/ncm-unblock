#include "ncm/module_load_probe/module_load_probe.hpp"

#include "ncm/runtime_probe/pe_image.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace ncm::module_load_probe {
namespace {

constexpr DWORD probe_termination_code = 0xE0000297U;
constexpr auto cleanup_timeout = std::chrono::seconds(5);

class unique_handle {
 public:
  unique_handle() noexcept = default;
  explicit unique_handle(HANDLE value) noexcept : value_(value) {}
  ~unique_handle() { reset(); }

  unique_handle(const unique_handle&) = delete;
  unique_handle& operator=(const unique_handle&) = delete;

  unique_handle(unique_handle&& other) noexcept
      : value_(std::exchange(other.value_, nullptr)) {}
  unique_handle& operator=(unique_handle&& other) noexcept {
    if (this != &other) {
      reset();
      value_ = std::exchange(other.value_, nullptr);
    }
    return *this;
  }

  [[nodiscard]] HANDLE get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
  }

  void reset(HANDLE value = nullptr) noexcept {
    if (*this) {
      CloseHandle(value_);
    }
    value_ = value;
  }

 private:
  HANDLE value_{};
};

[[noreturn]] void throw_last_error(const char* operation) {
  throw std::system_error(
      static_cast<int>(GetLastError()), std::system_category(), operation);
}

[[nodiscard]] DWORD wait_milliseconds(
    std::chrono::steady_clock::time_point deadline) noexcept {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return 0;
  }
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - now);
  constexpr auto maximum = static_cast<long long>(std::numeric_limits<DWORD>::max() - 1);
  return static_cast<DWORD>(std::clamp<long long>(remaining.count() + 1, 1, maximum));
}

[[nodiscard]] bool equal_ordinal_ignore_case(
    std::wstring_view left, std::wstring_view right) {
  if (left.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
      right.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  return CompareStringOrdinal(
             left.data(), static_cast<int>(left.size()), right.data(),
             static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] std::wstring without_extended_prefix(std::wstring_view path) {
  constexpr std::wstring_view extended_unc = L"\\\\?\\UNC\\";
  constexpr std::wstring_view extended = L"\\\\?\\";
  if (path.starts_with(extended_unc)) {
    return L"\\\\" + std::wstring(path.substr(extended_unc.size()));
  }
  if (path.starts_with(extended)) {
    return std::wstring(path.substr(extended.size()));
  }
  return std::wstring(path);
}

[[nodiscard]] std::wstring comparable_root_path(std::wstring_view path) {
  auto result = without_extended_prefix(path);
  while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
    result.pop_back();
  }
  return result;
}

[[nodiscard]] std::string lowercase_ascii(std::wstring_view value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    if (character > 0x7f) {
      throw std::invalid_argument("module basename must contain only ASCII characters");
    }
    auto narrow = static_cast<char>(character);
    if (narrow >= 'A' && narrow <= 'Z') {
      narrow = static_cast<char>(narrow - 'A' + 'a');
    }
    result.push_back(narrow);
  }
  return result;
}

[[nodiscard]] std::filesystem::path final_path_from_handle(HANDLE file) {
  constexpr DWORD flags = FILE_NAME_NORMALIZED | VOLUME_NAME_DOS;
  const auto required = GetFinalPathNameByHandleW(file, nullptr, 0, flags);
  if (required == 0) {
    throw_last_error("GetFinalPathNameByHandleW(size)");
  }
  std::wstring buffer(static_cast<std::size_t>(required), L'\0');
  const auto written = GetFinalPathNameByHandleW(
      file, buffer.data(), static_cast<DWORD>(buffer.size()), flags);
  if (written == 0) {
    throw_last_error("GetFinalPathNameByHandleW");
  }
  if (written >= buffer.size()) {
    throw std::runtime_error("module path changed while it was being resolved");
  }
  buffer.resize(written);
  return std::filesystem::path(std::move(buffer));
}

void reject_reparse_handle(HANDLE file, const char* description) {
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  if (!GetFileInformationByHandleEx(
          file, FileAttributeTagInfo, &attributes, sizeof(attributes))) {
    throw_last_error("GetFileInformationByHandleEx(FileAttributeTagInfo)");
  }
  if ((attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
    throw std::runtime_error(std::string(description) + " is a reparse point");
  }
}

void reject_reparse_path(const std::filesystem::path& path, const char* description) {
  for (const auto& component : path) {
    if (component == L"." || component == L"..") {
      throw std::invalid_argument(std::string(description) +
          " path contains a relative traversal component");
    }
  }
  std::wstring volume_buffer(32768, L'\0');
  if (!GetVolumePathNameW(
          path.c_str(), volume_buffer.data(), static_cast<DWORD>(volume_buffer.size()))) {
    throw_last_error("GetVolumePathNameW");
  }
  volume_buffer.resize(std::wcslen(volume_buffer.c_str()));
  const std::filesystem::path volume_root(volume_buffer);
  auto current = path;
  for (;;) {
    const auto at_volume_root = equal_ordinal_ignore_case(
        comparable_root_path(current.wstring()),
        comparable_root_path(volume_root.wstring()));
    const auto& path_to_check = at_volume_root ? volume_root : current;
    const auto attributes = GetFileAttributesW(path_to_check.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      const auto status = GetLastError();
      throw std::system_error(
          static_cast<int>(status), std::system_category(),
          "GetFileAttributesW(" + path_to_check.string() + ")");
    }
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
      throw std::runtime_error(std::string(description) +
          " path contains a reparse point");
    }
    if (at_volume_root) {
      break;
    }
    const auto parent = current.parent_path();
    if (parent.empty() || parent == current) {
      throw std::runtime_error(std::string(description) +
          " path ancestry did not reach its volume root");
    }
    current = parent;
  }
}

struct file_identity {
  FILE_ID_INFO value{};
};

[[nodiscard]] file_identity identity_from_handle(HANDLE file) {
  file_identity result;
  if (!GetFileInformationByHandleEx(
          file, FileIdInfo, &result.value, sizeof(result.value))) {
    throw_last_error("GetFileInformationByHandleEx(FileIdInfo)");
  }
  return result;
}

[[nodiscard]] bool same_identity(
    const file_identity& left, const file_identity& right) noexcept {
  return left.value.VolumeSerialNumber == right.value.VolumeSerialNumber &&
      std::memcmp(
          left.value.FileId.Identifier, right.value.FileId.Identifier,
          sizeof(left.value.FileId.Identifier)) == 0;
}

struct pinned_file {
  unique_handle handle;
  file_identity identity;
  std::filesystem::path final_path;
};

[[nodiscard]] pinned_file pin_file(
    const std::filesystem::path& path, const char* description) {
  unique_handle file(CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr));
  if (!file) {
    throw_last_error("CreateFileW(pinned image)");
  }
  reject_reparse_handle(file.get(), description);
  auto final_path = final_path_from_handle(file.get());
  reject_reparse_path(final_path, description);
  const auto identity = identity_from_handle(file.get());
  return {std::move(file), identity, std::move(final_path)};
}

void close_debug_file(DEBUG_EVENT& event) noexcept {
  HANDLE* file{};
  if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
    file = &event.u.CreateProcessInfo.hFile;
  } else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
    file = &event.u.LoadDll.hFile;
  }
  if (file != nullptr && *file != nullptr && *file != INVALID_HANDLE_VALUE) {
    CloseHandle(*file);
    *file = nullptr;
  }
}

[[nodiscard]] bool job_is_empty(HANDLE job) noexcept {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
  return QueryInformationJobObject(
             job, JobObjectBasicAccountingInformation, &accounting,
             sizeof(accounting), nullptr) != FALSE &&
      accounting.ActiveProcesses == 0;
}

struct debug_session {
  unique_handle job;
  unique_handle process;
  unique_handle primary_thread;
  DWORD process_id{};
  bool assigned_to_job{};
  bool exit_event_seen{};
  bool event_pending{};
  DEBUG_EVENT event{};
  std::size_t event_count{};

  ~debug_session();
};

[[nodiscard]] std::optional<std::string> terminate_and_drain(debug_session& session) noexcept {
  if (!session.process) {
    return std::nullopt;
  }

  if (WaitForSingleObject(session.process.get(), 0) == WAIT_TIMEOUT) {
    const auto terminated = session.assigned_to_job
        ? TerminateJobObject(session.job.get(), probe_termination_code)
        : TerminateProcess(session.process.get(), probe_termination_code);
    if (!terminated) {
      return "unable to request fail-closed process termination";
    }
  }

  if (session.event_pending) {
    close_debug_file(session.event);
    if (!ContinueDebugEvent(
            session.event.dwProcessId, session.event.dwThreadId, DBG_CONTINUE)) {
      return "unable to continue the stopped debug event after termination";
    }
    session.event_pending = false;
  }

  const auto deadline = std::chrono::steady_clock::now() + cleanup_timeout;
  constexpr std::size_t maximum_cleanup_events = 1024;
  std::size_t cleanup_events{};
  while (!session.exit_event_seen && cleanup_events < maximum_cleanup_events) {
    const auto wait = wait_milliseconds(deadline);
    if (wait == 0) {
      break;
    }
    session.event = {};
    if (!WaitForDebugEvent(&session.event, wait)) {
      if (GetLastError() == ERROR_SEM_TIMEOUT) {
        break;
      }
      return "unable to drain debug events after termination";
    }
    session.event_pending = true;
    ++cleanup_events;
    close_debug_file(session.event);
    if (session.event.dwProcessId != session.process_id) {
      return "received a debug event for an unexpected process while draining";
    }
    if (session.event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
      session.exit_event_seen = true;
    }
    if (!ContinueDebugEvent(
            session.event.dwProcessId, session.event.dwThreadId, DBG_CONTINUE)) {
      return "unable to continue a teardown debug event";
    }
    session.event_pending = false;
  }

  const auto wait = wait_milliseconds(deadline);
  if (WaitForSingleObject(session.process.get(), wait) != WAIT_OBJECT_0) {
    return "terminated target did not exit within the cleanup deadline";
  }
  if (!session.exit_event_seen) {
    return "EXIT_PROCESS_DEBUG_EVENT was not observed";
  }
  if (session.assigned_to_job && !job_is_empty(session.job.get())) {
    return "private probe job was not empty after target exit";
  }
  return std::nullopt;
}

debug_session::~debug_session() {
  if (!process) {
    return;
  }
  const auto requires_cleanup = event_pending || !exit_event_seen ||
      WaitForSingleObject(process.get(), 0) == WAIT_TIMEOUT;
  if (!requires_cleanup) {
    return;
  }

  // A prior cleanup attempt may have returned with an event still pending.
  // Retry the complete terminate/continue/drain sequence before the job handle
  // provides the final kill-on-close fallback.
  (void)terminate_and_drain(*this);
  if (event_pending) {
    if (assigned_to_job && job) {
      TerminateJobObject(job.get(), probe_termination_code);
    } else {
      TerminateProcess(process.get(), probe_termination_code);
    }
    close_debug_file(event);
    if (ContinueDebugEvent(event.dwProcessId, event.dwThreadId, DBG_CONTINUE)) {
      event_pending = false;
      (void)terminate_and_drain(*this);
    }
  }
  WaitForSingleObject(
      process.get(), static_cast<DWORD>(cleanup_timeout.count() * 1000));
}

void validate_options(const probe_options& options) {
  if (options.target_executable.empty() || !options.target_executable.is_absolute()) {
    throw std::invalid_argument("target executable path must be absolute");
  }
  if (options.expected_module_path.empty() || !options.expected_module_path.is_absolute()) {
    throw std::invalid_argument("expected module path must be absolute");
  }
  if (options.module_basename.empty() ||
      options.module_basename == L"." || options.module_basename == L".." ||
      std::filesystem::path(options.module_basename).filename().wstring() !=
          options.module_basename) {
    throw std::invalid_argument("module name must be a basename without directory components");
  }
  if (!equal_ordinal_ignore_case(
          options.expected_module_path.filename().wstring(), options.module_basename)) {
    throw std::invalid_argument("expected module filename does not match module basename");
  }
  if (options.timeout <= std::chrono::milliseconds::zero() ||
      options.timeout > std::chrono::seconds(60)) {
    throw std::invalid_argument("timeout must be from 1 through 60000 milliseconds");
  }
  if (options.maximum_events == 0 || options.maximum_events > 4096) {
    throw std::invalid_argument("maximum debug event count must be from 1 through 4096");
  }
  if (options.target_executable.wstring().find(L'\0') != std::wstring::npos ||
      options.expected_module_path.wstring().find(L'\0') != std::wstring::npos ||
      options.module_basename.find(L'\0') != std::wstring::npos) {
    throw std::invalid_argument("probe inputs contain an embedded NUL");
  }
}

struct preflight_result {
  pinned_file target;
  pinned_file expected;
};

[[nodiscard]] preflight_result preflight(const probe_options& options) {
  validate_options(options);
  reject_reparse_path(options.target_executable, "target executable");
  reject_reparse_path(options.expected_module_path, "expected module");
  auto pinned_target = pin_file(options.target_executable, "target executable");
  auto pinned_expected = pin_file(options.expected_module_path, "expected module");
  const auto target = runtime_probe::inspect_pe_image(pinned_target.final_path);
  if (target.machine != IMAGE_FILE_MACHINE_I386 || target.pe32_plus) {
    throw std::runtime_error("target executable is not an x86 PE32 image");
  }
  if (options.require_target_signature && !target.signature.valid) {
    throw std::runtime_error("target executable does not have a valid Authenticode signature");
  }

  const auto module = lowercase_ascii(options.module_basename);
  if (std::ranges::find(target.imports, module) == target.imports.end()) {
    throw std::runtime_error("target module is not present in the ordinary import directory");
  }
  if (std::ranges::find(target.delay_imports, module) != target.delay_imports.end()) {
    throw std::runtime_error("target module is also present in the delay-import directory");
  }

  const auto expected = runtime_probe::inspect_pe_image(pinned_expected.final_path);
  if (expected.machine != IMAGE_FILE_MACHINE_I386 || expected.pe32_plus) {
    throw std::runtime_error("expected module is not an x86 PE32 image");
  }
  if (options.require_expected_signature && !expected.signature.valid) {
    throw std::runtime_error("expected module does not have a valid Authenticode signature");
  }
  return {std::move(pinned_target), std::move(pinned_expected)};
}

[[nodiscard]] std::wstring target_command_line(const std::filesystem::path& target) {
  std::wstring result = L"\"";
  result.append(target.wstring());
  result.push_back(L'\"');
  return result;
}

}  // namespace

probe_result run(const probe_options& options) {
  auto pinned = preflight(options);

  debug_session session;
  session.job.reset(CreateJobObjectW(nullptr, nullptr));
  if (!session.job) {
    throw_last_error("CreateJobObjectW");
  }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(
          session.job.get(), JobObjectExtendedLimitInformation, &limits,
          sizeof(limits))) {
    throw_last_error("SetInformationJobObject");
  }

  auto command_line = target_command_line(options.target_executable);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(
          options.target_executable.c_str(), command_line.data(), nullptr, nullptr,
          FALSE, DEBUG_ONLY_THIS_PROCESS | CREATE_UNICODE_ENVIRONMENT, nullptr,
          options.target_executable.parent_path().c_str(), &startup, &process)) {
    throw_last_error("CreateProcessW");
  }
  session.process.reset(process.hProcess);
  session.primary_thread.reset(process.hThread);
  session.process_id = process.dwProcessId;

  if (!AssignProcessToJobObject(session.job.get(), session.process.get())) {
    const auto status = GetLastError();
    const auto cleanup_error = terminate_and_drain(session);
    if (cleanup_error.has_value()) {
      throw std::runtime_error(
          "AssignProcessToJobObject failed and cleanup was incomplete: " + *cleanup_error);
    }
    SetLastError(status);
    throw_last_error("AssignProcessToJobObject");
  }
  session.assigned_to_job = true;

  const auto deadline = std::chrono::steady_clock::now() + options.timeout;
  std::optional<std::filesystem::path> observed_path;
  std::optional<std::string> failure;

  try {
    for (;;) {
      if (session.event_count >= options.maximum_events) {
        throw std::runtime_error("debug event count limit was reached");
      }
      const auto wait = wait_milliseconds(deadline);
      if (wait == 0) {
        throw std::runtime_error("module load probe timed out");
      }
      session.event = {};
      if (!WaitForDebugEvent(&session.event, wait)) {
        if (GetLastError() == ERROR_SEM_TIMEOUT) {
          throw std::runtime_error("module load probe timed out");
        }
        throw_last_error("WaitForDebugEvent");
      }
      session.event_pending = true;
      ++session.event_count;

      if (session.event.dwProcessId != session.process_id) {
        throw std::runtime_error("received a debug event for an unexpected process");
      }
      if (session.event_count == 1 &&
          session.event.dwDebugEventCode != CREATE_PROCESS_DEBUG_EVENT) {
        throw std::runtime_error("first debug event was not CREATE_PROCESS_DEBUG_EVENT");
      }

      if (session.event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) {
        if (session.event.u.CreateProcessInfo.hFile == nullptr ||
            session.event.u.CreateProcessInfo.hFile == INVALID_HANDLE_VALUE) {
          throw std::runtime_error(
              "CREATE_PROCESS_DEBUG_EVENT did not provide an image file handle");
        }
        unique_handle created_image(session.event.u.CreateProcessInfo.hFile);
        session.event.u.CreateProcessInfo.hFile = nullptr;
        reject_reparse_handle(created_image.get(), "created process image");
        if (!same_identity(
                identity_from_handle(created_image.get()), pinned.target.identity)) {
          throw std::runtime_error(
              "created process image does not match the pinned target file identity");
        }
      } else if (session.event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) {
        if (session.event.u.LoadDll.hFile == nullptr ||
            session.event.u.LoadDll.hFile == INVALID_HANDLE_VALUE) {
          throw std::runtime_error("LOAD_DLL_DEBUG_EVENT did not provide a file handle");
        }
        unique_handle loaded_file(session.event.u.LoadDll.hFile);
        session.event.u.LoadDll.hFile = nullptr;
        reject_reparse_handle(loaded_file.get(), "loaded module");
        auto loaded_path = final_path_from_handle(loaded_file.get());
        reject_reparse_path(loaded_path, "loaded module");
        if (equal_ordinal_ignore_case(
                loaded_path.filename().wstring(), options.module_basename)) {
          if (!same_identity(
                  identity_from_handle(loaded_file.get()), pinned.expected.identity)) {
            throw std::runtime_error(
                "loaded module does not match the pinned expected file identity");
          }
          observed_path = std::move(loaded_path);
          break;
        }
      } else if (session.event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
        const auto& exception = session.event.u.Exception;
        if (exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
            exception.dwFirstChance != FALSE) {
          throw std::runtime_error("initial breakpoint arrived before the target module was observed");
        }
        throw std::runtime_error("target raised an exception before the target module was observed");
      } else if (session.event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) {
        session.exit_event_seen = true;
        throw std::runtime_error("target exited before the target module was observed");
      } else if (session.event.dwDebugEventCode == RIP_EVENT) {
        throw std::runtime_error("debugger received a RIP_EVENT before the target module was observed");
      }

      if (!ContinueDebugEvent(
              session.event.dwProcessId, session.event.dwThreadId, DBG_CONTINUE)) {
        throw_last_error("ContinueDebugEvent");
      }
      session.event_pending = false;
    }
  } catch (const std::exception& error) {
    failure = error.what();
  }

  const auto cleanup_error = terminate_and_drain(session);
  if (cleanup_error.has_value()) {
    if (failure.has_value()) {
      throw std::runtime_error(*failure + "; cleanup failure: " + *cleanup_error);
    }
    throw std::runtime_error("cleanup failure: " + *cleanup_error);
  }
  if (failure.has_value()) {
    throw std::runtime_error(*failure);
  }
  if (!observed_path.has_value()) {
    throw std::runtime_error("target module observation was not recorded");
  }
  if (!equal_ordinal_ignore_case(
          observed_path->wstring(), pinned.expected.final_path.wstring())) {
    throw std::runtime_error("target module loaded from an unexpected final path");
  }
  return {*observed_path, session.event_count};
}

}  // namespace ncm::module_load_probe
