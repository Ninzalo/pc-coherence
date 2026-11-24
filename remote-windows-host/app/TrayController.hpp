#ifndef RWH_APP_TRAYCONTROLLER_HPP
#define RWH_APP_TRAYCONTROLLER_HPP

// Win32 notification-area (tray) presence for the Windows Host (H-FOUND).
// All user-facing strings come from the en-US.rc string table only
// (plan rule 3 / FR-14).

#include <cstddef>
#include <cstdint>
#include <functional>

struct HWND__;  // opaque Win32 handle (kept out of the header)
using LRESULT = long;  // Win32 LRESULT without including <windows.h>

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
  // Win32 message dispatch (static, routed through GWLP_USERDATA).
  static LRESULT WINAPI wndProc(HWND__* hwnd, unsigned int msg,
                                uintptr_t wParam, intptr_t lParam);
  void showMenu(HWND__* hwnd);
  void showStatus();

  std::function<TrayStatus()> statusFn_;
  HWND__* hwnd_ = nullptr;
};

}  // namespace remotewindows::app

#endif
