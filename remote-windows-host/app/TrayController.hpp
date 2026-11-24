#ifndef RWH_APP_TRAYCONTROLLER_HPP
#define RWH_APP_TRAYCONTROLLER_HPP

// Win32 notification-area (tray) presence for the Windows Host (H-FOUND).
// All user-facing strings come from the en-US.rc string table only
// (plan rule 3 / FR-14). Windows-only: <windows.h> is a hard dependency.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <windows.h>

namespace remotewindows::app {

struct TrayStatus {
  uint16_t port = 0;
  std::size_t connections = 0;
};

class TrayController {
 public:
  explicit TrayController(std::function<TrayStatus()> statusFn);
  ~TrayController();

  // Registers the tray icon and runs the message loop. Returns when the
  // user picks Exit from the tray menu. 0 = normal exit.
  int run();

 private:
  static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                  LPARAM lParam);
  void showMenu(HWND hwnd);
  void showStatus();

  std::function<TrayStatus()> statusFn_;
  HWND hwnd_ = nullptr;
};

}  // namespace remotewindows::app

#endif
