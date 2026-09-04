#include <Windows.h>

#include <string_view>

int wmain(int argument_count, wchar_t** arguments) {
  for (int index = 1; index < argument_count; ++index) {
    if (std::wstring_view(arguments[index]).starts_with(L"--type=")) {
      Sleep(30000);
      return 0;
    }
  }
  Sleep(30000);
  return 0;
}
