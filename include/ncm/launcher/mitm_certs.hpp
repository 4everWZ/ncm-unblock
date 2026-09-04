#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ncm::launcher {

struct mitm_material {
  std::filesystem::path ca_certificate;
  std::filesystem::path server_certificate;
  std::filesystem::path server_key;
};

// Resolve packaged MITM material. Search order:
// 1. explicit_directory when set
// 2. <host_directory>/../certs (plugin layout: native/unm-host.exe + certs/)
// 3. <unm_directory>/certs
// 4. <unm_directory>
[[nodiscard]] std::optional<mitm_material> resolve_mitm_material(
    const std::filesystem::path& host_directory,
    const std::filesystem::path& unm_directory,
    const std::optional<std::filesystem::path>& explicit_directory = std::nullopt);

// Install the CA into the Current User Root store when it is not already present.
// Idempotent. Throws on encode/store failures.
void ensure_current_user_root_trust(const std::filesystem::path& ca_certificate);

// True when a certificate with the same SHA1 thumbprint is already in CurrentUser\Root.
[[nodiscard]] bool current_user_root_contains(
    const std::filesystem::path& ca_certificate);

// SIGN_CERT / SIGN_KEY environment pairs for the official UNM standalone.
[[nodiscard]] std::vector<std::pair<std::wstring, std::wstring>>
mitm_sign_environment(const mitm_material& material);

}  // namespace ncm::launcher
