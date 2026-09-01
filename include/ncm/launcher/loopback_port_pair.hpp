#pragma once

#include <cstdint>

namespace ncm::launcher {

class loopback_port_pair {
 public:
  loopback_port_pair() noexcept = default;
  ~loopback_port_pair();

  loopback_port_pair(const loopback_port_pair&) = delete;
  loopback_port_pair& operator=(const loopback_port_pair&) = delete;
  loopback_port_pair(loopback_port_pair&& other) noexcept;
  loopback_port_pair& operator=(loopback_port_pair&& other) noexcept;

  [[nodiscard]] static loopback_port_pair acquire_automatic();
  [[nodiscard]] static loopback_port_pair acquire_fixed(
      std::uint16_t http_port, std::uint16_t https_port);

  [[nodiscard]] std::uint16_t http_port() const noexcept;
  [[nodiscard]] std::uint16_t https_port() const noexcept;
  [[nodiscard]] bool active() const noexcept;
  void release() noexcept;

 private:
  [[nodiscard]] static loopback_port_pair acquire(
      std::uint16_t http_port, std::uint16_t https_port);
  loopback_port_pair(
      std::uintptr_t http_socket, std::uintptr_t https_socket,
      std::uint16_t http_port, std::uint16_t https_port) noexcept;

  static constexpr auto invalid_socket_ = static_cast<std::uintptr_t>(-1);
  std::uintptr_t http_socket_{invalid_socket_};
  std::uintptr_t https_socket_{invalid_socket_};
  std::uint16_t http_port_{};
  std::uint16_t https_port_{};
  bool winsock_started_{};
};

}  // namespace ncm::launcher
