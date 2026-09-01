#include "ncm/proxy_observer/request_observation.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_connect() {
  const auto observation = ncm::proxy_observer::observe_request(
      "CONNECT interface.music.163.com:443 HTTP/1.1\r\n"
      "Host: interface.music.163.com:443\r\n"
      "Proxy-Authorization: private-value\r\n\r\n");
  require(observation.method == "CONNECT", "CONNECT method not recognized");
  require(observation.form == ncm::proxy_observer::target_form::authority,
          "CONNECT target form is wrong");
  require(observation.scheme == "https", "CONNECT scheme is wrong");
  require(observation.destination == ncm::proxy_observer::destination_class::netease,
          "NetEase authority was not classified");
  require(observation.port == 443, "CONNECT port is wrong");
  require(observation.headers_complete, "complete CONNECT headers reported incomplete");
}

void test_absolute_uri_redaction_contract() {
  constexpr std::string_view secret_path = "/eapi/song?token=private-value";
  const auto observation = ncm::proxy_observer::observe_request(
      "POST http://interface.music.163.com/eapi/song?token=private-value HTTP/1.1\r\n"
      "Host: interface.music.163.com\r\n"
      "Authorization: private-value\r\n\r\n");
  require(observation.method == "POST", "POST method not recognized");
  require(observation.form == ncm::proxy_observer::target_form::absolute,
          "absolute target form is wrong");
  require(observation.scheme == "http", "absolute URI scheme is wrong");
  require(observation.destination == ncm::proxy_observer::destination_class::netease,
          "NetEase absolute URI was not classified");
  require(observation.port == 80, "HTTP default port is wrong");
  require(observation.method.find(secret_path) == std::string::npos,
          "private target data escaped through the observation");
}

void test_origin_form() {
  const auto observation = ncm::proxy_observer::observe_request(
      "GET /private/path?token=value HTTP/1.1\r\nHost: example.org:8080\r\n\r\n");
  require(observation.form == ncm::proxy_observer::target_form::origin,
          "origin target form is wrong");
  require(observation.destination == ncm::proxy_observer::destination_class::other,
          "other destination was not classified");
  require(observation.port == 8080, "Host header port is wrong");
}

void test_incomplete_request() {
  const auto observation = ncm::proxy_observer::observe_request("CONNECT music.163.com:443 HTTP/1.1\r\n");
  require(!observation.headers_complete, "incomplete headers reported complete");
  require(observation.destination == ncm::proxy_observer::destination_class::netease,
          "safe metadata was not recovered from incomplete headers");
}

void test_invalid_authority_is_not_defaulted() {
  const auto observation = ncm::proxy_observer::observe_request(
      "CONNECT music.163.com:not-a-port HTTP/1.1\r\n\r\n");
  require(observation.form == ncm::proxy_observer::target_form::authority,
          "invalid authority target form is wrong");
  require(observation.destination == ncm::proxy_observer::destination_class::unavailable,
          "invalid authority exposed a destination classification");
  require(!observation.port.has_value(), "invalid authority received a default port");
}

void test_unknown_scheme_is_redacted() {
  const auto observation = ncm::proxy_observer::observe_request(
      "GET private-value://example.org/path HTTP/1.1\r\n\r\n");
  require(observation.form == ncm::proxy_observer::target_form::absolute,
          "unknown scheme target form is wrong");
  require(observation.scheme == "other", "unknown scheme was not redacted");
}

}  // namespace

int main() {
  try {
    test_connect();
    test_absolute_uri_redaction_contract();
    test_origin_form();
    test_incomplete_request();
    test_invalid_authority_is_not_defaulted();
    test_unknown_scheme_is_redacted();
    std::cout << "proxy observation tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
