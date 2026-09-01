#include <Windows.h>

#include <iterator>

extern "C" __declspec(dllimport) int module_load_probe_fixture_ping();

int wmain() {
  wchar_t path[1024]{};
  const auto length = GetEnvironmentVariableW(
      L"NCM_MODULE_LOAD_PROBE_EXE_MARKER", path,
      static_cast<DWORD>(std::size(path)));
  if (length > 0 && length < std::size(path)) {
    const auto file = CreateFileW(
        path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
    }
  }
  return module_load_probe_fixture_ping();
}
