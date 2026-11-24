
#include "app/TrayController.hpp"

#include <shellapi.h>
#include <windows.h>

#include <cstdio>

#include "app/resources/resource.h"

namespace remotewindows::app {

namespace {
constexpr wchar_t kWindowClass[] = L"RWHTrayWindow";
constexpr UINT kTrayId = 1;
constexpr UINT kMenuStatus = 1;
constexpr UINT kMenuExit = 2;
constexpr unsigned int kTrayCallback = WM_APP + 1;

TrayController* selfOf(HWND* hwnd) {
  return reinterpret_cast<TrayController*>(
      GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}
}  // namespace

TrayController::TrayController(std::function<TrayStatus()> statusFn)
    : statusFn_(std::move(statusFn)) {}

TrayController::~TrayController() = default;

LRESULT WINAPI TrayController::wndProc(HWND* hwnd, unsigned int msg,
                                       uintptr_t wParam,
                                       intptr_t lParam) {
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
        else if (wParam == kMenuExit) PostQuitMessage(0);
      }
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void TrayController::showMenu(HWND* hwnd) {
  HMODULE mod = GetModuleHandleW(nullptr);
  HMENU menu = CreatePopupMenu();
  if (!menu) return;
  wchar_t item[128];
  LoadStringW(mod, IDS_MENU_STATUS, item, ARRAYSIZE(item));
  AppendMenuW(menu, MF_STRING, kMenuStatus, item);
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
  wchar_t text[192];
  StringFormatW(format, ARRAYSIZE(text), L"%d", (DWORD)s.port);
  StringFormatW(text, ARRAYSIZE(text), L"%d", (DWORD)s.connections);
  MessageBoxW(hwnd_, text, L"Remote Windows Host", MB_OK | MB_ICON_INFORMATION);
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
