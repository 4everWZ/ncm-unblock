#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ncm::proxy_observer {

enum class target_form {
  authority,
  absolute,
  origin,
  asterisk,
  invalid,
};

enum class destination_class {
  netease,
  other,
  unavailable,
};

struct request_observation {
  std::string method;
  target_form form{target_form::invalid};
  std::string scheme;
  destination_class destination{destination_class::unavailable};
  std::optional<std::uint16_t> port;
  bool headers_complete{};
};

[[nodiscard]] request_observation observe_request(std::string_view request_headers);
[[nodiscard]] std::string_view to_string(target_form form) noexcept;
[[nodiscard]] std::string_view to_string(destination_class destination) noexcept;

}  // namespace ncm::proxy_observer
