#include <WinSock2.h>
#include <WS2tcpip.h>

#include "ncm/launcher/loopback_port_pair.hpp"
#include "ncm/launcher/managed_process.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] SOCKET try_bind(std::uint16_t port) {
  const auto socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_handle == INVALID_SOCKET) {
    throw std::runtime_error("unable to create competing socket");
  }
  BOOL exclusive = TRUE;
  if (setsockopt(
          socket_handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
          reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
    closesocket(socket_handle);
    throw std::runtime_error("unable to make competing socket exclusive");
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (bind(
          socket_handle, reinterpret_cast<const sockaddr*>(&address),
          sizeof(address)) == SOCKET_ERROR) {
    closesocket(socket_handle);
    return INVALID_SOCKET;
  }
  return socket_handle;
}

void test_automatic_lease_blocks_competitors() {
  auto lease = ncm::launcher::loopback_port_pair::acquire_automatic();
  require(lease.active(), "automatic loopback lease is inactive");
  require(lease.http_port() != 0 && lease.https_port() != 0,
          "automatic loopback lease selected port zero");
  require(lease.http_port() != lease.https_port(),
          "automatic loopback lease selected one port twice");
  require(try_bind(lease.http_port()) == INVALID_SOCKET,
          "HTTP lease allowed a competing exclusive bind");
  require(try_bind(lease.https_port()) == INVALID_SOCKET,
          "HTTPS lease allowed a competing exclusive bind");

  const auto http_port = lease.http_port();
  const auto https_port = lease.https_port();
  lease.release();
  require(!lease.active(), "released loopback lease remained active");
  const auto http_socket = try_bind(http_port);
  const auto https_socket = try_bind(https_port);
  require(http_socket != INVALID_SOCKET && https_socket != INVALID_SOCKET,
          "released loopback ports could not be rebound");
  closesocket(http_socket);
  closesocket(https_socket);
}

void test_fixed_lease_and_collision() {
  auto selection = ncm::launcher::loopback_port_pair::acquire_automatic();
  const auto http_port = selection.http_port();
  const auto https_port = selection.https_port();
  selection.release();

  auto lease = ncm::launcher::loopback_port_pair::acquire_fixed(http_port, https_port);
  require(lease.http_port() == http_port && lease.https_port() == https_port,
          "fixed loopback lease changed configured ports");
  bool collision_rejected{};
  try {
    (void)ncm::launcher::loopback_port_pair::acquire_fixed(http_port, https_port);
  } catch (const std::system_error&) {
    collision_rejected = true;
  }
  require(collision_rejected, "fixed loopback collision was accepted");

  bool invalid_pair_rejected{};
  try {
    (void)ncm::launcher::loopback_port_pair::acquire_fixed(http_port, http_port);
  } catch (const std::invalid_argument&) {
    invalid_pair_rejected = true;
  }
  require(invalid_pair_rejected, "identical fixed ports were accepted");

  lease.release();
  const auto https_competitor = try_bind(https_port);
  require(https_competitor != INVALID_SOCKET, "unable to hold HTTPS collision fixture");
  bool second_bind_collision_rejected{};
  try {
    (void)ncm::launcher::loopback_port_pair::acquire_fixed(http_port, https_port);
  } catch (const std::system_error&) {
    second_bind_collision_rejected = true;
  }
  closesocket(https_competitor);
  require(second_bind_collision_rejected, "HTTPS-side fixed collision was accepted");
  const auto rolled_back_http = try_bind(http_port);
  require(rolled_back_http != INVALID_SOCKET,
          "failed HTTPS acquisition did not release its HTTP lease");
  closesocket(rolled_back_http);
}

void test_release_then_resume_handoff(const std::filesystem::path& child) {
  auto lease = ncm::launcher::loopback_port_pair::acquire_automatic();
  const auto http_port = lease.http_port();
  const auto https_port = lease.https_port();
  ncm::launcher::process_spec spec{
      child,
      child.parent_path(),
      {L"--bind-pair", std::to_wstring(http_port), std::to_wstring(https_port)}};
  auto process = ncm::launcher::managed_process::prepare(spec);
  require(process.suspended(), "prepared sidecar was not suspended");
  require(process.contains_process(process.process_id()),
          "prepared sidecar was not assigned to its private job");
  require(try_bind(http_port) == INVALID_SOCKET && try_bind(https_port) == INVALID_SOCKET,
          "prepared sidecar did not retain both launch leases");

  lease.release();
  process.resume();
  require(!process.suspended(), "resumed sidecar remained suspended");
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 0,
          "sidecar could not bind the released loopback pair");
  require(process.wait_for_tree(std::chrono::seconds(5)),
          "sidecar tree did not become empty after handoff test");
}

void test_handoff_collision_is_visible(const std::filesystem::path& child) {
  auto lease = ncm::launcher::loopback_port_pair::acquire_automatic();
  const auto http_port = lease.http_port();
  const auto https_port = lease.https_port();
  ncm::launcher::process_spec spec{
      child,
      child.parent_path(),
      {L"--bind-pair", std::to_wstring(http_port), std::to_wstring(https_port)}};
  auto process = ncm::launcher::managed_process::prepare(spec);
  lease.release();
  auto competitor = ncm::launcher::loopback_port_pair::acquire_fixed(http_port, https_port);
  process.resume();
  const auto exit_code = process.wait_for_root(std::chrono::seconds(5));
  require(exit_code.has_value() && *exit_code == 125,
          "sidecar handoff collision was not surfaced by the child");
  require(process.wait_for_tree(std::chrono::seconds(5)),
          "colliding sidecar tree did not become empty");
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
    std::cerr << "test failure: WSAStartup failed\n";
    return 1;
  }
  try {
    require(argument_count == 2, "test child path argument is missing");
    test_automatic_lease_blocks_competitors();
    test_fixed_lease_and_collision();
    test_release_then_resume_handoff(std::filesystem::path(arguments[1]));
    test_handoff_collision_is_visible(std::filesystem::path(arguments[1]));
    WSACleanup();
    std::cout << "loopback port pair tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    WSACleanup();
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
