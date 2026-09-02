// Synthetic stand-in for a browser runtime's self-identification exports.
//
// The real module cannot be redistributed or committed, so the probe is
// validated against a fixture that exposes the same three exports under both
// x86 argument-cleanup contracts. Building it twice is what makes the probe's
// convention measurement falsifiable: a probe that simply assumed `__cdecl`
// would still pass against the `__cdecl` build and fail here.
//
// The reported values are deliberately synthetic and do not describe any real
// browser build.

#include <Windows.h>

#if defined(NCM_CEF_FIXTURE_STDCALL)
#define NCM_FIXTURE_CALL __stdcall
#else
#define NCM_FIXTURE_CALL __cdecl
#endif

extern "C" {

// Distinct per entry, so an off-by-one in the probe's iteration is visible
// rather than absorbed.
int NCM_FIXTURE_CALL cef_version_info(int entry) {
  if (entry < 0 || entry > 5) {
    return 0;
  }
  return entry * 100 + 7;
}

const char* NCM_FIXTURE_CALL cef_api_hash(int entry) {
  switch (entry) {
    case 0:
      return "fixture-platform-0";
    case 1:
      return "fixture-universal-1";
    case 2:
      return "fixture-commit-2";
    default:
      return nullptr;
  }
}

int NCM_FIXTURE_CALL cef_build_revision(void) {
  return 1750;
}

}  // extern "C"

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
  return TRUE;
}
