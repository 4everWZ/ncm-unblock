#include "ncm/config/settings.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

const std::filesystem::path package{L"C:\\portable"};

void test_valid_configuration() {
  const auto result = ncm::config::parse_settings(
      "[ncm]\n"
      "path = D:\\Netease\\cloudmusic.exe\n"
      "[unm]\n"
      "path = core/unm.exe\n"
      "http_port = 3412\n"
      "https_port = 3413\n"
      "sources = kugou bodian\n"
      "readiness_timeout_ms = 2500\n"
      "[launcher]\n"
      "write_log = true\n",
      package);
  require(result.status == ncm::config::load_status::loaded, "valid INI was rejected");
  require(result.value.ncm_executable == L"D:\\Netease\\cloudmusic.exe", "NCM path changed");
  require(result.value.unm_executable == package / L"core/unm.exe", "relative UNM path changed");
  require(result.value.http_port == 3412 && result.value.https_port == 3413, "ports changed");
  require(result.value.sources == std::vector<std::wstring>{L"kugou", L"bodian"}, "sources changed");
  require(result.value.readiness_timeout == std::chrono::milliseconds(2500), "timeout changed");
  require(result.value.write_log, "launcher log flag changed");
}

void test_upstream_defaults_are_preserved() {
  const auto result = ncm::config::parse_settings(
      "[ncm]\npath=C:\\ncm\\cloudmusic.exe\n"
      "[unm]\npath=core\\unm.exe\nsources=\n",
      package);
  require(result.status == ncm::config::load_status::loaded, "empty sources were rejected");
  require(result.value.sources.empty(), "empty sources did not preserve upstream defaults");
  require(result.value.http_port == 3412 && result.value.https_port == 3413,
          "fixed defaults changed");
}

void test_invalid_configuration_is_rejected() {
  const std::string_view invalid[]{
      "[unm]\npath=unm.exe\n",
      "[ncm]\npath=a.exe\n[unm]\npath=b.exe\nhttp_port=3412\nhttps_port=3412\n",
      "[ncm]\npath=a.exe\n[unm]\npath=b.exe\nsources=Kuwo\n",
      "[ncm]\npath=a.exe\npath=b.exe\n[unm]\npath=c.exe\n",
      "[cef]\npath=a\n",
      "path=a.exe\n",
  };
  for (const auto text : invalid) {
    const auto result = ncm::config::parse_settings(text, package);
    require(result.status == ncm::config::load_status::invalid, "invalid INI was accepted");
    require(!result.diagnostic.empty(), "invalid INI has no diagnostic");
  }
}

}  // namespace

int wmain() {
  try {
    test_valid_configuration();
    test_upstream_defaults_are_preserved();
    test_invalid_configuration_is_rejected();
    std::cout << "configuration tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
