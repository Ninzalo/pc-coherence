
#include "app/TrayController.hpp"

#include "app/Win32Env.hpp"

#include <shellapi.h>

#include <cstdio>
#include <cwchar>

#include "app/resources/resource.h"

namespace remotewindows::app {

namespace {
constexpr wchar_t kWindowClass[] = L"RWHTrayWindow";
constexpr UINT kTrayId = 1;
constexpr UINT kMenuStatus = 1;
constexpr UINT kMenuExit = 2;
constexpr UINT kMenuPairingCode = 3;
constexpr UINT kTrayCallback = WM_APP + 1;

TrayController* selfOf(HWND hwnd) {
  return reinterpret_cast<TrayController*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
}  // namespace

TrayController::TrayController(std::function<TrayStatus()> statusFn,
                               std::function<std::string()> pairingCodeFn)
    : statusFn_(std::move(statusFn)),
      pairingCodeFn_(std::move(pairingCodeFn)) {}

TrayController::~TrayController() = default;

LRESULT CALLBACK TrayController::wndProc(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) {
  TrayController* self = selfOf(hwnd);
  switch (msg) {
    case WM_NCCREATE: {
      auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                        reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
      return 0;
    }
    case kTrayCallback:
      if (self) {
        const DWORD evt = LOWORD(lParam);
        if (evt == WM_LBUTTONUP || evt == WM_RBUTTONUP) self->showMenu(hwnd);
      }
      return 0;
    case WM_COMMAND:
      if (self) {
        if (wParam == kMenuStatus) self->showStatus();
        else if (wParam == kMenuPairingCode) self->showPairingCode();
        else if (wParam == kMenuExit) PostQuitMessage(0);
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void TrayController::showMenu(HWND hwnd) {
  HMODULE mod = GetModuleHandleW(nullptr);
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  wchar_t item[128];
  LoadStringW(mod, IDS_MENU_STATUS, item, ARRAYSIZE(item));
  AppendMenuW(menu, MF_STRING, kMenuStatus, item);
  if (pairingCodeFn_) {
    LoadStringW(mod, IDS_MENU_PAIRING_CODE, item, ARRAYSIZE(item));
    AppendMenuW(menu, MF_STRING, kMenuPairingCode, item);
  }
  LoadStringW(mod, IDS_MENU_EXIT, item, ARRAYSIZE(item));
  AppendMenuW(menu, MF_STRING, kMenuExit, item);

  POINT pt{0, 0};
  GetCursorPos(&pt);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
  DestroyMenu(menu);
}

void TrayController::showStatus() {
  HMODULE mod = GetModuleHandleW(nullptr);
  wchar_t format[128];
  LoadStringW(mod, IDS_STATUS_FORMAT, format, ARRAYSIZE(format));
  TrayStatus s = statusFn_();
  wchar_t text[256];
  wsprintfW(text, format, (UINT)s.port, (UINT)s.connections,
            (UINT)s.pairing_sessions);
  wchar_t title[64];
  LoadStringW(mod, IDS_APP_TITLE, title, ARRAYSIZE(title));
  MessageBoxW(hwnd_, text, title, MB_OK | MB_ICONINFORMATION);
}

void TrayController::showPairingCode() {
  // FR-13 UI channel: the one-time code is displayed via MessageBox
  // ONLY. It is never logged and never written to a file (AGENTS rule 9).
  HMODULE mod = GetModuleHandleW(nullptr);
  wchar_t format[128];
  LoadStringW(mod, IDS_PAIRING_CODE_FORMAT, format, ARRAYSIZE(format));
  const std::string code = pairingCodeFn_();
  const std::wstring wcode(code.begin(), code.end());  // 6 ASCII digits
  wchar_t text[256];
  wsprintfW(text, format, wcode.c_str());
  wchar_t title[64];
  LoadStringW(mod, IDS_PAIRING_CODE_TITLE, title, ARRAYSIZE(title));
  MessageBoxW(hwnd_, text, title, MB_OK | MB_ICONINFORMATION);
}

int TrayController::run() {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &TrayController::wndProc;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = kWindowClass;
  if (!RegisterClassExW(&wc)) {
    const DWORD e = GetLastError();
    err_ = "RegisterClassExW failed: " + std::to_string(e);
    std::fprintf(stderr, "%s\n", err_.c_str());
    return 2;
  }

  HMODULE module_h = GetModuleHandleW(nullptr);
  hwnd_ = CreateWindowExW(0, kWindowClass, L"RWH", 0, 0, 0, 0, 0,
                          HWND_MESSAGE, nullptr, module_h, this);
  if (!hwnd_) {
    const DWORD e_msgonly = GetLastError();
    // Fallback: a hidden normal top-level window. A message-only
    // window is a convenience, not a requirement — a hidden
    // top-level window is a fully valid notification-area callback
    // target (diagnosis 2026-11-24: msg-only creation failed with
    // GetLastError()==0 in both an interactive desktop session and
    // a non-interactive session; the hidden window is the fix).
    hwnd_ = CreateWindowExW(0, kWindowClass, L"RWH", 0, -32000, -32000,
                            1, 1, nullptr, nullptr, module_h, this);
    if (hwnd_) {
      window_fallback_ = true;
    } else {
      const DWORD e_normal = GetLastError();
      err_ = "CreateWindowExW failed: msgonly=" + std::to_string(e_msgonly)
             + " normal=" + std::to_string(e_normal)
             + " pid=" + std::to_string(GetCurrentProcessId())
             + " module=" + (module_h != nullptr ? "ok" : "null");
      std::fprintf(stderr, "%s\n", err_.c_str());
      return 2;
    }
  }

  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd_;
  nid.uID = kTrayId;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = kTrayCallback;
  nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);  // placeholder icon;
                                                   // product icon: packaging
  HMODULE mod = GetModuleHandleW(nullptr);
  LoadStringW(mod, IDS_TRAY_TOOLTIP, nid.szTip, ARRAYSIZE(nid.szTip));
  if (!Shell_NotifyIconW(NIM_ADD, &nid)) {
    const DWORD e = GetLastError();
    err_ = "Shell_NotifyIconW(NIM_ADD) failed: " + std::to_string(e);
    std::fprintf(stderr, "%s\n", err_.c_str());
    return 3;
  }

  MSG msg{};
  int loop_rc = 0;  // 0 = normal WM_QUIT (menu "Exit"); 4 = GetMessage error
  while (true) {
    const LRESULT r = GetMessageW(&msg, nullptr, 0, 0);
    if (r <= 0) {
      if (r < 0) {
        const DWORD e = GetLastError();
        err_ = "GetMessageW error: " + std::to_string(e);
        std::fprintf(stderr, "%s\n", err_.c_str());
        loop_rc = 4;
      }
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  NOTIFYICONDATAW remove{};
  remove.cbSize = sizeof(remove);
  remove.hWnd = hwnd_;
  remove.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &remove);
  return loop_rc;
}

}  // namespace remotewindows::app
