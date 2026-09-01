#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <chrono>
#include <cwchar>
#include <string>
#include <string_view>
#include <thread>

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count == 3 && std::wstring_view(arguments[1]) == L"--exit") {
    wchar_t* end{};
    const auto code = std::wcstoul(arguments[2], &end, 10);
    return end != nullptr && *end == L'\0' ? static_cast<int>(code) : 125;
  }
  if (argument_count == 3 && std::wstring_view(arguments[1]) == L"--sleep") {
    wchar_t* end{};
    const auto milliseconds = std::wcstoul(arguments[2], &end, 10);
    if (end == nullptr || *end != L'\0') {
      return 125;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    return 0;
  }
  if (argument_count == 4 && std::wstring_view(arguments[1]) == L"--bind-pair") {
    wchar_t* http_end{};
    wchar_t* https_end{};
    const auto http_port = std::wcstoul(arguments[2], &http_end, 10);
    const auto https_port = std::wcstoul(arguments[3], &https_end, 10);
    if (http_end == nullptr || *http_end != L'\0' ||
        https_end == nullptr || *https_end != L'\0' ||
        http_port == 0 || http_port > 65535 || https_port == 0 || https_port > 65535) {
      return 125;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      return 125;
    }
    SOCKET sockets[2]{INVALID_SOCKET, INVALID_SOCKET};
    const unsigned long ports[2]{http_port, https_port};
    bool ready = true;
    for (int index = 0; index < 2; ++index) {
      sockets[index] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(static_cast<unsigned short>(ports[index]));
      if (sockets[index] == INVALID_SOCKET ||
          bind(sockets[index], reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
          listen(sockets[index], SOMAXCONN) == SOCKET_ERROR) {
        ready = false;
        break;
      }
    }
    for (const auto socket_handle : sockets) {
      if (socket_handle != INVALID_SOCKET) {
        closesocket(socket_handle);
      }
    }
    WSACleanup();
    return ready ? 0 : 125;
  }
  if (argument_count == 10 && std::wstring_view(arguments[1]) == L"--check-args" &&
      std::wstring_view(arguments[2]) == L"value with spaces" &&
      std::wstring_view(arguments[3]) == L"trailing\\" &&
      std::wstring_view(arguments[4]) == L"quote\"value" &&
      std::wstring_view(arguments[5]).empty() &&
      std::wstring_view(arguments[6]) == L"space trailing\\" &&
      std::wstring_view(arguments[7]) == L"two trailing\\\\" &&
      std::wstring_view(arguments[8]) == L"slash\\\"quote" &&
      std::wstring_view(arguments[9]) == L"after-empty") {
    return 0;
  }
  if (argument_count == 2 && std::wstring_view(arguments[1]) == L"--require-job") {
    BOOL in_job{};
    return IsProcessInJob(GetCurrentProcess(), nullptr, &in_job) && in_job ? 0 : 125;
  }
  if (argument_count == 2 && std::wstring_view(arguments[1]) == L"--spawn-child") {
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(),
                                          static_cast<DWORD>(executable.size()));
    if (length == 0 || length == executable.size()) {
      return 125;
    }
    executable.resize(length);
    auto command_line = L"\"" + executable + L"\" --sleep 30000";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(
            executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &child)) {
      return 125;
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }
  if (argument_count == 2 && std::wstring_view(arguments[1]) == L"--spawn-child-and-exit") {
    std::wstring executable(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(),
                                          static_cast<DWORD>(executable.size()));
    if (length == 0 || length == executable.size()) {
      return 125;
    }
    executable.resize(length);
    auto command_line = L"\"" + executable + L"\" --sleep 30000";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(
            executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
            nullptr, nullptr, &startup, &child)) {
      return 125;
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return 0;
  }
  return 125;
}
