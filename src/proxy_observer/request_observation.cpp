#include "ncm/proxy_observer/request_observation.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <limits>
#include <string>

namespace ncm::proxy_observer {
namespace {

[[nodiscard]] char lowercase_ascii(char character) noexcept {
  if (character >= 'A' && character <= 'Z') {
    return static_cast<char>(character - 'A' + 'a');
  }
  return character;
}

[[nodiscard]] std::string lowercase_ascii(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](char character) {
    return lowercase_ascii(character);
  });
  return result;
}

[[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
    value.remove_suffix(1);
  }
  return value;
}

[[nodiscard]] bool valid_method(std::string_view value) noexcept {
  return !value.empty() && value.size() <= 16 &&
      std::ranges::all_of(value, [](char character) {
        return character >= 'A' && character <= 'Z';
      });
}

struct authority_info {
  std::string host;
  std::optional<std::uint16_t> port;
  bool valid{true};
};

[[nodiscard]] std::optional<std::uint16_t> parse_port(std::string_view value) noexcept {
  if (value.empty()) {
    return std::nullopt;
  }
  unsigned int port{};
  const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
      port == 0 || port > std::numeric_limits<std::uint16_t>::max()) {
    return std::nullopt;
  }
  return static_cast<std::uint16_t>(port);
}

[[nodiscard]] authority_info parse_authority(std::string_view value) {
  value = trim_ascii(value);
  if (const auto user_info = value.rfind('@'); user_info != std::string_view::npos) {
    value.remove_prefix(user_info + 1);
  }

  authority_info result;
  if (value.empty()) {
    result.valid = false;
    return result;
  }
  if (!value.empty() && value.front() == '[') {
    const auto bracket = value.find(']');
    if (bracket == std::string_view::npos) {
      result.valid = false;
      return result;
    }
    result.host = lowercase_ascii(value.substr(1, bracket - 1));
    if (bracket + 1 < value.size()) {
      if (value[bracket + 1] != ':') {
        result.valid = false;
        result.host.clear();
        return result;
      }
      result.port = parse_port(value.substr(bracket + 2));
      if (!result.port.has_value()) {
        result.valid = false;
        result.host.clear();
      }
    }
    if (result.host.empty()) {
      result.valid = false;
    }
    return result;
  }

  const auto colon = value.rfind(':');
  if (colon != std::string_view::npos && value.find(':') == colon) {
    result.host = lowercase_ascii(value.substr(0, colon));
    result.port = parse_port(value.substr(colon + 1));
    if (!result.port.has_value()) {
      result.valid = false;
      result.host.clear();
      return result;
    }
  } else if (colon != std::string_view::npos) {
    result.valid = false;
    return result;
  } else {
    result.host = lowercase_ascii(value);
  }
  while (!result.host.empty() && result.host.back() == '.') {
    result.host.pop_back();
  }
  if (result.host.empty()) {
    result.valid = false;
  }
  return result;
}

[[nodiscard]] bool is_netease_host(std::string_view host) noexcept {
  constexpr std::string_view suffixes[] = {
      "163.com", "126.net", "127.net", "netease.com", "music.163.com"};
  return std::ranges::any_of(suffixes, [host](std::string_view suffix) {
    return host == suffix ||
        (host.size() > suffix.size() && host.ends_with(suffix) &&
         host[host.size() - suffix.size() - 1] == '.');
  });
}

[[nodiscard]] destination_class classify(std::string_view host) noexcept {
  if (host.empty()) {
    return destination_class::unavailable;
  }
  return is_netease_host(host) ? destination_class::netease : destination_class::other;
}

[[nodiscard]] std::string_view header_value(
    std::string_view headers, std::string_view expected_name) noexcept {
  auto line_start = headers.find("\r\n");
  if (line_start == std::string_view::npos) {
    return {};
  }
  line_start += 2;

  while (line_start < headers.size()) {
    const auto line_end = headers.find("\r\n", line_start);
    if (line_end == std::string_view::npos || line_end == line_start) {
      return {};
    }
    const auto line = headers.substr(line_start, line_end - line_start);
    const auto colon = line.find(':');
    if (colon != std::string_view::npos) {
      const auto name = lowercase_ascii(trim_ascii(line.substr(0, colon)));
      if (name == expected_name) {
        return trim_ascii(line.substr(colon + 1));
      }
    }
    line_start = line_end + 2;
  }
  return {};
}

}  // namespace

request_observation observe_request(std::string_view request_headers) {
  request_observation observation;
  observation.headers_complete = request_headers.find("\r\n\r\n") != std::string_view::npos;

  const auto line_end = request_headers.find("\r\n");
  if (line_end == std::string_view::npos) {
    return observation;
  }
  const auto request_line = request_headers.substr(0, line_end);
  const auto first_space = request_line.find(' ');
  const auto second_space = first_space == std::string_view::npos
      ? std::string_view::npos
      : request_line.find(' ', first_space + 1);
  if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
    return observation;
  }

  const auto method = request_line.substr(0, first_space);
  if (!valid_method(method)) {
    return observation;
  }
  observation.method.assign(method);
  const auto target = request_line.substr(first_space + 1, second_space - first_space - 1);

  authority_info authority;
  if (method == "CONNECT") {
    observation.form = target_form::authority;
    observation.scheme = "https";
    authority = parse_authority(target);
    if (authority.valid && !authority.port.has_value()) {
      authority.port = static_cast<std::uint16_t>(443);
    }
  } else if (target == "*") {
    observation.form = target_form::asterisk;
  } else if (const auto scheme_end = target.find("://"); scheme_end != std::string_view::npos) {
    observation.form = target_form::absolute;
    const auto parsed_scheme = lowercase_ascii(target.substr(0, scheme_end));
    observation.scheme = parsed_scheme == "http" || parsed_scheme == "https"
        ? parsed_scheme
        : "other";
    const auto authority_start = scheme_end + 3;
    const auto authority_end = target.find_first_of("/?#", authority_start);
    authority = parse_authority(target.substr(
        authority_start,
        authority_end == std::string_view::npos
            ? std::string_view::npos
            : authority_end - authority_start));
    if (authority.valid && !authority.port.has_value()) {
      if (observation.scheme == "http") {
        authority.port = static_cast<std::uint16_t>(80);
      } else if (observation.scheme == "https") {
        authority.port = static_cast<std::uint16_t>(443);
      }
    }
  } else if (!target.empty() && target.front() == '/') {
    observation.form = target_form::origin;
    authority = parse_authority(header_value(request_headers, "host"));
  }

  if (authority.valid) {
    observation.destination = classify(authority.host);
    observation.port = authority.port;
  }
  return observation;
}

std::string_view to_string(target_form form) noexcept {
  switch (form) {
    case target_form::authority:
      return "authority";
    case target_form::absolute:
      return "absolute";
    case target_form::origin:
      return "origin";
    case target_form::asterisk:
      return "asterisk";
    case target_form::invalid:
      return "invalid";
  }
  return "invalid";
}

std::string_view to_string(destination_class destination) noexcept {
  switch (destination) {
    case destination_class::netease:
      return "netease";
    case destination_class::other:
      return "other";
    case destination_class::unavailable:
      return "unavailable";
  }
  return "unavailable";
}

}  // namespace ncm::proxy_observer
