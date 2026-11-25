
#ifndef RWH_WIN32_CATALOG_WIN32LAUNCHER_HPP
#define RWH_WIN32_CATALOG_WIN32LAUNCHER_HPP

// RW-H-009 (H-CATALOG): allow-list-validated application launcher (FR-02,
// plan rule 9). WIN32-only: ShellExecuteExW. Implements core::AppLauncher;
// unit-tested only on the Windows runner (NOT RUN on macOS).

#ifdef _WIN32

#include <string>
#include <vector>

#include "core/catalog_service.hpp"

namespace remotewindows::win32 {

// Launches ONLY catalog-resolved Start Menu shortcuts. The client never
// sends paths or commands (FR-02); the launcher is an allow-list GATE,
// not an executor: every validation failure returns before
// ShellExecuteExW is touched. No arguments, no working-directory
// overrides, no elevation — the shortcut is the unit of launch.
//
// Validation order (launch()):
//   1. target ends with ".lnk" (case-insensitive)
//        -> else unauthorized "Launch target is not an allow-listed shortcut"
//   2. path exists as a file (GetFileAttributesW)
//        -> else launch_failed "The launch target no longer exists"
//   3. path is under one of the allowed roots (case-insensitive prefix
//        match on the normalized wide path)
//        -> else the same unauthorized outcome.
// Success: SHELLEXECUTEINFOW with SEE_MASK_NOCLOSEPROCESS + SW_SHOWNORMAL;
// pid = GetProcessId(hProcess) (0 is possible for some launches — pid 0
// means unknown, still ok=true); we own and CloseHandle the process
// handle. Failure: launch_failed with a stable generic safe_message —
// never the raw OS error, never the path.
class Win32Launcher : public core::AppLauncher {
 public:
  // allowed_roots: the allow-list IS the Start Menu. Empty -> per-user
  // %APPDATA%\Microsoft\Windows\Start Menu\Programs + all-users
  // C:\ProgramData\Microsoft\Windows\Start Menu\Programs (same defaults
  // as StartMenuSource).
  explicit Win32Launcher(std::vector<std::wstring> allowed_roots = {});

  core::LaunchOutcome launch(const core::LaunchRequest& req) override;

 private:
  std::vector<std::wstring> allowed_roots_;
};

}  // namespace remotewindows::win32

#endif  // _WIN32

#endif  // RWH_WIN32_CATALOG_WIN32LAUNCHER_HPP
