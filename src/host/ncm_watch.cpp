#include "ncm/host/ncm_watch.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ncm::host {
namespace {

static_assert(sizeof(void*) == 8, "ncm_watch requires an x64 build");

constexpr ULONG k_process_basic_information = 0;
constexpr ULONG k_process_command_line_information = 60;
constexpr LONG k_length_mismatch = static_cast<LONG>(0xC0000004L);
constexpr std::size_t k_peb_process_parameters = 0x20;
constexpr std::size_t k_rtl_user_process_parameters_command_line = 0x70;

struct unicode_string {
  USHORT length;
  USHORT maximum_length;
  PWSTR buffer;
};

struct process_basic_information {
  LONG exit_status;
  PVOID peb_base_address;
  ULONG_PTR affinity_mask;
  LONG base_priority;
  ULONG_PTR unique_process_id;
  ULONG_PTR inherited_from_unique_process_id;
};

using nt_query_information_process = LONG(NTAPI*)(
    HANDLE, ULONG, PVOID, ULONG, PULONG);

[[nodiscard]] HANDLE as_handle(void* value) noexcept {
  return static_cast<HANDLE>(value);
}

[[nodiscard]] bool equal_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) noexcept {
  return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

[[nodiscard]] std::uint64_t file_time_value(const FILETIME& value) noexcept {
  return static_cast<std::uint64_t>(value.dwLowDateTime) |
      (static_cast<std::uint64_t>(value.dwHighDateTime) << 32U);
}

[[nodiscard]] std::uint64_t process_creation_time(HANDLE process) {
  FILETIME creation{};
  FILETIME exit{};
  FILETIME kernel{};
  FILETIME user{};
  if (!GetProcessTimes(process, &creation, &exit, &kernel, &user)) {
    return 0;
  }
  return file_time_value(creation);
}

[[nodiscard]] nt_query_information_process query_information_process() {
  const auto ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll == nullptr) {
    return nullptr;
  }
  return reinterpret_cast<nt_query_information_process>(
      GetProcAddress(ntdll, "NtQueryInformationProcess"));
}

[[nodiscard]] bool read_memory(
    HANDLE process, const void* address, void* buffer, std::size_t size) {
  SIZE_T copied{};
  return ReadProcessMemory(
             process, address, buffer, size, &copied) != FALSE &&
      copied == size;
}

[[nodiscard]] std::optional<std::wstring> command_line_via_peb(HANDLE process) {
  const auto query = query_information_process();
  if (query == nullptr) {
    return std::nullopt;
  }
  process_basic_information information{};
  const auto status = query(
      process, k_process_basic_information, &information,
      sizeof(information), nullptr);
  if (status != 0 || information.peb_base_address == nullptr) {
    return std::nullopt;
  }
  void* parameters{};
  if (!read_memory(
          process,
          static_cast<const std::byte*>(information.peb_base_address) +
              k_peb_process_parameters,
          &parameters, sizeof(parameters)) ||
      parameters == nullptr) {
    return std::nullopt;
  }
  unicode_string command_line{};
  if (!read_memory(
          process,
          static_cast<const std::byte*>(parameters) +
              k_rtl_user_process_parameters_command_line,
          &command_line, sizeof(command_line))) {
    return std::nullopt;
  }
  if (command_line.buffer == nullptr || command_line.length == 0) {
    return std::wstring{};
  }
  std::wstring text(command_line.length / sizeof(wchar_t), L'\0');
  if (!read_memory(
          process, command_line.buffer, text.data(), command_line.length)) {
    return std::nullopt;
  }
  return text;
}

[[nodiscard]] std::optional<std::wstring> command_line_via_query(HANDLE process) {
  const auto query = query_information_process();
  if (query == nullptr) {
    return std::nullopt;
  }
  ULONG needed{};
  auto status = query(
      process, k_process_command_line_information, nullptr, 0, &needed);
  if (status != k_length_mismatch || needed < sizeof(unicode_string)) {
    needed = 4096;
  }
  std::vector<std::byte> buffer(needed);
  status = query(
      process, k_process_command_line_information, buffer.data(),
      static_cast<ULONG>(buffer.size()), &needed);
  if (status != 0) {
    if (needed <= buffer.size()) {
      return std::nullopt;
    }
    buffer.assign(needed, std::byte{});
    status = query(
        process, k_process_command_line_information, buffer.data(),
        static_cast<ULONG>(buffer.size()), &needed);
    if (status != 0) {
      return std::nullopt;
    }
  }
  const auto* unicode = reinterpret_cast<const unicode_string*>(buffer.data());
  if (unicode->buffer == nullptr || unicode->length == 0) {
    return std::wstring{};
  }
  return std::wstring(
      unicode->buffer, unicode->length / sizeof(wchar_t));
}

[[nodiscard]] std::optional<std::wstring> command_line_of(HANDLE process) {
  if (const auto via_peb = command_line_via_peb(process); via_peb.has_value()) {
    return via_peb;
  }
  return command_line_via_query(process);
}

[[nodiscard]] bool is_cef_utility(std::wstring_view command_line) noexcept {
  return command_line.find(L"--type=") != std::wstring_view::npos;
}

[[nodiscard]] std::optional<std::filesystem::path> image_path(HANDLE process) {
  std::wstring path(32768, L'\0');
  DWORD length = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
    return std::nullopt;
  }
  path.resize(length);
  return std::filesystem::path(path).lexically_normal();
}

[[nodiscard]] bool parent_is_same_image(
    std::uint32_t parent_process_id, const std::filesystem::path& executable) {
  if (parent_process_id == 0) {
    return false;
  }
  const auto parent = OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parent_process_id);
  if (parent == nullptr) {
    return false;
  }
  const auto path = image_path(parent);
  CloseHandle(parent);
  return path.has_value() && equal_path(*path, executable);
}

struct candidate {
  std::uint32_t process_id{};
  std::uint64_t creation_time{};
  HANDLE process{};
};

void close_candidate(candidate& value) noexcept {
  if (value.process != nullptr) {
    CloseHandle(value.process);
    value.process = nullptr;
  }
}

[[nodiscard]] HANDLE open_candidate(std::uint32_t process_id) {
  auto process = OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE,
      FALSE, process_id);
  if (process != nullptr) {
    return process;
  }
  return OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, process_id);
}

}  // namespace

ncm_watch::ncm_watch(
    void* process, std::uint32_t process_id, std::uint64_t creation_time,
    std::filesystem::path executable) noexcept
    : process_(process),
      process_id_(process_id),
      creation_time_(creation_time),
      executable_(std::move(executable)) {}

ncm_watch::~ncm_watch() {
  reset();
}

ncm_watch::ncm_watch(ncm_watch&& other) noexcept
    : process_(std::exchange(other.process_, nullptr)),
      process_id_(std::exchange(other.process_id_, 0)),
      creation_time_(std::exchange(other.creation_time_, 0)),
      executable_(std::move(other.executable_)) {}

ncm_watch& ncm_watch::operator=(ncm_watch&& other) noexcept {
  if (this != &other) {
    reset();
    process_ = std::exchange(other.process_, nullptr);
    process_id_ = std::exchange(other.process_id_, 0);
    creation_time_ = std::exchange(other.creation_time_, 0);
    executable_ = std::move(other.executable_);
  }
  return *this;
}

std::optional<ncm_watch> ncm_watch::attach(
    const std::filesystem::path& executable) {
  if (executable.empty() || !executable.is_absolute()) {
    return std::nullopt;
  }
  const auto normalized = executable.lexically_normal();
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return std::nullopt;
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) {
    CloseHandle(snapshot);
    return std::nullopt;
  }

  const auto expected_name = normalized.filename().wstring();
  candidate best{};
  do {
    if (_wcsicmp(entry.szExeFile, expected_name.c_str()) != 0) {
      continue;
    }
    const auto process = open_candidate(entry.th32ProcessID);
    if (process == nullptr) {
      continue;
    }
    const auto path = image_path(process);
    if (!path.has_value() || !equal_path(*path, normalized)) {
      CloseHandle(process);
      continue;
    }
    const auto command_line = command_line_of(process);
    const auto utility = command_line.has_value()
        ? is_cef_utility(*command_line)
        : parent_is_same_image(entry.th32ParentProcessID, normalized);
    if (utility) {
      CloseHandle(process);
      continue;
    }
    const auto created = process_creation_time(process);
    if (best.process != nullptr &&
        (created == 0 || (best.creation_time != 0 && best.creation_time <= created))) {
      CloseHandle(process);
      continue;
    }
    close_candidate(best);
    best = {entry.th32ProcessID, created, process};
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);

  if (best.process == nullptr) {
    return std::nullopt;
  }
  return ncm_watch{
      best.process, best.process_id, best.creation_time, normalized};
}

bool ncm_watch::alive() const {
  if (process_ == nullptr) {
    return false;
  }
  return WaitForSingleObject(as_handle(process_), 0) == WAIT_TIMEOUT;
}

std::uint32_t ncm_watch::process_id() const noexcept {
  return process_id_;
}

void* ncm_watch::wait_handle() const noexcept {
  return process_;
}

void ncm_watch::reset() noexcept {
  if (process_ != nullptr) {
    CloseHandle(as_handle(process_));
  }
  process_ = nullptr;
  process_id_ = 0;
  creation_time_ = 0;
  executable_.clear();
}

}  // namespace ncm::host
