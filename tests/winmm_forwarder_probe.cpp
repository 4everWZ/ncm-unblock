// Child process for the WinMM export-parity experiment.
//
// The checks run in a dedicated process because they depend on which module
// owns the base name `winmm.dll`. A test harness that has already loaded the
// system module would resolve the negative control's forwarders to it and hide
// the self-forward hazard entirely.

#include "ncm/winmm_proxy/forwarder.hpp"
#include "winmm_backend_contract.hpp"

#include <Windows.h>

#include <cstdint>
#include <exception>
#include <iterator>
#include <string>

namespace {

using stdcall_export = unsigned long(__stdcall*)(
    unsigned long, unsigned long, unsigned long, unsigned long);

#pragma warning(push)
// A naked function supplies its own prologue, epilogue, and return value, and
// reaches its parameters through the frame it sets up itself.
#pragma warning(disable : 4100 4716 4731)

// Calls a four-argument stdcall export and records how far the stack pointer
// moved. A `__stdcall` callee must pop the 16 argument bytes itself, so a
// non-zero delta means a thunk changed the calling convention seen by the
// caller. The stack pointer is restored either way.
__declspec(naked) unsigned long __cdecl invoke_checked(
    stdcall_export target, unsigned long first, unsigned long second,
    unsigned long third, unsigned long fourth, int* stack_delta) {
  __asm {
    push ebp
    mov  ebp, esp
    push ebx
    mov  ebx, esp
    push dword ptr [ebp + 24]
    push dword ptr [ebp + 20]
    push dword ptr [ebp + 16]
    push dword ptr [ebp + 12]
    call dword ptr [ebp + 8]
    mov  ecx, esp
    sub  ecx, ebx
    mov  edx, dword ptr [ebp + 28]
    mov  dword ptr [edx], ecx
    mov  esp, ebx
    pop  ebx
    pop  ebp
    ret
  }
}

#pragma warning(pop)

class probe_error : public std::exception {
 public:
  explicit probe_error(std::string message) : message_(std::move(message)) {}
  [[nodiscard]] const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

void require(bool condition, const std::string& message) {
  if (!condition) {
    throw probe_error(message);
  }
}

[[nodiscard]] std::string describe(unsigned long value) {
  return std::to_string(value);
}

[[nodiscard]] HMODULE owning_module(const void* address) {
  HMODULE owner{};
  if (GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          static_cast<LPCWSTR>(address), &owner) == 0) {
    return nullptr;
  }
  return owner;
}

[[nodiscard]] std::wstring module_path(HMODULE module) {
  wchar_t buffer[MAX_PATH]{};
  const auto written = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
  if (written == 0 || written >= std::size(buffer)) {
    return {};
  }
  return {buffer, written};
}

[[nodiscard]] bool same_path(const std::wstring& left, const std::wstring& right) {
  return CompareStringOrdinal(
             left.c_str(), static_cast<int>(left.size()), right.c_str(),
             static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

[[nodiscard]] LPCSTR lookup_key(const ncm::winmm_proxy::export_entry& entry) {
  return entry.name != nullptr ? entry.name : MAKEINTRESOURCEA(entry.ordinal);
}

[[nodiscard]] std::string entry_label(const ncm::winmm_proxy::export_entry& entry) {
  if (entry.name != nullptr) {
    return entry.name;
  }
  return "ordinal " + std::to_string(entry.ordinal);
}

void require_no_preloaded_winmm() {
  require(GetModuleHandleW(L"winmm.dll") == nullptr,
          "a winmm.dll was already loaded, so the probe could not observe base-name ownership");
}

// Walks the whole pinned surface through the proxy and compares it against the
// backend reached directly.
std::string run_forward_probe(const std::wstring& proxy_path, const std::wstring& backend_path) {
  require_no_preloaded_winmm();

  const HMODULE proxy = LoadLibraryW(proxy_path.c_str());
  require(proxy != nullptr, "the proxy fixture did not load: " + describe(GetLastError()));
  require(GetModuleHandleW(L"winmm.dll") == proxy,
          "the proxy fixture did not take ownership of the winmm.dll base name");

  unsigned count{};
  const auto* entries = ncm::winmm_proxy::pinned_exports(&count);
  require(entries != nullptr && count == ncm::winmm_proxy_fixture::entry_count,
          "the pinned export table is unavailable");

  // Resolve one export first so the proxy loads its own backend by absolute
  // path while nothing else has loaded a module with that base name.
  const auto first_probe = reinterpret_cast<stdcall_export>(GetProcAddress(proxy, lookup_key(entries[0])));
  require(first_probe != nullptr, "the proxy did not export its first pinned entry");
  int first_delta = 0;
  const auto first_result = invoke_checked(first_probe, 1, 2, 3, 4, &first_delta);
  require(first_delta == 0, "the first forwarded call did not preserve the stdcall stack contract");
  const unsigned long first_entry_point =
      entries[0].alias_of != 0 ? entries[0].alias_of : entries[0].ordinal;
  require(first_result == ncm::winmm_proxy_fixture::expected_result(first_entry_point, 1, 2, 3, 4),
          "the first forwarded call did not reach the synthetic backend");

  const HMODULE backend = LoadLibraryW(backend_path.c_str());
  require(backend != nullptr, "the backend fixture did not load: " + describe(GetLastError()));
  require(backend != proxy,
          "loading the backend by absolute path returned the proxy module, so a same-named "
          "backend cannot be reached in this process");
  require(same_path(module_path(proxy), proxy_path),
          "the proxy module is not backed by the staged application-directory file");
  require(same_path(module_path(backend), backend_path),
          "the backend module is not backed by the requested file");

  for (unsigned index = 0; index < count; ++index) {
    const auto& entry = entries[index];
    const auto label = entry_label(entry);
    const auto key = lookup_key(entry);

    const auto via_proxy = reinterpret_cast<stdcall_export>(GetProcAddress(proxy, key));
    require(via_proxy != nullptr, "the proxy does not export " + label);
    const auto via_backend = reinterpret_cast<stdcall_export>(GetProcAddress(backend, key));
    require(via_backend != nullptr, "the backend does not export " + label);

    require(owning_module(reinterpret_cast<const void*>(via_proxy)) == proxy,
            "the proxy export for " + label + " is not owned by the proxy module");
    require(owning_module(reinterpret_cast<const void*>(via_backend)) == backend,
            "the backend export for " + label + " is not owned by the backend module");

    const unsigned long first = 0x00010000UL + index;
    const unsigned long second = 0x00020000UL + index * 3UL;
    const unsigned long third = 0x00030000UL + index * 5UL;
    const unsigned long fourth = 0x00040000UL + index * 7UL;
    const unsigned long entry_point = entry.alias_of != 0 ? entry.alias_of : entry.ordinal;
    const auto expected =
        ncm::winmm_proxy_fixture::expected_result(entry_point, first, second, third, fourth);

    int proxy_delta = 0;
    const auto proxy_result = invoke_checked(via_proxy, first, second, third, fourth, &proxy_delta);
    require(proxy_delta == 0, "the forwarded call to " + label + " changed the stdcall stack contract");
    require(proxy_result == expected,
            "the forwarded call to " + label + " returned " + describe(proxy_result) +
                " instead of " + describe(expected));

    int backend_delta = 0;
    const auto backend_result =
        invoke_checked(via_backend, first, second, third, fourth, &backend_delta);
    require(backend_delta == 0, "the direct call to " + label + " changed the stdcall stack contract");
    require(backend_result == expected, "the direct call to " + label + " did not match the contract");

    if (entry.alias_of != 0) {
      const auto* aliased = entries;
      for (unsigned search = 0; search < count; ++search) {
        if (entries[search].ordinal == entry.alias_of) {
          aliased = &entries[search];
          break;
        }
      }
      require(aliased->ordinal == entry.alias_of,
              "the manifest alias target for " + label + " is missing from the surface");
      require(GetProcAddress(backend, lookup_key(*aliased)) ==
                  reinterpret_cast<FARPROC>(via_backend),
              "the backend did not preserve the shared entry point for " + label);
    }
  }

  return "forward: entries=" + std::to_string(count) + " proxy=" +
      std::to_string(reinterpret_cast<uintptr_t>(proxy)) + " backend=" +
      std::to_string(reinterpret_cast<uintptr_t>(backend)) + " distinct=yes";
}

// The proxy must stop the process rather than dispatch when it cannot reach a
// distinct backend. Returning from the call at all is the failure this checks
// for; the parent observes the exit code.
std::string run_resolve_failure_probe(const std::wstring& proxy_path) {
  require_no_preloaded_winmm();

  const HMODULE proxy = LoadLibraryW(proxy_path.c_str());
  require(proxy != nullptr, "the proxy fixture did not load: " + describe(GetLastError()));

  unsigned count{};
  const auto* entries = ncm::winmm_proxy::pinned_exports(&count);
  require(entries != nullptr && count != 0, "the pinned export table is unavailable");

  const auto target = reinterpret_cast<stdcall_export>(GetProcAddress(proxy, lookup_key(entries[0])));
  require(target != nullptr, "the proxy did not export its first pinned entry");

  int delta = 0;
  const auto result = invoke_checked(target, 1, 2, 3, 4, &delta);
  require(false, "the proxy dispatched without a backend, returning " + describe(result));
  return {};
}

// The bootstrap must resolve the backend on its own thread, without the host
// ever calling a forwarded export. Nothing here touches the proxy after the
// load, so a backend that appears can only have come from the bootstrap.
std::string run_bootstrap_probe(const std::wstring& proxy_path, const std::wstring& backend_path) {
  require_no_preloaded_winmm();
  require(GetModuleHandleW(backend_path.c_str()) == nullptr,
          "the backend was already loaded before the proxy");

  const HMODULE proxy = LoadLibraryW(proxy_path.c_str());
  require(proxy != nullptr, "the proxy fixture did not load: " + describe(GetLastError()));

  HMODULE backend{};
  for (unsigned attempt = 0; attempt < 200 && backend == nullptr; ++attempt) {
    Sleep(25);
    backend = GetModuleHandleW(backend_path.c_str());
  }
  require(backend != nullptr,
          "the bootstrap did not resolve the backend without a forwarded call");
  require(backend != proxy, "the bootstrap resolved the proxy as its own backend");

  return "bootstrap: backend-resolved=yes proxy=" +
      std::to_string(reinterpret_cast<uintptr_t>(proxy)) + " backend=" +
      std::to_string(reinterpret_cast<uintptr_t>(backend));
}

// Exercises the production proxy against the real system WinMM.
//
// Nothing is called with fabricated arguments: most WinMM entry points have
// device or timer side effects. `mmsystemGetVersion` takes no arguments and
// returns a constant, and because resolution is all-or-nothing, one successful
// forwarded call proves every pinned entry resolved in the host module.
std::string run_system_backend_probe(const std::wstring& proxy_path) {
  require_no_preloaded_winmm();

  const HMODULE proxy = LoadLibraryW(proxy_path.c_str());
  require(proxy != nullptr, "the proxy did not load: " + describe(GetLastError()));
  require(GetModuleHandleW(L"winmm.dll") == proxy,
          "the proxy did not take ownership of the winmm.dll base name");

  unsigned count{};
  const auto* entries = ncm::winmm_proxy::pinned_exports(&count);
  require(entries != nullptr && count != 0, "the pinned export table is unavailable");

  using version_export = unsigned int(__stdcall*)();
  const auto via_proxy = reinterpret_cast<version_export>(GetProcAddress(proxy, "mmsystemGetVersion"));
  require(via_proxy != nullptr, "the proxy does not export mmsystemGetVersion");
  const auto proxy_version = via_proxy();

  wchar_t system_directory[MAX_PATH]{};
  const auto directory_length =
      GetSystemDirectoryW(system_directory, static_cast<UINT>(std::size(system_directory)));
  require(directory_length != 0 && directory_length < std::size(system_directory),
          "the system directory is unavailable");
  const std::wstring system_path = std::wstring(system_directory, directory_length) + L"\\winmm.dll";

  const HMODULE system_module = GetModuleHandleW(system_path.c_str());
  require(system_module != nullptr,
          "the proxy did not load the system module while resolving its backend");
  require(system_module != proxy, "the system backend resolved to the proxy module itself");
  require(same_path(module_path(system_module), system_path),
          "the resolved backend is not the system module");

  const auto via_system =
      reinterpret_cast<version_export>(GetProcAddress(system_module, "mmsystemGetVersion"));
  require(via_system != nullptr, "the system module does not export mmsystemGetVersion");
  require(proxy_version == via_system(),
          "the forwarded mmsystemGetVersion result did not match the system module");

  unsigned missing = 0;
  for (unsigned index = 0; index < count; ++index) {
    const auto key = lookup_key(entries[index]);
    require(GetProcAddress(proxy, key) != nullptr,
            "the proxy does not export " + entry_label(entries[index]));
    if (GetProcAddress(system_module, key) == nullptr) {
      ++missing;
    }
  }
  require(missing == 0,
          "the host WinMM is missing " + describe(missing) + " pinned entries");

  std::string report = "systembackend: entries=" + std::to_string(count) +
      " version=" + describe(proxy_version) + " distinct=yes backend=";
  const auto resolved = module_path(system_module);
  report.append(resolved.begin(), resolved.end());
  return report;
}

// Negative control: a `.def` that forwards to the module name the proxy itself
// carries. Nothing here is ever called; only where the loader points is
// observed.
std::string run_self_forward_probe(const std::wstring& control_path) {
  require_no_preloaded_winmm();

  const HMODULE control = LoadLibraryW(control_path.c_str());
  if (control == nullptr) {
    return "selfforward: load-failed error=" + describe(GetLastError());
  }
  require(GetModuleHandleW(L"winmm.dll") == control,
          "the negative control did not take ownership of the winmm.dll base name");

  unsigned count{};
  const auto* entries = ncm::winmm_proxy::pinned_exports(&count);
  require(entries != nullptr && count != 0, "the pinned export table is unavailable");

  unsigned unresolved = 0;
  unsigned resolved_to_self = 0;
  unsigned resolved_elsewhere = 0;
  for (unsigned index = 0; index < count; ++index) {
    const auto address = GetProcAddress(control, lookup_key(entries[index]));
    if (address == nullptr) {
      ++unresolved;
      continue;
    }
    if (owning_module(reinterpret_cast<const void*>(address)) == control) {
      ++resolved_to_self;
    } else {
      ++resolved_elsewhere;
    }
  }

  return "selfforward: unresolved=" + std::to_string(unresolved) + " self=" +
      std::to_string(resolved_to_self) + " elsewhere=" + std::to_string(resolved_elsewhere);
}

void write_report(const std::wstring& path, const std::string& text) {
  const HANDLE file = CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written{};
  WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
  CloseHandle(file);
}

}  // namespace

int wmain(int argument_count, wchar_t** arguments) {
  if (argument_count != 4 && argument_count != 5) {
    return 2;
  }
  const std::wstring mode = arguments[1];
  const std::wstring module = arguments[2];
  const std::wstring report_path = arguments[3];

  try {
    std::string report;
    if (mode == L"forward") {
      if (argument_count != 5) {
        return 2;
      }
      report = run_forward_probe(module, arguments[4]);
    } else if (mode == L"selfforward") {
      report = run_self_forward_probe(module);
    } else if (mode == L"resolvefailure") {
      report = run_resolve_failure_probe(module);
    } else if (mode == L"systembackend") {
      report = run_system_backend_probe(module);
    } else if (mode == L"bootstrap") {
      if (argument_count != 5) {
        return 2;
      }
      report = run_bootstrap_probe(module, arguments[4]);
    } else {
      return 2;
    }
    write_report(report_path, report);
    return 0;
  } catch (const std::exception& error) {
    write_report(report_path, std::string("error: ") + error.what());
    return 1;
  }
}
