
#include "win32/catalog/Win32ProcessProbe.hpp"

#ifdef _WIN32

#include "app/Win32Env.hpp"  // <windows.h> pin + SDK macro undefs

namespace remotewindows::win32 {

bool Win32ProcessProbe::isAlive(uint32_t pid) {
  const HANDLE h =
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h) {
    // Conservative: an access-restricted RUNNING process must not produce
    // a false "closed" event (access failure is indistinguishable from a
    // running process we cannot query). Documented in the header.
    return true;
  }
  DWORD code = 0;
  const bool alive =
      GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return alive;
}

}  // namespace remotewindows::win32

#endif  // _WIN32
