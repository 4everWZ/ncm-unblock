#include "ncm/launcher/ncm_session.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cwctype>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace ncm::launcher {
namespace {

[[noreturn]] void throw_last_error(const char* operation) {
  throw std::system_error(
      static_cast<int>(GetLastError()), std::system_category(), operation);
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
    throw_last_error("GetProcessTimes");
  }
  return file_time_value(creation);
}

struct candidate_process {
  std::uint32_t process_id{};
  std::uint32_t parent_process_id{};
  std::uint64_t creation_time{};
  std::filesystem::path path;
};

[[nodiscard]] std::vector<candidate_process> snapshot_candidates(
    const std::filesystem::path& executable,
    const std::filesystem::path& reporter) {
  const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    throw_last_error("CreateToolhelp32Snapshot");
  }
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot, &entry)) {
    const auto status = GetLastError();
    CloseHandle(snapshot);
    SetLastError(status);
    throw_last_error("Process32FirstW");
  }

  const auto executable_name = executable.filename().wstring();
  const auto reporter_name = reporter.filename().wstring();
  std::vector<candidate_process> result;
  do {
    if (_wcsicmp(entry.szExeFile, executable_name.c_str()) != 0 &&
        _wcsicmp(entry.szExeFile, reporter_name.c_str()) != 0) {
      continue;
    }
    const auto process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
    if (process == nullptr) {
      continue;
    }
    std::wstring path(32768, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (QueryFullProcessImageNameW(process, 0, path.data(), &length)) {
      try {
        path.resize(length);
        const auto resolved = std::filesystem::path(path).lexically_normal();
        if (equal_path(resolved, executable) || equal_path(resolved, reporter)) {
          result.push_back({
              entry.th32ProcessID, entry.th32ParentProcessID,
              process_creation_time(process), resolved});
        }
      } catch (...) {
        CloseHandle(process);
        throw;
      }
    }
    CloseHandle(process);
  } while (Process32NextW(snapshot, &entry));
  CloseHandle(snapshot);
  return result;
}

}  // namespace

ncm_session::ncm_session(
    void* root_process, std::uint32_t root_process_id,
    std::uint64_t root_creation_time, std::filesystem::path executable)
    : root_process_(root_process),
      root_process_id_(root_process_id),
      executable_(std::move(executable)),
      reporter_(executable_.parent_path() / L"cloudmusic_reporter.exe"),
      tracked_{{root_process_id, root_creation_time}} {}

ncm_session::~ncm_session() {
  reset();
}

ncm_session::ncm_session(ncm_session&& other) noexcept
    : root_process_(std::exchange(other.root_process_, nullptr)),
      root_process_id_(std::exchange(other.root_process_id_, 0)),
      executable_(std::move(other.executable_)),
      reporter_(std::move(other.reporter_)),
      tracked_(std::move(other.tracked_)) {}

ncm_session& ncm_session::operator=(ncm_session&& other) noexcept {
  if (this != &other) {
    reset();
    root_process_ = std::exchange(other.root_process_, nullptr);
    root_process_id_ = std::exchange(other.root_process_id_, 0);
    executable_ = std::move(other.executable_);
    reporter_ = std::move(other.reporter_);
    tracked_ = std::move(other.tracked_);
  }
  return *this;
}

bool ncm_session::target_running(const std::filesystem::path& executable) {
  if (executable.empty() || !executable.is_absolute()) {
    throw std::invalid_argument("NCM executable path must be absolute");
  }
  const auto normalized = executable.lexically_normal();
  return !snapshot_candidates(
              normalized, normalized.parent_path() / L"cloudmusic_reporter.exe")
              .empty();
}

ncm_session ncm_session::launch(const std::filesystem::path& executable) {
  if (executable.empty() || !executable.is_absolute()) {
    throw std::invalid_argument("NCM executable path must be absolute");
  }
  const auto normalized = executable.lexically_normal();
  auto command_line = L"\"" + normalized.wstring() + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(
          normalized.c_str(), command_line.data(), nullptr, nullptr, FALSE,
          CREATE_UNICODE_ENVIRONMENT, nullptr, normalized.parent_path().c_str(),
          &startup, &process)) {
    throw_last_error("CreateProcessW(NCM)");
  }
  CloseHandle(process.hThread);
  try {
    return {
        process.hProcess, process.dwProcessId,
        process_creation_time(process.hProcess), normalized};
  } catch (...) {
    CloseHandle(process.hProcess);
    throw;
  }
}

bool ncm_session::active() {
  if (root_process_ == nullptr) {
    throw std::logic_error("NCM session is empty");
  }
  const auto candidates = snapshot_candidates(executable_, reporter_);
  std::vector<std::uint32_t> live_tracked;
  for (const auto& candidate : candidates) {
    if (std::any_of(tracked_.begin(), tracked_.end(), [&](const auto& tracked) {
          return tracked.process_id == candidate.process_id &&
              tracked.creation_time == candidate.creation_time;
        })) {
      live_tracked.push_back(candidate.process_id);
    }
  }

  bool changed = true;
  while (changed) {
    changed = false;
    for (const auto& candidate : candidates) {
      if (std::find(live_tracked.begin(), live_tracked.end(), candidate.process_id) !=
          live_tracked.end()) {
        continue;
      }
      const auto parent = std::find_if(
          tracked_.begin(), tracked_.end(), [&](const auto& tracked) {
            return tracked.process_id == candidate.parent_process_id &&
                tracked.creation_time <= candidate.creation_time;
          });
      if (parent == tracked_.end()) {
        continue;
      }
      tracked_.push_back({candidate.process_id, candidate.creation_time});
      live_tracked.push_back(candidate.process_id);
      changed = true;
    }
  }
  return !live_tracked.empty();
}

std::uint32_t ncm_session::root_process_id() const noexcept {
  return root_process_id_;
}

void ncm_session::reset() noexcept {
  if (root_process_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(root_process_));
  }
  root_process_ = nullptr;
  root_process_id_ = 0;
  executable_.clear();
  reporter_.clear();
  tracked_.clear();
}

}  // namespace ncm::launcher
