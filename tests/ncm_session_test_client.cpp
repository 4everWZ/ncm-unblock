#include <Windows.h>

#include <chrono>
#include <string>
#include <string_view>
#include <thread>

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count == 2 && std::wstring_view(arguments[1]) == L"--child") {
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    return 0;
  }
  if (argument_count != 1) return 125;

  std::wstring executable(32768, L'\0');
  const auto length = GetModuleFileNameW(
      nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (length == 0 || length >= executable.size()) return 125;
  executable.resize(length);
  auto command = L"\"" + executable + L"\" --child";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  if (!CreateProcessW(
          executable.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
          nullptr, nullptr, &startup, &child)) {
    return 125;
  }
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  return 0;
}
