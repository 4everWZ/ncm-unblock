#include "ncm/proxy_observer/request_observation.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cwchar>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

class winsock_session {
 public:
  winsock_session() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
      throw std::runtime_error("WSAStartup failed");
    }
  }
  ~winsock_session() { WSACleanup(); }
  winsock_session(const winsock_session&) = delete;
  winsock_session& operator=(const winsock_session&) = delete;
};

class unique_socket {
 public:
  explicit unique_socket(SOCKET socket = INVALID_SOCKET) noexcept : socket_(socket) {}
  ~unique_socket() {
    if (socket_ != INVALID_SOCKET) {
      closesocket(socket_);
    }
  }
  unique_socket(const unique_socket&) = delete;
  unique_socket& operator=(const unique_socket&) = delete;
  [[nodiscard]] SOCKET get() const noexcept { return socket_; }
  [[nodiscard]] bool valid() const noexcept { return socket_ != INVALID_SOCKET; }

 private:
  SOCKET socket_;
};

struct options {
  std::uint16_t port{};
  std::uint32_t max_events{20};
  std::uint32_t idle_timeout_ms{30000};
};

[[nodiscard]] std::uint32_t parse_unsigned(std::wstring_view value, std::uint32_t maximum) {
  const std::wstring owned(value);
  wchar_t* end{};
  errno = 0;
  const auto parsed = std::wcstoul(owned.c_str(), &end, 10);
  if (errno != 0 || end == nullptr || *end != L'\0' || parsed > maximum) {
    throw std::invalid_argument("invalid numeric argument");
  }
  return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] options parse_options(int argument_count, wchar_t** arguments) {
  options result;
  for (int index = 1; index < argument_count; ++index) {
    const std::wstring_view argument(arguments[index]);
    if (index + 1 >= argument_count) {
      throw std::invalid_argument("missing option value");
    }
    const std::wstring_view value(arguments[++index]);
    if (argument == L"--port") {
      result.port = static_cast<std::uint16_t>(parse_unsigned(value, 65535));
    } else if (argument == L"--max-events") {
      result.max_events = parse_unsigned(value, 10000);
      if (result.max_events == 0) {
        throw std::invalid_argument("max-events must be positive");
      }
    } else if (argument == L"--idle-timeout-ms") {
      result.idle_timeout_ms = parse_unsigned(value, 3600000);
      if (result.idle_timeout_ms == 0) {
        throw std::invalid_argument("idle-timeout-ms must be positive");
      }
    } else {
      throw std::invalid_argument("unknown option");
    }
  }
  return result;
}

[[nodiscard]] std::string receive_headers(SOCKET socket) {
  constexpr std::size_t maximum_headers = 16384;
  DWORD timeout_ms = 3000;
  if (setsockopt(
          socket, SOL_SOCKET, SO_RCVTIMEO,
          reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) == SOCKET_ERROR) {
    throw std::runtime_error("unable to bound client receive time");
  }

  std::string headers;
  headers.reserve(4096);
  char buffer[1024]{};
  while (headers.size() < maximum_headers) {
    const auto received = recv(socket, buffer, sizeof(buffer), 0);
    if (received <= 0) {
      break;
    }
    headers.append(buffer, static_cast<std::size_t>(received));
    if (const auto end = headers.find("\r\n\r\n"); end != std::string::npos) {
      headers.resize(end + 4);
      break;
    }
  }
  if (headers.size() > maximum_headers) {
    headers.resize(maximum_headers);
  }
  return headers;
}

void reject_request(SOCKET socket) noexcept {
  constexpr std::string_view response =
      "HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
  static_cast<void>(send(socket, response.data(), static_cast<int>(response.size()), 0));
  shutdown(socket, SD_BOTH);
}

[[nodiscard]] std::uint16_t bound_port(SOCKET socket) {
  sockaddr_in address{};
  int size = sizeof(address);
  if (getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == SOCKET_ERROR) {
    throw std::runtime_error("getsockname failed");
  }
  return ntohs(address.sin_port);
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    const auto settings = parse_options(argument_count, arguments);
    const winsock_session winsock;
    const unique_socket listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!listener.valid()) {
      throw std::runtime_error("socket failed");
    }

    BOOL exclusive = TRUE;
    if (setsockopt(
            listener.get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
            reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
      throw std::runtime_error("unable to reserve loopback socket exclusively");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(settings.port);
    if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
      throw std::runtime_error("loopback bind failed");
    }
    if (listen(listener.get(), SOMAXCONN) == SOCKET_ERROR) {
      throw std::runtime_error("listen failed");
    }

    std::cout << "observer-listening address=127.0.0.1 port=" << bound_port(listener.get()) << '\n';
    std::cout.flush();

    auto last_activity = GetTickCount64();
    std::uint32_t event_count{};
    while (event_count < settings.max_events) {
      const auto elapsed = GetTickCount64() - last_activity;
      if (elapsed >= settings.idle_timeout_ms) {
        std::cout << "observer-stopped reason=idle-timeout events=" << event_count << '\n';
        return 0;
      }

      fd_set read_set;
      FD_ZERO(&read_set);
      FD_SET(listener.get(), &read_set);
      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = 250000;
      const auto ready = select(0, &read_set, nullptr, nullptr, &timeout);
      if (ready == SOCKET_ERROR) {
        throw std::runtime_error("select failed");
      }
      if (ready == 0) {
        continue;
      }

      const unique_socket client(accept(listener.get(), nullptr, nullptr));
      if (!client.valid()) {
        throw std::runtime_error("accept failed");
      }
      const auto headers = receive_headers(client.get());
      const auto observation = ncm::proxy_observer::observe_request(headers);
      ++event_count;
      last_activity = GetTickCount64();
      std::cout << "proxy-event index=" << event_count
                << " method=" << (observation.method.empty() ? "invalid" : observation.method)
                << " target-form=" << ncm::proxy_observer::to_string(observation.form)
                << " scheme=" << (observation.scheme.empty() ? "unavailable" : observation.scheme)
                << " destination=" << ncm::proxy_observer::to_string(observation.destination)
                << " port=";
      if (observation.port.has_value()) {
        std::cout << *observation.port;
      } else {
        std::cout << "unavailable";
      }
      std::cout << " headers-complete=" << (observation.headers_complete ? "true" : "false") << '\n';
      std::cout.flush();
      reject_request(client.get());
    }

    std::cout << "observer-stopped reason=max-events events=" << event_count << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    std::cerr << "usage: ncm_proxy_observer [--port <0-65535>] [--max-events <n>] "
                 "[--idle-timeout-ms <n>]\n";
    return 1;
  }
}
