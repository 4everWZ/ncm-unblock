#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <array>
#include <chrono>
#include <cwchar>
#include <string>
#include <string_view>
#include <thread>

int wmain(int argument_count, wchar_t** arguments) {
  if ((argument_count == 7 &&
       (std::wstring_view(arguments[1]) == L"--unm-sidecar" ||
        std::wstring_view(arguments[1]) == L"--unm-empty-pac" ||
        std::wstring_view(arguments[1]) == L"--unm-short-pac" ||
        std::wstring_view(arguments[1]) == L"--unm-slow-pac")) ||
      (argument_count == 8 && std::wstring_view(arguments[1]) == L"--unm-fail-once")) {
    int first_enforced = 2;
    if (std::wstring_view(arguments[1]) == L"--unm-fail-once") {
      first_enforced = 3;
      const auto marker = CreateFileW(
          arguments[2], GENERIC_WRITE, 0, nullptr, CREATE_NEW,
          FILE_ATTRIBUTE_TEMPORARY, nullptr);
      if (marker != INVALID_HANDLE_VALUE) {
        CloseHandle(marker);
        return 125;
      }
      if (GetLastError() != ERROR_FILE_EXISTS) {
        return 125;
      }
    }
    if (std::wstring_view(arguments[first_enforced]) != L"-a" ||
        std::wstring_view(arguments[first_enforced + 1]) != L"127.0.0.1" ||
        std::wstring_view(arguments[first_enforced + 2]) != L"-p" ||
        std::wstring_view(arguments[first_enforced + 4]) != L"-s") {
      return 125;
    }
    const std::wstring ports(arguments[first_enforced + 3]);
    const auto separator = ports.find(L':');
    if (separator == std::wstring::npos) {
      return 125;
    }
    wchar_t* http_end{};
    wchar_t* https_end{};
    const auto http_port = std::wcstoul(ports.substr(0, separator).c_str(), &http_end, 10);
    const auto https_port = std::wcstoul(ports.substr(separator + 1).c_str(), &https_end, 10);
    if (http_end == nullptr || *http_end != L'\0' ||
        https_end == nullptr || *https_end != L'\0' ||
        http_port == 0 || http_port > 65535 || https_port == 0 || https_port > 65535) {
      return 125;
    }
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      return 125;
    }
    SOCKET listeners[2]{INVALID_SOCKET, INVALID_SOCKET};
    const unsigned long selected_ports[2]{http_port, https_port};
    bool ready = true;
    for (int index = 0; index < 2; ++index) {
      listeners[index] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      address.sin_port = htons(static_cast<unsigned short>(selected_ports[index]));
      if (listeners[index] == INVALID_SOCKET ||
          bind(listeners[index], reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
          listen(listeners[index], SOMAXCONN) == SOCKET_ERROR) {
        ready = false;
        break;
      }
    }
    if (ready) {
      const DWORD timeout = 5000;
      setsockopt(
          listeners[0], SOL_SOCKET, SO_RCVTIMEO,
          reinterpret_cast<const char*>(&timeout), sizeof(timeout));
      const auto client = accept(listeners[0], nullptr, nullptr);
      if (client == INVALID_SOCKET) {
        ready = false;
      } else {
        std::array<char, 256> request{};
        const auto received = recv(client, request.data(), static_cast<int>(request.size()), 0);
        constexpr std::string_view valid_response =
            "HTTP/1.1 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\nPAC READY";
        constexpr std::string_view empty_response =
            "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        constexpr std::string_view short_response =
            "HTTP/1.1 200 OK\r\nContent-Length: 9\r\nConnection: close\r\n\r\nP";
        const auto mode = std::wstring_view(arguments[1]);
        const auto response = mode == L"--unm-empty-pac"
            ? empty_response
            : (mode == L"--unm-short-pac" ? short_response : valid_response);
        ready = received > 0 &&
            std::string_view(request.data(), static_cast<std::size_t>(received)).find("GET /proxy.pac ") !=
                std::string_view::npos;
        if (ready && mode == L"--unm-slow-pac") {
          for (const auto character : response) {
            if (send(client, &character, 1, 0) != 1) {
              ready = false;
              break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
          }
        } else if (ready) {
          ready = send(client, response.data(), static_cast<int>(response.size()), 0) ==
              static_cast<int>(response.size());
        }
        closesocket(client);
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    for (const auto listener : listeners) {
      if (listener != INVALID_SOCKET) {
        closesocket(listener);
      }
    }
    WSACleanup();
    return ready ? 0 : 125;
  }
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
  if (argument_count == 2 && std::wstring_view(arguments[1]) == L"--write-stdio") {
    constexpr std::string_view output = "managed stdout\n";
    constexpr std::string_view error = "managed stderr\n";
    DWORD written{};
    const auto output_ok = WriteFile(
        GetStdHandle(STD_OUTPUT_HANDLE), output.data(),
        static_cast<DWORD>(output.size()), &written, nullptr) &&
        written == output.size();
    const auto error_ok = WriteFile(
        GetStdHandle(STD_ERROR_HANDLE), error.data(),
        static_cast<DWORD>(error.size()), &written, nullptr) &&
        written == error.size();
    return output_ok && error_ok ? 0 : 125;
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
