#include "ncm/launcher/managed_process.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace ncm::launcher {
namespace {

[[nodiscard]] HANDLE as_handle(void* value) noexcept {
  return static_cast<HANDLE>(value);
}

[[noreturn]] void throw_last_error(const char* operation) {
  throw std::system_error(
      static_cast<int>(GetLastError()), std::system_category(), operation);
}

[[nodiscard]] std::wstring quote_argument(std::wstring_view argument) {
  if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
    return std::wstring(argument);
  }

  std::wstring result;
  result.push_back(L'"');
  std::size_t backslashes{};
  for (const auto character : argument) {
    if (character == L'\\') {
      ++backslashes;
      continue;
    }
    if (character == L'"') {
      result.append(backslashes * 2 + 1, L'\\');
      result.push_back(L'"');
    } else {
      result.append(backslashes, L'\\');
      result.push_back(character);
    }
    backslashes = 0;
  }
  result.append(backslashes * 2, L'\\');
  result.push_back(L'"');
  return result;
}

[[nodiscard]] std::wstring build_command_line(const process_spec& spec) {
  std::wstring result = quote_argument(spec.executable.wstring());
  for (const auto& argument : spec.arguments) {
    result.push_back(L' ');
    result.append(quote_argument(argument));
  }
  return result;
}

[[nodiscard]] DWORD wait_milliseconds(std::chrono::milliseconds timeout) noexcept {
  if (timeout.count() <= 0) {
    return 0;
  }
  constexpr auto maximum = static_cast<long long>(std::numeric_limits<DWORD>::max() - 1);
  return static_cast<DWORD>(std::min(timeout.count(), maximum));
}

void reject_embedded_nul(std::wstring_view value, const char* field) {
  if (value.find(L'\0') != std::wstring_view::npos) {
    throw std::invalid_argument(std::string(field) + " contains an embedded NUL");
  }
}

[[nodiscard]] std::wstring ascii_upper(std::wstring_view value) {
  std::wstring result(value);
  for (auto& character : result) {
    if (character >= L'a' && character <= L'z') {
      character = static_cast<wchar_t>(character - L'a' + L'A');
    }
  }
  return result;
}

// Build a CREATE_UNICODE_ENVIRONMENT block: NAME=VALUE\0...\0\0.
// Starts from the current process environment and overlays overrides.
[[nodiscard]] std::wstring build_environment_block(
    const std::vector<std::pair<std::wstring, std::wstring>>& overrides) {
  for (const auto& [name, value] : overrides) {
    if (name.empty()) {
      throw std::invalid_argument("environment variable name must not be empty");
    }
    reject_embedded_nul(name, "environment variable name");
    reject_embedded_nul(value, "environment variable value");
    if (name.find(L'=') != std::wstring_view::npos) {
      throw std::invalid_argument("environment variable name must not contain '='");
    }
  }

  const auto parent = GetEnvironmentStringsW();
  if (parent == nullptr) {
    throw_last_error("GetEnvironmentStringsW");
  }
  std::vector<std::pair<std::wstring, std::wstring>> entries;
  try {
    const wchar_t* cursor = parent;
    while (*cursor != L'\0') {
      const std::wstring_view entry(cursor);
      cursor += entry.size() + 1;
      // Skip the special "=C:=..." drive entries; still copy them as-is later
      // by treating name as the full string when no '=' after position 0.
      const auto separator = entry.find(L'=');
      if (separator == std::wstring_view::npos) {
        continue;
      }
      entries.emplace_back(
          std::wstring(entry.substr(0, separator)),
          std::wstring(entry.substr(separator + 1)));
    }
  } catch (...) {
    FreeEnvironmentStringsW(parent);
    throw;
  }
  FreeEnvironmentStringsW(parent);

  for (const auto& [name, value] : overrides) {
    const auto wanted = ascii_upper(name);
    bool replaced{};
    for (auto& entry : entries) {
      if (ascii_upper(entry.first) == wanted) {
        entry.first = name;
        entry.second = value;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      entries.emplace_back(name, value);
    }
  }

  std::wstring block;
  for (const auto& [name, value] : entries) {
    block.append(name);
    block.push_back(L'=');
    block.append(value);
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

[[nodiscard]] bool job_is_empty(HANDLE job) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
  if (!QueryInformationJobObject(
          job, JobObjectBasicAccountingInformation, &accounting,
          sizeof(accounting), nullptr)) {
    throw_last_error("QueryInformationJobObject");
  }
  return accounting.ActiveProcesses == 0;
}

class owned_handle {
 public:
  owned_handle() noexcept = default;
  explicit owned_handle(HANDLE handle) noexcept : handle_(handle) {}
  ~owned_handle() {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }
  owned_handle(const owned_handle&) = delete;
  owned_handle& operator=(const owned_handle&) = delete;
  owned_handle(owned_handle&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}
  owned_handle& operator=(owned_handle&& other) noexcept {
    if (this != &other) {
      if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
      }
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }
  [[nodiscard]] HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_{};
};

class process_startup {
 public:
  explicit process_startup(const process_spec& spec) {
    startup_.StartupInfo.cb = sizeof(startup_);
    if (!spec.output_file.has_value()) {
      return;
    }
    if (!spec.output_file->is_absolute()) {
      throw std::invalid_argument("managed output file path must be absolute");
    }
    reject_embedded_nul(spec.output_file->wstring(), "managed output file path");
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    output_ = owned_handle(CreateFileW(
        spec.output_file->c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &security,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output_.get() == INVALID_HANDLE_VALUE) {
      throw_last_error("CreateFileW(managed output)");
    }
    input_ = owned_handle(CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input_.get() == INVALID_HANDLE_VALUE) {
      throw_last_error("CreateFileW(NUL)");
    }
    startup_.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup_.StartupInfo.hStdInput = input_.get();
    startup_.StartupInfo.hStdOutput = output_.get();
    startup_.StartupInfo.hStdError = output_.get();

    SIZE_T bytes{};
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) {
      throw_last_error("InitializeProcThreadAttributeList(size)");
    }
    attributes_.resize(bytes);
    startup_.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(
        attributes_.data());
    if (!InitializeProcThreadAttributeList(
            startup_.lpAttributeList, 1, 0, &bytes)) {
      throw_last_error("InitializeProcThreadAttributeList");
    }
    initialized_ = true;
    handles_ = {input_.get(), output_.get()};
    if (!UpdateProcThreadAttribute(
            startup_.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles_.data(), sizeof(handles_), nullptr, nullptr)) {
      throw_last_error("UpdateProcThreadAttribute(handle list)");
    }
  }

  ~process_startup() {
    if (initialized_) {
      DeleteProcThreadAttributeList(startup_.lpAttributeList);
    }
  }
  process_startup(const process_startup&) = delete;
  process_startup& operator=(const process_startup&) = delete;

  [[nodiscard]] STARTUPINFOW* info() noexcept {
    return &startup_.StartupInfo;
  }
  [[nodiscard]] bool inherits_handles() const noexcept {
    return output_.get() != nullptr;
  }
  [[nodiscard]] bool extended() const noexcept { return initialized_; }

 private:
  STARTUPINFOEXW startup_{};
  std::vector<std::byte> attributes_;
  std::array<HANDLE, 2> handles_{};
  owned_handle input_;
  owned_handle output_;
  bool initialized_{};
};

}  // namespace

managed_process::managed_process(
    void* job, void* process, void* thread, void* completion_port,
    std::uint32_t process_id) noexcept
    : job_(job),
      process_(process),
      thread_(thread),
      completion_port_(completion_port),
      process_id_(process_id) {}

managed_process::~managed_process() {
  reset();
}

managed_process::managed_process(managed_process&& other) noexcept
    : job_(std::exchange(other.job_, nullptr)),
      process_(std::exchange(other.process_, nullptr)),
      thread_(std::exchange(other.thread_, nullptr)),
      completion_port_(std::exchange(other.completion_port_, nullptr)),
      process_id_(std::exchange(other.process_id_, 0)) {}

managed_process& managed_process::operator=(managed_process&& other) noexcept {
  if (this != &other) {
    reset();
    job_ = std::exchange(other.job_, nullptr);
    process_ = std::exchange(other.process_, nullptr);
    thread_ = std::exchange(other.thread_, nullptr);
    completion_port_ = std::exchange(other.completion_port_, nullptr);
    process_id_ = std::exchange(other.process_id_, 0);
  }
  return *this;
}

managed_process managed_process::prepare(const process_spec& spec) {
  if (spec.executable.empty() || !spec.executable.is_absolute()) {
    throw std::invalid_argument("managed executable path must be absolute");
  }
  if (!spec.working_directory.empty() && !spec.working_directory.is_absolute()) {
    throw std::invalid_argument("managed working directory must be absolute");
  }
  reject_embedded_nul(spec.executable.wstring(), "managed executable path");
  reject_embedded_nul(spec.working_directory.wstring(), "managed working directory");
  for (const auto& argument : spec.arguments) {
    reject_embedded_nul(argument, "managed argument");
  }
  auto command_line = build_command_line(spec);
  const auto working_directory = spec.working_directory.empty()
      ? spec.executable.parent_path()
      : spec.working_directory;
  std::wstring environment_block;
  void* environment = nullptr;
  if (!spec.environment.empty()) {
    environment_block = build_environment_block(spec.environment);
    environment = environment_block.data();
  }

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (job == nullptr) {
    throw_last_error("CreateJobObjectW");
  }

  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!SetInformationJobObject(
          job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    const auto status = GetLastError();
    CloseHandle(job);
    SetLastError(status);
    throw_last_error("SetInformationJobObject");
  }

  HANDLE completion_port = CreateIoCompletionPort(
      INVALID_HANDLE_VALUE, nullptr, 0, 1);
  if (completion_port == nullptr) {
    const auto status = GetLastError();
    CloseHandle(job);
    SetLastError(status);
    throw_last_error("CreateIoCompletionPort");
  }
  JOBOBJECT_ASSOCIATE_COMPLETION_PORT association{};
  association.CompletionKey = job;
  association.CompletionPort = completion_port;
  if (!SetInformationJobObject(
          job, JobObjectAssociateCompletionPortInformation, &association,
          sizeof(association))) {
    const auto status = GetLastError();
    CloseHandle(completion_port);
    CloseHandle(job);
    SetLastError(status);
    throw_last_error("SetInformationJobObject(completion port)");
  }

  process_startup startup(spec);
  DWORD creation_flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
  if (spec.no_window) {
    creation_flags |= CREATE_NO_WINDOW;
  }
  if (startup.extended()) {
    creation_flags |= EXTENDED_STARTUPINFO_PRESENT;
  }
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(
          spec.executable.c_str(), command_line.data(), nullptr, nullptr,
          startup.inherits_handles() ? TRUE : FALSE, creation_flags, environment,
          working_directory.empty() ? nullptr : working_directory.c_str(),
          startup.info(), &process)) {
    const auto status = GetLastError();
    CloseHandle(completion_port);
    CloseHandle(job);
    SetLastError(status);
    throw_last_error("CreateProcessW");
  }

  if (!AssignProcessToJobObject(job, process.hProcess)) {
    const auto status = GetLastError();
    const auto termination_requested = TerminateProcess(process.hProcess, status) != FALSE;
    const auto termination_status = termination_requested ? ERROR_SUCCESS : GetLastError();
    const auto wait_status = WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(completion_port);
    CloseHandle(job);
    if (!termination_requested && wait_status != WAIT_OBJECT_0) {
      SetLastError(termination_status);
      throw_last_error("TerminateProcess after AssignProcessToJobObject failure");
    }
    if (wait_status != WAIT_OBJECT_0) {
      throw std::runtime_error(
          "unassigned suspended process did not terminate within five seconds");
    }
    SetLastError(status);
    throw_last_error("AssignProcessToJobObject");
  }

  return {job, process.hProcess, process.hThread, completion_port, process.dwProcessId};
}

managed_process managed_process::start(const process_spec& spec) {
  auto process = prepare(spec);
  process.resume();
  return process;
}

void managed_process::resume() {
  if (thread_ == nullptr) {
    throw std::logic_error("managed process is not suspended");
  }
  if (ResumeThread(as_handle(thread_)) == static_cast<DWORD>(-1)) {
    const auto status = GetLastError();
    if (!terminate_and_wait_tree(status, std::chrono::seconds(5))) {
      throw std::runtime_error(
          "job did not terminate within five seconds after ResumeThread failure");
    }
    CloseHandle(as_handle(thread_));
    thread_ = nullptr;
    SetLastError(status);
    throw_last_error("ResumeThread");
  }
  CloseHandle(as_handle(thread_));
  thread_ = nullptr;
}

bool managed_process::suspended() const noexcept {
  return thread_ != nullptr;
}

std::uint32_t managed_process::process_id() const noexcept {
  return process_id_;
}

bool managed_process::root_running() const {
  if (process_ == nullptr) {
    return false;
  }
  const auto status = WaitForSingleObject(as_handle(process_), 0);
  if (status == WAIT_TIMEOUT) {
    return true;
  }
  if (status == WAIT_OBJECT_0) {
    return false;
  }
  throw_last_error("WaitForSingleObject");
}

std::optional<std::uint32_t> managed_process::wait_for_root(
    std::chrono::milliseconds timeout) const {
  if (process_ == nullptr) {
    throw std::logic_error("managed process is empty");
  }
  const auto status = WaitForSingleObject(as_handle(process_), wait_milliseconds(timeout));
  if (status == WAIT_TIMEOUT) {
    return std::nullopt;
  }
  if (status != WAIT_OBJECT_0) {
    throw_last_error("WaitForSingleObject");
  }
  DWORD exit_code{};
  if (!GetExitCodeProcess(as_handle(process_), &exit_code)) {
    throw_last_error("GetExitCodeProcess");
  }
  return exit_code;
}

bool managed_process::contains_process(std::uint32_t process_id) const {
  if (job_ == nullptr) {
    throw std::logic_error("managed process is empty");
  }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    if (GetLastError() == ERROR_INVALID_PARAMETER) {
      return false;
    }
    throw_last_error("OpenProcess");
  }
  BOOL contained{};
  const auto queried = IsProcessInJob(process, as_handle(job_), &contained);
  const auto status = GetLastError();
  CloseHandle(process);
  if (!queried) {
    SetLastError(status);
    throw_last_error("IsProcessInJob");
  }
  return contained != FALSE;
}

bool managed_process::wait_for_tree(std::chrono::milliseconds timeout) const {
  if (job_ == nullptr || completion_port_ == nullptr) {
    throw std::logic_error("managed process is empty");
  }
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  for (;;) {
    if (job_is_empty(as_handle(job_))) {
      return true;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return false;
    }
    DWORD message{};
    ULONG_PTR completion_key{};
    OVERLAPPED* overlapped{};
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    const auto wake_interval = std::min(remaining, std::chrono::milliseconds(250));
    if (!GetQueuedCompletionStatus(
            as_handle(completion_port_), &message, &completion_key, &overlapped,
            wait_milliseconds(wake_interval))) {
      if (GetLastError() == WAIT_TIMEOUT) {
        continue;
      }
      throw_last_error("GetQueuedCompletionStatus");
    }
  }
}

bool managed_process::terminate_and_wait_tree(
    std::uint32_t exit_code, std::chrono::milliseconds timeout) const {
  if (job_ == nullptr) {
    throw std::logic_error("managed process is empty");
  }
  if (!TerminateJobObject(as_handle(job_), exit_code)) {
    throw_last_error("TerminateJobObject");
  }
  return wait_for_tree(timeout);
}

void managed_process::reset() noexcept {
  if (job_ != nullptr && completion_port_ != nullptr) {
    try {
      if (!job_is_empty(as_handle(job_))) {
        TerminateJobObject(as_handle(job_), 1);
        (void)wait_for_tree(std::chrono::seconds(5));
      }
    } catch (...) {
      // Closing a kill-on-close job remains the final bounded cleanup action.
    }
  }
  if (job_ != nullptr) {
    CloseHandle(as_handle(job_));
  }
  if (thread_ != nullptr) {
    CloseHandle(as_handle(thread_));
  }
  if (completion_port_ != nullptr) {
    CloseHandle(as_handle(completion_port_));
  }
  if (process_ != nullptr) {
    CloseHandle(as_handle(process_));
  }
  job_ = nullptr;
  process_ = nullptr;
  thread_ = nullptr;
  completion_port_ = nullptr;
  process_id_ = 0;
}

}  // namespace ncm::launcher
