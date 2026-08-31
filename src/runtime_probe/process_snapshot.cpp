#include "ncm/runtime_probe/process_snapshot.hpp"

#include <WinSock2.h>
#include <Windows.h>
#include <Iphlpapi.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ncm::runtime_probe {
namespace {

class unique_handle {
 public:
  explicit unique_handle(HANDLE handle) noexcept : handle_(handle) {}
  ~unique_handle() {
    if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

  unique_handle(const unique_handle&) = delete;
  unique_handle& operator=(const unique_handle&) = delete;

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] bool valid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

 private:
  HANDLE handle_{};
};

[[nodiscard]] std::wstring lowercase(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
    if (character >= L'A' && character <= L'Z') {
      return static_cast<wchar_t>(character - L'A' + L'a');
    }
    return character;
  });
  return value;
}

[[nodiscard]] bool is_network_module(std::wstring_view module_name) {
  const auto name = lowercase(std::wstring(module_name));
  constexpr std::wstring_view markers[] = {
      L"winhttp", L"wininet", L"libcurl", L"libcef", L"netutils", L"ssl", L"crypto"};
  return std::ranges::any_of(markers, [&name](std::wstring_view marker) {
    return name.find(marker) != std::wstring::npos;
  });
}

[[nodiscard]] std::optional<std::map<std::uint32_t, std::uint32_t>> tcp_connection_counts() {
  DWORD size{};
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::vector<std::byte> buffer(size);
    const auto status = GetExtendedTcpTable(
        buffer.empty() ? nullptr : buffer.data(), &size,
        FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    if (status == ERROR_INSUFFICIENT_BUFFER) {
      continue;
    }
    if (status != NO_ERROR || buffer.empty()) {
      return std::nullopt;
    }

    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    std::map<std::uint32_t, std::uint32_t> counts;
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
      ++counts[table->table[index].dwOwningPid];
    }
    return counts;
  }
  return std::nullopt;
}

[[nodiscard]] std::filesystem::path process_image_path(std::uint32_t process_id) {
  const unique_handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id));
  if (!process.valid()) {
    return {};
  }

  std::wstring buffer(32768, L'\0');
  DWORD size = static_cast<DWORD>(buffer.size());
  if (!QueryFullProcessImageNameW(process.get(), 0, buffer.data(), &size)) {
    return {};
  }
  buffer.resize(size);
  return buffer;
}

struct module_snapshot {
  std::vector<std::wstring> modules;
  bool complete{};
};

[[nodiscard]] module_snapshot process_network_modules(std::uint32_t process_id) {
  HANDLE snapshot_handle = INVALID_HANDLE_VALUE;
  for (int attempt = 0; attempt < 3; ++attempt) {
    snapshot_handle = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
    if (snapshot_handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH) {
      break;
    }
  }
  const unique_handle snapshot(snapshot_handle);
  if (!snapshot.valid()) {
    return {};
  }

  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Module32FirstW(snapshot.get(), &entry)) {
    return {{}, GetLastError() == ERROR_NO_MORE_FILES};
  }

  module_snapshot result;
  do {
    if (is_network_module(entry.szModule)) {
      result.modules.emplace_back(entry.szModule);
    }
  } while (Module32NextW(snapshot.get(), &entry));

  result.complete = GetLastError() == ERROR_NO_MORE_FILES;

  std::ranges::sort(result.modules);
  result.modules.erase(
      std::unique(result.modules.begin(), result.modules.end()), result.modules.end());
  return result;
}

}  // namespace

std::vector<process_info> find_processes(std::wstring_view executable_name) {
  if (executable_name.empty()) {
    throw std::invalid_argument("process name cannot be empty");
  }

  const auto connection_counts = tcp_connection_counts();
  const unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot.valid()) {
    throw std::runtime_error("unable to create process snapshot");
  }

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (!Process32FirstW(snapshot.get(), &entry)) {
    throw std::runtime_error("unable to enumerate processes");
  }

  std::vector<process_info> processes;
  do {
    if (_wcsicmp(entry.szExeFile, std::wstring(executable_name).c_str()) != 0) {
      continue;
    }

    process_info info;
    info.process_id = entry.th32ProcessID;
    info.parent_process_id = entry.th32ParentProcessID;
    if (connection_counts.has_value()) {
      const auto count = connection_counts->find(info.process_id);
      info.ipv4_tcp_connections = count == connection_counts->end() ? 0U : count->second;
    }
    info.image_path = process_image_path(info.process_id);
    auto modules = process_network_modules(info.process_id);
    info.network_modules = std::move(modules.modules);
    info.network_modules_complete = modules.complete;
    processes.push_back(std::move(info));
  } while (Process32NextW(snapshot.get(), &entry));

  std::ranges::sort(processes, {}, &process_info::process_id);
  return processes;
}

}  // namespace ncm::runtime_probe
