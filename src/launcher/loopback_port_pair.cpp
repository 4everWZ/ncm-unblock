#include "ncm/launcher/loopback_port_pair.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>

#include <stdexcept>
#include <system_error>
#include <utility>

namespace ncm::launcher {
namespace {

[[noreturn]] void throw_winsock_error(const char* operation, int status) {
  throw std::system_error(status, std::system_category(), operation);
}

[[nodiscard]] SOCKET acquire_socket(std::uint16_t requested_port) {
  const auto socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_handle == INVALID_SOCKET) {
    throw_winsock_error("socket", WSAGetLastError());
  }

  BOOL exclusive = TRUE;
  if (setsockopt(
          socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
          reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
    const auto status = WSAGetLastError();
    closesocket(socket_handle);
    throw_winsock_error("setsockopt(SO_EXCLUSIVEADDRUSE)", status);
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(requested_port);
  if (bind(
          socket_handle, reinterpret_cast<const sockaddr*>(&address),
          sizeof(address)) == SOCKET_ERROR) {
    const auto status = WSAGetLastError();
    closesocket(socket_handle);
    throw_winsock_error("bind(127.0.0.1)", status);
  }
  return socket_handle;
}

[[nodiscard]] std::uint16_t bound_port(SOCKET socket_handle) {
  sockaddr_in address{};
  int address_length = sizeof(address);
  if (getsockname(
          socket_handle, reinterpret_cast<sockaddr*>(&address),
          &address_length) == SOCKET_ERROR) {
    throw_winsock_error("getsockname", WSAGetLastError());
  }
  return ntohs(address.sin_port);
}

}  // namespace

loopback_port_pair loopback_port_pair::acquire(
    std::uint16_t http_port, std::uint16_t https_port) {
  WSADATA data{};
  const auto startup_status = WSAStartup(MAKEWORD(2, 2), &data);
  if (startup_status != 0) {
    throw_winsock_error("WSAStartup", startup_status);
  }

  SOCKET http_socket = INVALID_SOCKET;
  SOCKET https_socket = INVALID_SOCKET;
  try {
    http_socket = acquire_socket(http_port);
    https_socket = acquire_socket(https_port);
    const auto selected_http = bound_port(http_socket);
    const auto selected_https = bound_port(https_socket);
    return {
        static_cast<std::uintptr_t>(http_socket),
        static_cast<std::uintptr_t>(https_socket), selected_http, selected_https};
  } catch (...) {
    if (https_socket != INVALID_SOCKET) {
      closesocket(https_socket);
    }
    if (http_socket != INVALID_SOCKET) {
      closesocket(http_socket);
    }
    WSACleanup();
    throw;
  }
}

loopback_port_pair::loopback_port_pair(
    std::uintptr_t http_socket, std::uintptr_t https_socket,
    std::uint16_t http_port, std::uint16_t https_port) noexcept
    : http_socket_(http_socket),
      https_socket_(https_socket),
      http_port_(http_port),
      https_port_(https_port),
      winsock_started_(true) {}

loopback_port_pair::~loopback_port_pair() {
  release();
}

loopback_port_pair::loopback_port_pair(loopback_port_pair&& other) noexcept
    : http_socket_(std::exchange(other.http_socket_, invalid_socket_)),
      https_socket_(std::exchange(other.https_socket_, invalid_socket_)),
      http_port_(std::exchange(other.http_port_, std::uint16_t{})),
      https_port_(std::exchange(other.https_port_, std::uint16_t{})),
      winsock_started_(std::exchange(other.winsock_started_, false)) {}

loopback_port_pair& loopback_port_pair::operator=(loopback_port_pair&& other) noexcept {
  if (this != &other) {
    release();
    http_socket_ = std::exchange(other.http_socket_, invalid_socket_);
    https_socket_ = std::exchange(other.https_socket_, invalid_socket_);
    http_port_ = std::exchange(other.http_port_, std::uint16_t{});
    https_port_ = std::exchange(other.https_port_, std::uint16_t{});
    winsock_started_ = std::exchange(other.winsock_started_, false);
  }
  return *this;
}

loopback_port_pair loopback_port_pair::acquire_automatic() {
  return acquire(0, 0);
}

loopback_port_pair loopback_port_pair::acquire_fixed(
    std::uint16_t http_port, std::uint16_t https_port) {
  if (http_port == 0 || https_port == 0 || http_port == https_port) {
    throw std::invalid_argument("fixed HTTP/HTTPS ports must be distinct and nonzero");
  }
  return acquire(http_port, https_port);
}

std::uint16_t loopback_port_pair::http_port() const noexcept {
  return http_port_;
}

std::uint16_t loopback_port_pair::https_port() const noexcept {
  return https_port_;
}

bool loopback_port_pair::active() const noexcept {
  return winsock_started_;
}

void loopback_port_pair::release() noexcept {
  if (http_socket_ != invalid_socket_) {
    closesocket(static_cast<SOCKET>(http_socket_));
    http_socket_ = invalid_socket_;
  }
  if (https_socket_ != invalid_socket_) {
    closesocket(static_cast<SOCKET>(https_socket_));
    https_socket_ = invalid_socket_;
  }
  if (winsock_started_) {
    WSACleanup();
    winsock_started_ = false;
  }
}

}  // namespace ncm::launcher
