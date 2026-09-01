#include <Windows.h>

#include <iterator>

namespace {

void write_marker() noexcept {
  wchar_t path[1024]{};
  const auto length = GetEnvironmentVariableW(
      L"NCM_MODULE_LOAD_PROBE_DLL_MARKER", path,
      static_cast<DWORD>(std::size(path)));
  if (length == 0 || length >= std::size(path)) {
    return;
  }
  const auto file = CreateFileW(
      path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file != INVALID_HANDLE_VALUE) {
    CloseHandle(file);
  }
}

}  // namespace

extern "C" __declspec(dllexport) int module_load_probe_fixture_ping() {
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    write_marker();
  }
  return TRUE;
}
