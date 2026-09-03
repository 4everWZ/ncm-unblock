#include "ncm/config/settings.hpp"
#include "ncm/launcher/ncm_session.hpp"
#include "ncm/launcher/unm_sidecar.hpp"
#include "ncm/runtime_probe/pe_image.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::string narrow(std::wstring_view text) {
  if (text.empty()) return {};
  const auto count = WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
      nullptr, nullptr);
  if (count <= 0) return {};
  std::string result(static_cast<std::size_t>(count), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
      count, nullptr, nullptr);
  return result;
}

[[nodiscard]] std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  auto count = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
      nullptr, 0);
  auto code_page = CP_UTF8;
  auto flags = MB_ERR_INVALID_CHARS;
  if (count <= 0) {
    code_page = CP_ACP;
    flags = 0;
    count = MultiByteToWideChar(
        code_page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
  }
  if (count <= 0) return L"unknown error";
  std::wstring result(static_cast<std::size_t>(count), L'\0');
  MultiByteToWideChar(
      code_page, flags, text.data(), static_cast<int>(text.size()),
      result.data(), count);
  return result;
}

class logger {
 public:
  explicit logger(const std::filesystem::path& path) {
    stream_.open(path, std::ios::binary | std::ios::app);
    if (!stream_) {
      throw std::runtime_error("launcher log could not be opened");
    }
  }

  void write(std::wstring_view message) {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char prefix[64]{};
    sprintf_s(
        prefix, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", time.wYear,
        time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds);
    stream_ << prefix << narrow(message) << "\r\n";
    stream_.flush();
  }

 private:
  std::ofstream stream_;
};

class displayed_error final : public std::runtime_error {
 public:
  explicit displayed_error(std::wstring message)
      : std::runtime_error(narrow(message)), message_(std::move(message)) {}
  [[nodiscard]] const std::wstring& message() const noexcept { return message_; }

 private:
  std::wstring message_;
};

[[noreturn]] void fail(std::wstring message) {
  throw displayed_error(std::move(message));
}

void require_regular_file(
    const std::filesystem::path& path, std::wstring_view description) {
  std::error_code code;
  if (!std::filesystem::is_regular_file(path, code) || code) {
    fail(std::wstring(description) + L" was not found:\r\n" + path.wstring());
  }
}

int run() {
  const auto package = ncm::config::package_directory();
  if (package.empty()) {
    fail(L"The launcher directory could not be determined.");
  }
  const auto loaded = ncm::config::load_settings(package);
  if (loaded.status != ncm::config::load_status::loaded) {
    auto message = loaded.diagnostic;
    if (loaded.line != 0) {
      message += L" (line " + std::to_wstring(loaded.line) + L")";
    }
    fail(L"Invalid ncm-unblock.ini:\r\n" + message);
  }
  const auto& settings = loaded.value;
  require_regular_file(settings.ncm_executable, L"NCM executable");
  require_regular_file(settings.unm_executable, L"UNM executable");

  const auto ncm_image = ncm::runtime_probe::inspect_pe_image(
      settings.ncm_executable);
  if (ncm_image.machine != IMAGE_FILE_MACHINE_I386 || ncm_image.pe32_plus ||
      ncm_image.file_version != "2.9.7.199711" || !ncm_image.signature.valid) {
    fail(L"The configured NCM executable is not the signed x86 2.9.7.199711 target.");
  }
  if (ncm::launcher::ncm_session::target_running(settings.ncm_executable)) {
    fail(L"NetEase Cloud Music is already running. Exit it from the tray and try again.");
  }

  std::unique_ptr<logger> log;
  std::optional<std::filesystem::path> unm_log;
  if (settings.write_log) {
    const auto logs = package / L"logs";
    std::filesystem::create_directories(logs);
    log = std::make_unique<logger>(logs / L"launcher.log");
    unm_log = logs / L"unm.log";
    log->write(L"launcher-start version=0.1.0");
  }

  ncm::launcher::unm_sidecar_options sidecar_options;
  sidecar_options.executable = settings.unm_executable;
  sidecar_options.working_directory = settings.unm_executable.parent_path();
  sidecar_options.output_file = unm_log;
  sidecar_options.fixed_http_port = settings.http_port;
  sidecar_options.fixed_https_port = settings.https_port;
  sidecar_options.readiness_timeout = settings.readiness_timeout;
  if (!settings.sources.empty()) {
    sidecar_options.arguments.emplace_back(L"-o");
    sidecar_options.arguments.insert(
        sidecar_options.arguments.end(), settings.sources.begin(),
        settings.sources.end());
  }

  auto sidecar = ncm::launcher::unm_sidecar::launch(sidecar_options);
  if (log) {
    log->write(
        L"unm-ready pid=" + std::to_wstring(sidecar.process().process_id()) +
        L" http=" + std::to_wstring(sidecar.http_port()) + L" https=" +
        std::to_wstring(sidecar.https_port()));
  }
  if (ncm::launcher::ncm_session::target_running(settings.ncm_executable)) {
    fail(L"NetEase Cloud Music started while UNM was initializing. Exit it and try again.");
  }

  auto session = ncm::launcher::ncm_session::launch(settings.ncm_executable);
  if (log) {
    log->write(L"ncm-started pid=" + std::to_wstring(session.root_process_id()));
  }

  while (session.active()) {
    if (sidecar.process().wait_for_tree(std::chrono::seconds(1))) {
      if (log) log->write(L"unm-exited-before-ncm");
      fail(L"UnblockNeteaseMusic exited while NetEase Cloud Music was running.");
    }
  }
  if (log) log->write(L"ncm-session-ended");
  if (!sidecar.process().terminate_and_wait_tree(0, std::chrono::seconds(5))) {
    fail(L"UnblockNeteaseMusic did not stop within five seconds.");
  }
  if (log) log->write(L"unm-stopped launcher-exit");
  return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  try {
    return run();
  } catch (const displayed_error& error) {
    MessageBoxW(
        nullptr, error.message().c_str(), L"NCM Unblock 2.9.7",
        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 1;
  } catch (const std::exception& error) {
    const auto message = widen(error.what());
    MessageBoxW(
        nullptr, message.empty() ? L"Unknown launcher failure." : message.c_str(),
        L"NCM Unblock 2.9.7", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    return 1;
  } catch (...) {
    MessageBoxW(nullptr, L"Unknown launcher failure.", L"NCM Unblock 2.9.7", MB_OK | MB_ICONERROR);
    return 1;
  }
}
