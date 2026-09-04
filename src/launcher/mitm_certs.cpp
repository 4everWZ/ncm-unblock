#include "ncm/launcher/mitm_certs.hpp"

#include <Windows.h>
#include <wincrypt.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace ncm::launcher {
namespace {

[[noreturn]] void throw_last_error(const char* operation) {
  throw std::system_error(
      static_cast<int>(GetLastError()), std::system_category(), operation);
}

[[nodiscard]] bool path_is_regular_file(const std::filesystem::path& path) {
  std::error_code code;
  return std::filesystem::is_regular_file(path, code) && !code;
}

[[nodiscard]] std::optional<mitm_material> try_directory(
    const std::filesystem::path& directory) {
  if (directory.empty()) {
    return std::nullopt;
  }
  mitm_material material{
      directory / L"ca.crt",
      directory / L"server.crt",
      directory / L"server.key",
  };
  if (!path_is_regular_file(material.ca_certificate) ||
      !path_is_regular_file(material.server_certificate) ||
      !path_is_regular_file(material.server_key)) {
    return std::nullopt;
  }
  return material;
}

[[nodiscard]] std::vector<unsigned char> read_file_bytes(
    const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("unable to open certificate file");
  }
  return std::vector<unsigned char>(
      (std::istreambuf_iterator<char>(stream)),
      std::istreambuf_iterator<char>());
}

struct cert_context_guard {
  PCCERT_CONTEXT context{};
  ~cert_context_guard() {
    if (context != nullptr) {
      CertFreeCertificateContext(context);
    }
  }
};

struct store_guard {
  HCERTSTORE store{};
  ~store_guard() {
    if (store != nullptr) {
      CertCloseStore(store, 0);
    }
  }
};

[[nodiscard]] cert_context_guard decode_certificate(
    const std::vector<unsigned char>& bytes) {
  cert_context_guard guard;
  guard.context = CertCreateCertificateContext(
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, bytes.data(),
      static_cast<DWORD>(bytes.size()));
  if (guard.context != nullptr) {
    return guard;
  }

  DWORD der_size = 0;
  if (!CryptStringToBinaryA(
          reinterpret_cast<const char*>(bytes.data()),
          static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64HEADER, nullptr,
          &der_size, nullptr, nullptr)) {
    throw_last_error("CryptStringToBinaryA(size)");
  }
  std::vector<unsigned char> der(der_size);
  if (!CryptStringToBinaryA(
          reinterpret_cast<const char*>(bytes.data()),
          static_cast<DWORD>(bytes.size()), CRYPT_STRING_BASE64HEADER,
          der.data(), &der_size, nullptr, nullptr)) {
    throw_last_error("CryptStringToBinaryA");
  }
  guard.context = CertCreateCertificateContext(
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), der_size);
  if (guard.context == nullptr) {
    throw_last_error("CertCreateCertificateContext");
  }
  return guard;
}

[[nodiscard]] std::array<unsigned char, 20> sha1_thumbprint(
    PCCERT_CONTEXT context) {
  std::array<unsigned char, 20> thumbprint{};
  DWORD size = static_cast<DWORD>(thumbprint.size());
  if (!CertGetCertificateContextProperty(
          context, CERT_SHA1_HASH_PROP_ID, thumbprint.data(), &size) ||
      size != thumbprint.size()) {
    throw_last_error("CertGetCertificateContextProperty(SHA1)");
  }
  return thumbprint;
}

}  // namespace

std::optional<mitm_material> resolve_mitm_material(
    const std::filesystem::path& host_directory,
    const std::filesystem::path& unm_directory,
    const std::optional<std::filesystem::path>& explicit_directory) {
  if (explicit_directory.has_value()) {
    if (auto found = try_directory(*explicit_directory)) {
      return found;
    }
  }
  if (!host_directory.empty()) {
    if (auto found = try_directory(host_directory / L"certs")) {
      return found;
    }
    if (auto found = try_directory(host_directory.parent_path() / L"certs")) {
      return found;
    }
  }
  if (!unm_directory.empty()) {
    if (auto found = try_directory(unm_directory / L"certs")) {
      return found;
    }
    if (auto found = try_directory(unm_directory)) {
      return found;
    }
  }
  return std::nullopt;
}

bool current_user_root_contains(const std::filesystem::path& ca_certificate) {
  const auto bytes = read_file_bytes(ca_certificate);
  auto decoded = decode_certificate(bytes);
  const auto wanted = sha1_thumbprint(decoded.context);

  store_guard store;
  store.store = CertOpenStore(
      CERT_STORE_PROV_SYSTEM_W, 0, 0,
      CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG |
          CERT_STORE_READONLY_FLAG,
      L"ROOT");
  if (store.store == nullptr) {
    throw_last_error("CertOpenStore(CurrentUser Root read)");
  }

  PCCERT_CONTEXT existing = nullptr;
  while ((existing = CertEnumCertificatesInStore(store.store, existing)) !=
         nullptr) {
    try {
      if (sha1_thumbprint(existing) == wanted) {
        CertFreeCertificateContext(existing);
        return true;
      }
    } catch (...) {
      // Skip unreadable entries and keep scanning.
    }
  }
  return false;
}

void ensure_current_user_root_trust(const std::filesystem::path& ca_certificate) {
  if (current_user_root_contains(ca_certificate)) {
    return;
  }
  const auto bytes = read_file_bytes(ca_certificate);
  auto decoded = decode_certificate(bytes);

  store_guard store;
  store.store = CertOpenStore(
      CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"ROOT");
  if (store.store == nullptr) {
    throw_last_error("CertOpenStore(CurrentUser Root write)");
  }

  if (!CertAddCertificateContextToStore(
          store.store, decoded.context, CERT_STORE_ADD_REPLACE_EXISTING,
          nullptr)) {
    throw_last_error("CertAddCertificateContextToStore");
  }
}

std::vector<std::pair<std::wstring, std::wstring>> mitm_sign_environment(
    const mitm_material& material) {
  return {
      {L"SIGN_CERT", material.server_certificate.wstring()},
      {L"SIGN_KEY", material.server_key.wstring()},
  };
}

}  // namespace ncm::launcher
