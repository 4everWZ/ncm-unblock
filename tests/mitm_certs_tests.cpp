#include "ncm/launcher/mitm_certs.hpp"

#include <Windows.h>
#include <wincrypt.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void remove_ca_from_current_user_root(const std::filesystem::path& ca_path) {
  std::ifstream stream(ca_path, std::ios::binary);
  require(static_cast<bool>(stream), "unable to read fixture CA");
  const std::string pem(
      (std::istreambuf_iterator<char>(stream)),
      std::istreambuf_iterator<char>());
  DWORD der_size = 0;
  require(
      CryptStringToBinaryA(
          pem.data(), static_cast<DWORD>(pem.size()), CRYPT_STRING_BASE64HEADER,
          nullptr, &der_size, nullptr, nullptr) != FALSE,
      "CryptStringToBinaryA size failed");
  std::vector<unsigned char> der(der_size);
  require(
      CryptStringToBinaryA(
          pem.data(), static_cast<DWORD>(pem.size()), CRYPT_STRING_BASE64HEADER,
          der.data(), &der_size, nullptr, nullptr) != FALSE,
      "CryptStringToBinaryA decode failed");
  const auto context = CertCreateCertificateContext(
      X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(), der_size);
  require(context != nullptr, "CertCreateCertificateContext failed");

  const auto store = CertOpenStore(
      CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"ROOT");
  require(store != nullptr, "CertOpenStore failed");
  // CertDeleteCertificateFromStore frees the found context. Always restart the
  // find from nullptr so a freed PCCERT_CONTEXT is never passed back in.
  for (;;) {
    const auto existing = CertFindCertificateInStore(
        store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_EXISTING,
        context, nullptr);
    if (existing == nullptr) {
      break;
    }
    if (!CertDeleteCertificateFromStore(existing)) {
      CertFreeCertificateContext(existing);
      CertFreeCertificateContext(context);
      CertCloseStore(store, 0);
      throw std::runtime_error("CertDeleteCertificateFromStore failed");
    }
  }
  CertFreeCertificateContext(context);
  CertCloseStore(store, 0);
}

void test_resolve_and_trust(const std::filesystem::path& certs_directory) {
  const auto material = ncm::launcher::resolve_mitm_material(
      {}, {}, certs_directory);
  require(material.has_value(), "fixture certs directory was not resolved");
  require(
      material->ca_certificate.filename() == L"ca.crt",
      "CA path unexpected");
  require(
      material->server_certificate.filename() == L"server.crt",
      "leaf path unexpected");
  require(
      material->server_key.filename() == L"server.key", "key path unexpected");

  const auto env = ncm::launcher::mitm_sign_environment(*material);
  require(env.size() == 2, "SIGN env pair count wrong");
  require(env[0].first == L"SIGN_CERT", "SIGN_CERT name wrong");
  require(env[1].first == L"SIGN_KEY", "SIGN_KEY name wrong");
  require(
      env[0].second == material->server_certificate.wstring(),
      "SIGN_CERT path wrong");
  require(
      env[1].second == material->server_key.wstring(), "SIGN_KEY path wrong");

  remove_ca_from_current_user_root(material->ca_certificate);
  require(
      !ncm::launcher::current_user_root_contains(material->ca_certificate),
      "fixture CA still present after cleanup");
  ncm::launcher::ensure_current_user_root_trust(material->ca_certificate);
  require(
      ncm::launcher::current_user_root_contains(material->ca_certificate),
      "fixture CA was not installed");
  ncm::launcher::ensure_current_user_root_trust(material->ca_certificate);
  require(
      ncm::launcher::current_user_root_contains(material->ca_certificate),
      "idempotent trust install failed");
}

void test_layout_search(
    const std::filesystem::path& certs_directory,
    const std::filesystem::path& scratch) {
  std::error_code code;
  std::filesystem::remove_all(scratch, code);
  const auto host_dir = scratch / L"plugin" / L"native";
  const auto certs_layout = scratch / L"plugin" / L"certs";
  std::filesystem::create_directories(host_dir);
  std::filesystem::create_directories(certs_layout);
  std::filesystem::copy_file(
      certs_directory / L"ca.crt", certs_layout / L"ca.crt");
  std::filesystem::copy_file(
      certs_directory / L"server.crt", certs_layout / L"server.crt");
  std::filesystem::copy_file(
      certs_directory / L"server.key", certs_layout / L"server.key");
  const auto material =
      ncm::launcher::resolve_mitm_material(host_dir, scratch / L"unm");
  require(material.has_value(), "plugin certs/ layout was not found");
  require(
      material->ca_certificate == certs_layout / L"ca.crt",
      "plugin layout CA path mismatch");
  std::filesystem::remove_all(scratch, code);
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  try {
    require(argument_count == 2, "certs directory argument is missing");
    const std::filesystem::path certs(arguments[1]);
    const auto scratch = std::filesystem::temp_directory_path() /
        (L"ncm-mitm-certs-" + std::to_wstring(GetCurrentProcessId()));
    test_resolve_and_trust(certs);
    test_layout_search(certs, scratch);
    std::cout << "mitm certs tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "test failure: " << error.what() << '\n';
    return 1;
  }
}
