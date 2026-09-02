#include "ncm/network_stack_census/census.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <cstdio>

namespace ncm::network_stack_census {
namespace {

// `LdrRegisterDllNotification` calls back with the loader lock held, so the
// recording path below allocates nothing, loads nothing, and touches no file.
// Report writing is deliberately left to the census thread.

struct UNICODE_STRING_VIEW {
  unsigned short Length;
  unsigned short MaximumLength;
  wchar_t* Buffer;
};

struct LDR_DLL_NOTIFICATION_DATA_VIEW {
  unsigned long Flags;
  const UNICODE_STRING_VIEW* FullDllName;
  const UNICODE_STRING_VIEW* BaseDllName;
  void* DllBase;
  unsigned long SizeOfImage;
};

using notification_function = void(CALLBACK*)(
    unsigned long reason, const LDR_DLL_NOTIFICATION_DATA_VIEW* data, void* context);
using register_notification = long(NTAPI*)(
    unsigned long flags, notification_function callback, void* context, void** cookie);

constexpr unsigned long notification_loaded = 1;
constexpr unsigned long notification_unloaded = 2;

constexpr unsigned event_capacity = 512;
constexpr unsigned long long default_window_ms = 180000;
constexpr unsigned long long report_interval_ms = 1000;

census_event g_events[event_capacity]{};
volatile long g_claimed{};
volatile long g_published{};
volatile long g_dropped{};
volatile long g_observed{};
unsigned long long g_started_ms{};

struct classification_rule {
  const wchar_t* pattern;
  bool prefix;
  stack_class classification;
};

// Membership is by base name only. A prefix rule covers the versioned names
// these libraries ship under; it never widens beyond its own family.
constexpr classification_rule rules[]{
    {L"winmm.dll", false, stack_class::winmm},
    {L"ws2_32.dll", false, stack_class::winsock},
    {L"mswsock.dll", false, stack_class::winsock},
    {L"wshtcpip.dll", false, stack_class::winsock},
    {L"wship6.dll", false, stack_class::winsock},
    {L"nsi.dll", false, stack_class::winsock},
    {L"winhttp.dll", false, stack_class::winhttp},
    {L"wininet.dll", false, stack_class::wininet},
    {L"urlmon.dll", false, stack_class::wininet},
    {L"schannel.dll", false, stack_class::schannel},
    {L"secur32.dll", false, stack_class::schannel},
    {L"sspicli.dll", false, stack_class::schannel},
    {L"ncrypt", true, stack_class::schannel},
    {L"libssl", true, stack_class::openssl},
    {L"libcrypto", true, stack_class::openssl},
    {L"libeay32.dll", false, stack_class::openssl},
    {L"ssleay32.dll", false, stack_class::openssl},
    {L"libcurl", true, stack_class::libcurl},
    {L"curl", true, stack_class::libcurl},
    {L"libcef.dll", false, stack_class::cef},
    {L"chrome_elf.dll", false, stack_class::cef},
    {L"cef_", true, stack_class::cef},
    {L"audioses.dll", false, stack_class::audio_render},
    {L"mmdevapi.dll", false, stack_class::audio_render},
    {L"audioeng.dll", false, stack_class::audio_render},
    {L"avrt.dll", false, stack_class::audio_render},
    {L"wdmaud.drv", false, stack_class::audio_render},
    {L"msacm32.dll", false, stack_class::audio_render},
    {L"ksuser.dll", false, stack_class::audio_render},
    {L"xaudio2", true, stack_class::audio_render},
};

[[nodiscard]] wchar_t lowered(wchar_t character) noexcept {
  return character >= L'A' && character <= L'Z'
      ? static_cast<wchar_t>(character + (L'a' - L'A'))
      : character;
}

[[nodiscard]] bool matches(const wchar_t* name, const wchar_t* pattern, bool prefix) noexcept {
  unsigned index = 0;
  for (; pattern[index] != L'\0'; ++index) {
    if (name[index] == L'\0' || lowered(name[index]) != pattern[index]) {
      return false;
    }
  }
  return prefix || name[index] == L'\0';
}

}  // namespace

stack_class classify_module(const wchar_t* base_name) noexcept {
  if (base_name == nullptr || base_name[0] == L'\0') {
    return stack_class::other;
  }
  for (const auto& rule : rules) {
    if (matches(base_name, rule.pattern, rule.prefix)) {
      return rule.classification;
    }
  }
  return stack_class::other;
}

const char* stack_class_name(stack_class value) noexcept {
  switch (value) {
    case stack_class::other:
      return "other";
    case stack_class::winmm:
      return "winmm";
    case stack_class::winsock:
      return "winsock";
    case stack_class::winhttp:
      return "winhttp";
    case stack_class::wininet:
      return "wininet";
    case stack_class::schannel:
      return "schannel";
    case stack_class::openssl:
      return "openssl";
    case stack_class::libcurl:
      return "libcurl";
    case stack_class::cef:
      return "cef";
    case stack_class::audio_render:
      return "audio_render";
  }
  return "unknown";
}

const char* process_role() noexcept {
  const wchar_t* command_line = GetCommandLineW();
  if (command_line == nullptr) {
    return "unknown";
  }
  static constexpr wchar_t switch_text[] = L"--type=";
  for (unsigned index = 0; command_line[index] != L'\0'; ++index) {
    if (!matches(command_line + index, switch_text, true)) {
      continue;
    }
    const wchar_t* value = command_line + index + 7;
    if (matches(value, L"renderer", true)) {
      return "renderer";
    }
    if (matches(value, L"gpu-process", true)) {
      return "gpu";
    }
    if (matches(value, L"utility", true)) {
      return "utility";
    }
    if (matches(value, L"crashpad", true)) {
      return "crashpad";
    }
    return "child";
  }
  return "root";
}

namespace {

// Loader-lock safe: fixed storage, no allocation, no I/O. A slot is claimed with
// one interlocked increment and published with a second counter so a reader
// never sees a half-written record.
void record(const wchar_t* base_name, bool loaded) noexcept {
  InterlockedIncrement(&g_observed);
  const stack_class classification = classify_module(base_name);
  if (classification == stack_class::other) {
    return;
  }
  const long slot = InterlockedIncrement(&g_claimed) - 1;
  if (slot < 0 || slot >= static_cast<long>(event_capacity)) {
    InterlockedIncrement(&g_dropped);
    return;
  }

  census_event& event = g_events[slot];
  event.elapsed_ms = GetTickCount64() - g_started_ms;
  event.classification = classification;
  event.loaded = loaded;
  unsigned index = 0;
  for (; index + 1 < 64 && base_name[index] != L'\0'; ++index) {
    event.base_name[index] = base_name[index];
  }
  event.base_name[index] = L'\0';
  InterlockedIncrement(&g_published);
}

void CALLBACK on_notification(
    unsigned long reason, const LDR_DLL_NOTIFICATION_DATA_VIEW* data, void*) {
  if (data == nullptr || data->BaseDllName == nullptr ||
      data->BaseDllName->Buffer == nullptr ||
      (reason != notification_loaded && reason != notification_unloaded)) {
    return;
  }
  // The counted string is not guaranteed to be terminated, so it is copied into
  // fixed stack storage before any name comparison.
  wchar_t name[64]{};
  const unsigned characters = data->BaseDllName->Length / sizeof(wchar_t);
  unsigned index = 0;
  for (; index + 1 < 64 && index < characters; ++index) {
    name[index] = data->BaseDllName->Buffer[index];
  }
  name[index] = L'\0';
  record(name, reason == notification_loaded);
}

void snapshot_loaded_modules() noexcept {
  const HANDLE snapshot =
      CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
  if (snapshot == INVALID_HANDLE_VALUE) {
    return;
  }
  MODULEENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Module32FirstW(snapshot, &entry)) {
    do {
      record(entry.szModule, true);
    } while (Module32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
}

[[nodiscard]] unsigned long long environment_number(
    const wchar_t* name, unsigned long long fallback) noexcept {
  wchar_t buffer[32]{};
  const DWORD written = GetEnvironmentVariableW(name, buffer, 32);
  if (written == 0 || written >= 32) {
    return fallback;
  }
  unsigned long long value = 0;
  for (DWORD index = 0; index < written; ++index) {
    if (buffer[index] < L'0' || buffer[index] > L'9') {
      return fallback;
    }
    value = value * 10 + static_cast<unsigned long long>(buffer[index] - L'0');
  }
  return value == 0 ? fallback : value;
}

}  // namespace

unsigned recorded_event_count() noexcept {
  const long published = InterlockedCompareExchange(&g_published, 0, 0);
  return published < 0 ? 0u : static_cast<unsigned>(published);
}

unsigned dropped_event_count() noexcept {
  const long dropped = InterlockedCompareExchange(&g_dropped, 0, 0);
  return dropped < 0 ? 0u : static_cast<unsigned>(dropped);
}

unsigned observed_module_count() noexcept {
  const long observed = InterlockedCompareExchange(&g_observed, 0, 0);
  return observed < 0 ? 0u : static_cast<unsigned>(observed);
}

bool recorded_event(unsigned index, census_event* out) noexcept {
  if (out == nullptr || index >= recorded_event_count()) {
    return false;
  }
  *out = g_events[index];
  return true;
}

bool begin_capture() noexcept {
  if (g_started_ms == 0) {
    g_started_ms = GetTickCount64();
  }

  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  const auto register_function = ntdll == nullptr
      ? nullptr
      : reinterpret_cast<register_notification>(
            reinterpret_cast<void*>(
                GetProcAddress(ntdll, "LdrRegisterDllNotification")));

  void* cookie{};
  const bool registered = register_function != nullptr &&
      register_function(0, &on_notification, nullptr, &cookie) >= 0;

  // Registering before the snapshot means a module mapped between the two steps
  // is recorded twice instead of missed. Duplicates are visible in the report;
  // a gap would not be.
  snapshot_loaded_modules();
  return registered;
}

bool write_report(const wchar_t* path) noexcept {
  const HANDLE file = CreateFileW(
      path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }

  const auto emit = [&](const char* text, int length) noexcept {
    if (length <= 0) {
      return;
    }
    DWORD ignored{};
    WriteFile(file, text, static_cast<DWORD>(length), &ignored, nullptr);
  };

  char line[256]{};
  emit(line, sprintf_s(
      line, "role=%s pid=%lu elapsed_ms=%llu\n", process_role(),
      GetCurrentProcessId(), GetTickCount64() - g_started_ms));

  const unsigned count = recorded_event_count();
  for (unsigned index = 0; index < count; ++index) {
    const census_event& event = g_events[index];
    emit(line, sprintf_s(
        line, "event %llu %s %s %ls\n", event.elapsed_ms,
        event.loaded ? "load" : "unload",
        stack_class_name(event.classification), event.base_name));
  }

  emit(line, sprintf_s(
      line, "totals observed=%u recorded=%u dropped=%u\n",
      observed_module_count(), count, dropped_event_count()));
  CloseHandle(file);
  return true;
}

void run_census() noexcept {
  wchar_t directory[MAX_PATH]{};
  const DWORD written =
      GetEnvironmentVariableW(L"NCM_CENSUS_REPORT_DIR", directory, MAX_PATH);
  if (written == 0 || written >= MAX_PATH) {
    OutputDebugStringW(
        L"ncm-unblock census: NCM_CENSUS_REPORT_DIR is unset, so no census ran\r\n");
    return;
  }

  const bool registered = begin_capture();
  if (!registered) {
    OutputDebugStringW(
        L"ncm-unblock census: loader notifications are unavailable;"
        L" only the startup snapshot was captured\r\n");
  }

  wchar_t path[MAX_PATH]{};
  if (_snwprintf_s(
          path, _TRUNCATE, L"%s\\census-%hs-%lu.txt", directory, process_role(),
          GetCurrentProcessId()) <= 0) {
    return;
  }

  // A bounded observation window with a rewrite tick. This is investigation
  // tooling, not the event-driven target design: the loader callback cannot
  // write a file, so the timeline has to be flushed from this thread.
  const unsigned long long window_ms =
      environment_number(L"NCM_CENSUS_WINDOW_MS", default_window_ms);
  const unsigned long long deadline = GetTickCount64() + window_ms;
  unsigned last_written = 0;
  for (;;) {
    const unsigned count = recorded_event_count();
    if (count != last_written) {
      static_cast<void>(write_report(path));
      last_written = count;
    }
    if (GetTickCount64() >= deadline) {
      break;
    }
    Sleep(static_cast<DWORD>(report_interval_ms));
  }
  static_cast<void>(write_report(path));
}

}  // namespace ncm::network_stack_census
