
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
  if (!RegisterClassExW(&wc)) return 2;

  hwnd_ = CreateWindowExW(0, kWindowClass, L"RWH", 0, 0, 0, 0, 0,
                          HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                          this);
  if (!hwnd_) return 2;

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
    std::fprintf(stderr, "tray icon registration failed\n");
    return 2;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  NOTIFYICONDATAW remove{};
  remove.cbSize = sizeof(remove);
  remove.hWnd = hwnd_;
  remove.uID = kTrayId;
  Shell_NotifyIconW(NIM_DELETE, &remove);
  return 0;
}

}  // namespace remotewindows::app
