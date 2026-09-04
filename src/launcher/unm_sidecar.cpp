#include "ncm/launcher/unm_sidecar.hpp"

#include "ncm/launcher/loopback_port_pair.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <iphlpapi.h>

#include <array>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace ncm::launcher {
namespace {

class winsock_session {
 public:
  winsock_session() {
    WSADATA data{};
    const auto status = WSAStartup(MAKEWORD(2, 2), &data);
    if (status != 0) {
      throw std::system_error(status, std::system_category(), "WSAStartup");
    }
  }
  ~winsock_session() { WSACleanup(); }
  winsock_session(const winsock_session&) = delete;
  winsock_session& operator=(const winsock_session&) = delete;
};

enum class readiness_status {
  ready,
  process_tree_exited,
  listener_missing,
  listener_wrong_owner,
  pac_invalid,
  ownership_changed,
};

[[nodiscard]] const char* readiness_description(readiness_status status) noexcept {
  switch (status) {
    case readiness_status::ready:
      return "ready";
    case readiness_status::process_tree_exited:
      return "process tree exited before readiness";
    case readiness_status::listener_missing:
      return "one or both loopback listeners were missing";
    case readiness_status::listener_wrong_owner:
      return "a loopback listener was owned outside the private job";
    case readiness_status::pac_invalid:
      return "PAC endpoint did not return a complete non-empty HTTP 200 response";
    case readiness_status::ownership_changed:
      return "listener ownership changed during PAC verification";
  }
  return "unknown readiness failure";
}

[[nodiscard]] bool is_reserved_argument(std::wstring_view argument) {
  return argument == L"-a" || argument == L"--address" ||
      argument == L"-p" || argument == L"--port" ||
      argument == L"-s" || argument == L"--strict" ||
      argument.starts_with(L"--address=") ||
      argument.starts_with(L"--port=") ||
      argument.starts_with(L"--strict=");
}

void validate_options(const unm_sidecar_options& options) {
  const auto fixed = options.fixed_http_port.has_value() ||
      options.fixed_https_port.has_value();
  if (!fixed && options.maximum_automatic_attempts == 0) {
    throw std::invalid_argument("automatic sidecar attempt budget must be positive");
  }
  if (options.readiness_timeout <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("sidecar readiness timeout must be positive");
  }
  if (options.fixed_http_port.has_value() != options.fixed_https_port.has_value()) {
    throw std::invalid_argument("fixed HTTP and HTTPS ports must be configured together");
  }
  for (const auto& argument : options.arguments) {
    if (is_reserved_argument(argument)) {
      throw std::invalid_argument(
          "sidecar arguments must not override address, port, or strict mode");
    }
  }
}

[[nodiscard]] std::vector<std::wstring> build_arguments(
    const unm_sidecar_options& options, std::uint16_t http_port,
    std::uint16_t https_port) {
  auto arguments = options.arguments;
  arguments.emplace_back(L"-a");
  arguments.emplace_back(L"127.0.0.1");
  arguments.emplace_back(L"-p");
  arguments.emplace_back(
      std::to_wstring(http_port) + L":" + std::to_wstring(https_port));
  arguments.emplace_back(L"-s");
  return arguments;
}

void release_and_resume(
    loopback_port_pair& lease, managed_process& process) {
  if (!lease.active() || !process.suspended()) {
    throw std::logic_error("sidecar handoff requires active leases and a suspended process");
  }
  lease.release();
  process.resume();
}

[[nodiscard]] std::vector<std::uint32_t> loopback_listener_owners(
    std::uint16_t port) {
  std::vector<std::byte> buffer;
  for (int attempt = 0; attempt < 3; ++attempt) {
    DWORD bytes = static_cast<DWORD>(buffer.size());
    const auto status = GetExtendedTcpTable(
        buffer.empty() ? nullptr : buffer.data(), &bytes, FALSE, AF_INET,
        TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (status == ERROR_INSUFFICIENT_BUFFER) {
      buffer.resize(bytes);
      continue;
    }
    if (status != NO_ERROR) {
      throw std::system_error(
          static_cast<int>(status), std::system_category(),
          "GetExtendedTcpTable");
    }
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.data());
    std::vector<std::uint32_t> owners;
    for (DWORD index = 0; index < table->dwNumEntries; ++index) {
      const auto& row = table->table[index];
      if (row.dwLocalAddr == htonl(INADDR_LOOPBACK) &&
          ntohs(static_cast<std::uint16_t>(row.dwLocalPort)) == port) {
        owners.push_back(row.dwOwningPid);
      }
    }
    return owners;
  }
  throw std::runtime_error("TCP listener table changed during three snapshots");
}

[[nodiscard]] std::string ascii_lower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(character >= 'A' && character <= 'Z'
        ? character - 'A' + 'a'
        : character);
  });
  return result;
}

[[nodiscard]] std::string_view trim_ascii(std::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] bool complete_chunked_body(std::string_view body) {
  std::size_t position{};
  std::size_t payload_size{};
  for (;;) {
    const auto line_end = body.find("\r\n", position);
    if (line_end == std::string_view::npos) {
      return false;
    }
    auto size_text = body.substr(position, line_end - position);
    const auto extension = size_text.find(';');
    if (extension != std::string_view::npos) {
      size_text = size_text.substr(0, extension);
    }
    size_text = trim_ascii(size_text);
    std::size_t chunk_size{};
    const auto parsed = std::from_chars(
        size_text.data(), size_text.data() + size_text.size(), chunk_size, 16);
    if (size_text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != size_text.data() + size_text.size()) {
      return false;
    }
    position = line_end + 2;
    if (chunk_size == 0) {
      return payload_size != 0 && body.substr(position).starts_with("\r\n");
    }
    if (chunk_size > body.size() - position ||
        body.size() - position - chunk_size < 2 ||
        body.substr(position + chunk_size, 2) != "\r\n") {
      return false;
    }
    payload_size += chunk_size;
    position += chunk_size + 2;
  }
}

[[nodiscard]] bool complete_nonempty_http_body(
    std::string_view response, std::size_t header_end) {
  const auto header_block = response.substr(0, header_end);
  const auto body = response.substr(header_end + 4);
  std::optional<std::size_t> content_length;
  bool chunked{};
  auto position = header_block.find("\r\n");
  if (position == std::string_view::npos) {
    return false;
  }
  position += 2;
  while (position < header_block.size()) {
    const auto line_end = header_block.find("\r\n", position);
    const auto end = line_end == std::string_view::npos ? header_block.size() : line_end;
    const auto line = header_block.substr(position, end - position);
    const auto separator = line.find(':');
    if (separator == std::string_view::npos) {
      return false;
    }
    const auto name = ascii_lower(trim_ascii(line.substr(0, separator)));
    const auto value = trim_ascii(line.substr(separator + 1));
    if (name == "content-length") {
      std::size_t length{};
      const auto parsed = std::from_chars(
          value.data(), value.data() + value.size(), length, 10);
      if (value.empty() || parsed.ec != std::errc{} ||
          parsed.ptr != value.data() + value.size()) {
        return false;
      }
      content_length = length;
    } else if (name == "transfer-encoding" &&
               ascii_lower(value).find("chunked") != std::string::npos) {
      chunked = true;
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    position = line_end + 2;
  }
  if (chunked) {
    return !content_length.has_value() && complete_chunked_body(body);
  }
  if (content_length.has_value()) {
    return *content_length != 0 && body.size() == *content_length;
  }
  return !body.empty();
}

[[nodiscard]] DWORD remaining_socket_timeout(
    std::chrono::steady_clock::time_point deadline) {
  const auto now = std::chrono::steady_clock::now();
  if (now >= deadline) {
    return 0;
  }
  const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
  return static_cast<DWORD>(std::max<std::int64_t>(1, std::min<std::int64_t>(500, remaining.count())));
}

[[nodiscard]] bool connect_before_deadline(
    SOCKET socket_handle, const sockaddr_in& address,
    std::chrono::steady_clock::time_point deadline) {
  u_long nonblocking = 1;
  if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    throw std::system_error(
        WSAGetLastError(), std::system_category(),
        "ioctlsocket(FIONBIO enable)");
  }
  const auto connected = connect(
      socket_handle, reinterpret_cast<const sockaddr*>(&address),
      sizeof(address));
  if (connected == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
    return false;
  }
  if (connected == SOCKET_ERROR) {
    for (;;) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        return false;
      }
      const auto remaining = std::min(
          std::chrono::duration_cast<std::chrono::microseconds>(deadline - now),
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::seconds(1)));
      timeval timeout{};
      timeout.tv_sec = static_cast<long>(remaining.count() / 1'000'000);
      timeout.tv_usec = static_cast<long>(remaining.count() % 1'000'000);
      fd_set writable;
      fd_set failed;
      FD_ZERO(&writable);
      FD_ZERO(&failed);
      FD_SET(socket_handle, &writable);
      FD_SET(socket_handle, &failed);
      const auto selected = select(0, nullptr, &writable, &failed, &timeout);
      if (selected == 0) {
        return false;
      }
      if (selected == SOCKET_ERROR) {
        throw std::system_error(
            WSAGetLastError(), std::system_category(),
            "select(PAC readiness connect)");
      }
      int socket_error{};
      int error_length = sizeof(socket_error);
      if (getsockopt(
              socket_handle, SOL_SOCKET, SO_ERROR,
              reinterpret_cast<char*>(&socket_error), &error_length) == SOCKET_ERROR) {
        throw std::system_error(
            WSAGetLastError(), std::system_category(),
            "getsockopt(SO_ERROR)");
      }
      if (socket_error != 0) {
        return false;
      }
      break;
    }
  }
  nonblocking = 0;
  if (ioctlsocket(socket_handle, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    throw std::system_error(
        WSAGetLastError(), std::system_category(),
        "ioctlsocket(FIONBIO disable)");
  }
  return true;
}

[[nodiscard]] bool pac_is_ready(
    std::uint16_t port, std::chrono::steady_clock::time_point deadline) {
  const auto socket_handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socket_handle == INVALID_SOCKET) {
    throw std::system_error(
        WSAGetLastError(), std::system_category(), "socket(PAC readiness)");
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  try {
    if (!connect_before_deadline(socket_handle, address, deadline)) {
      closesocket(socket_handle);
      return false;
    }
  } catch (...) {
    closesocket(socket_handle);
    throw;
  }
  constexpr std::string_view request =
      "GET /proxy.pac HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  std::size_t sent{};
  while (sent < request.size()) {
    const auto send_timeout = remaining_socket_timeout(deadline);
    if (send_timeout == 0 || setsockopt(
            socket_handle, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&send_timeout), sizeof(send_timeout)) == SOCKET_ERROR) {
      closesocket(socket_handle);
      return false;
    }
    const auto count = send(
        socket_handle, request.data() + sent,
        static_cast<int>(request.size() - sent), 0);
    if (count <= 0) {
      closesocket(socket_handle);
      return false;
    }
    sent += static_cast<std::size_t>(count);
  }
  std::array<char, 1024> chunk{};
  std::string response;
  response.reserve(1024);
  bool response_complete{};
  while (response.size() < 65536) {
    const auto receive_timeout = remaining_socket_timeout(deadline);
    if (receive_timeout == 0 || setsockopt(
            socket_handle, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&receive_timeout), sizeof(receive_timeout)) == SOCKET_ERROR) {
      break;
    }
    const auto received = recv(
        socket_handle, chunk.data(), static_cast<int>(chunk.size()), 0);
    if (received == 0) {
      response_complete = true;
      break;
    }
    if (received == SOCKET_ERROR) {
      break;
    }
    response.append(chunk.data(), static_cast<std::size_t>(received));
  }
  closesocket(socket_handle);
  const auto header_end = response.find("\r\n\r\n");
  if (!response_complete || header_end == std::string::npos ||
      response.size() <= header_end + 4) {
    return false;
  }
  const std::string_view status_line(response);
  const auto successful = status_line.starts_with("HTTP/1.1 200 ") ||
      status_line.starts_with("HTTP/1.0 200 ");
  return successful && complete_nonempty_http_body(response, header_end);
}

[[nodiscard]] readiness_status wait_until_ready(
    managed_process& process, std::uint16_t http_port,
    std::uint16_t https_port, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  auto last_status = readiness_status::listener_missing;
  do {
    if (process.wait_for_tree(std::chrono::milliseconds::zero())) {
      return readiness_status::process_tree_exited;
    }
    const auto http_owners = loopback_listener_owners(http_port);
    const auto https_owners = loopback_listener_owners(https_port);
    const auto all_owned = [&process](const auto& owners) {
      return !owners.empty() && std::all_of(
          owners.begin(), owners.end(), [&process](std::uint32_t owner) {
            return process.contains_process(owner);
          });
    };
    if (http_owners.empty() || https_owners.empty()) {
      last_status = readiness_status::listener_missing;
    } else if (!all_owned(http_owners) || !all_owned(https_owners)) {
      last_status = readiness_status::listener_wrong_owner;
    } else if (!pac_is_ready(http_port, deadline)) {
      last_status = readiness_status::pac_invalid;
    } else {
      const auto verified_http_owners = loopback_listener_owners(http_port);
      const auto verified_https_owners = loopback_listener_owners(https_port);
      if (!process.wait_for_tree(std::chrono::milliseconds::zero()) &&
          all_owned(verified_http_owners) && all_owned(verified_https_owners)) {
        return readiness_status::ready;
      }
      last_status = readiness_status::ownership_changed;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  } while (std::chrono::steady_clock::now() < deadline);
  return last_status;
}

[[nodiscard]] loopback_port_pair acquire_ports(const unm_sidecar_options& options) {
  if (options.fixed_http_port.has_value()) {
    return loopback_port_pair::acquire_fixed(
        *options.fixed_http_port, *options.fixed_https_port);
  }
  return loopback_port_pair::acquire_automatic();
}

}  // namespace

unm_sidecar::unm_sidecar(
    managed_process process, std::uint16_t http_port,
    std::uint16_t https_port) noexcept
    : process_(std::move(process)),
      http_port_(http_port),
      https_port_(https_port) {}

unm_sidecar unm_sidecar::launch(const unm_sidecar_options& options) {
  validate_options(options);
  winsock_session winsock;
  const auto fixed = options.fixed_http_port.has_value();
  const auto attempts = fixed ? std::size_t{1} : options.maximum_automatic_attempts;
  auto last_status = readiness_status::listener_missing;
  for (std::size_t attempt = 0; attempt < attempts; ++attempt) {
    auto lease = acquire_ports(options);
    const auto http_port = lease.http_port();
    const auto https_port = lease.https_port();
    process_spec process_options{
        options.executable,
        options.working_directory,
        build_arguments(options, http_port, https_port),
        options.output_file,
        options.environment,
        true};
    auto process = managed_process::prepare(process_options);
    release_and_resume(lease, process);
    last_status = wait_until_ready(
        process, http_port, https_port, options.readiness_timeout);
    if (last_status == readiness_status::ready) {
      return {std::move(process), http_port, https_port};
    }
    if (!process.terminate_and_wait_tree(1, std::chrono::seconds(5))) {
      throw std::runtime_error(
          "sidecar process tree did not stop after readiness failure");
    }
  }
  const auto prefix = fixed
      ? "sidecar did not become ready on the configured fixed port pair: "
      : "sidecar did not become ready within the automatic attempt budget: ";
  throw std::runtime_error(std::string(prefix) + readiness_description(last_status));
}

std::uint16_t unm_sidecar::http_port() const noexcept {
  return http_port_;
}

std::uint16_t unm_sidecar::https_port() const noexcept {
  return https_port_;
}

const managed_process& unm_sidecar::process() const noexcept {
  return process_;
}

managed_process& unm_sidecar::process() noexcept {
  return process_;
}

}  // namespace ncm::launcher
