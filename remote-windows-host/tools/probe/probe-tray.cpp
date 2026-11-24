// probe-tray.cpp — diagnostic probe for the tray window-creation failure
// (2026-11-24: CreateWindowExW returned NULL with GetLastError()==0 for
// both a message-only and a normal top-level window, in both an
// interactive desktop session and a non-interactive SSH session).
//
// Standalone console app, built and run ONLY via the runner's
// 'probe-tray' action (not part of the shipped product, not linked
// into rwh-host). Prints one line per test; no window contents or
// user data are touched (AGENTS rule 9).
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#include <windows.h>

#include <cstdarg>
#include <cstdio>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

static void report(const char* label, HWND h) {
  std::printf("%-34s hwnd=%p lasterror=%lu\n", label,
              reinterpret_cast<void*>(h), GetLastError());
}

static HWND makeWindow(const wchar_t* cls, const char* label, HWND parent,
                       DWORD style, int x, int y, int cx, int cy,
                       BOOL show) {
  HWND h =
      CreateWindowExW(0, cls, L"rwh-probe", style, x, y, cx, cy, parent,
                      nullptr, GetModuleHandleW(nullptr), nullptr);
  report(label, h);
  if (h && show) ShowWindow(h, SW_SHOWNORMAL);
  return h;
}

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);

  const DWORD our_pid = GetCurrentProcessId();
  DWORD our_session = 0;
  ProcessIdToSessionId(our_pid, &our_session);
  std::printf("pid=%lu session=%lu\n", our_pid, our_session);

  const HMODULE mod = GetModuleHandleW(nullptr);
  std::printf("module=%p\n", reinterpret_cast<void*>(mod));

  const HWND desktop = GetDesktopWindow();
  std::printf("desktop=%p\n", reinterpret_cast<void*>(desktop));
  DWORD desk_pid = 0;
  GetWindowThreadProcessId(desktop, &desk_pid);
  DWORD desk_session = 0;
  ProcessIdToSessionId(desk_pid, &desk_session);
  std::printf("desktop_owner_pid=%lu desktop_owner_session=%lu\n", desk_pid,
              desk_session);

  // Class 1: minimal — mirrors TrayController exactly (no style, no
  // cursor, no background).
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = mod;
  wc.lpszClassName = L"RWHProbeMin";
  std::printf("reg_min     atom=%lu lasterror=%lu\n", RegisterClassExW(&wc),
              GetLastError());

  const HWND h1 = makeWindow(L"RWHProbeMin", "A msgonly (minimal class)",
                             HWND_MESSAGE, 0, 0, 0, 0, 0, FALSE);
  const HWND h2 = makeWindow(L"RWHProbeMin", "B normal hidden (minimal)",
                             nullptr, WS_OVERLAPPED, -32000, -32000, 1, 1,
                             FALSE);
  const HWND h3 = makeWindow(L"RWHProbeMin", "C visible top-level (minimal)",
                             nullptr, WS_OVERLAPPED, 100, 100, 100, 80, TRUE);

  // Class 2: fully populated class (style bits, cursor, background).
  WNDCLASSEXW wf{};
  wf.cbSize = sizeof(wf);
  wf.style = CS_HREDRAW | CS_VREDRAW;
  wf.lpfnWndProc = WndProc;
  wf.hInstance = mod;
  wf.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wf.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  wf.lpszClassName = L"RWHProbeFull";
  std::printf("reg_full    atom=%lu lasterror=%lu\n", RegisterClassExW(&wf),
              GetLastError());
  const HWND h4 = makeWindow(L"RWHProbeFull", "D msgonly (full class)",
                             HWND_MESSAGE, 0, 0, 0, 0, 0, FALSE);
  const HWND h5 = makeWindow(L"RWHProbeFull", "E normal hidden (full)",
                             nullptr, WS_OVERLAPPED, -32000, -32000, 1, 1,
                             FALSE);

  // Long shot: does installing a console-control handler that swallows
  // all events change window creation?
  SetConsoleCtrlHandler([](DWORD) { return TRUE; }, TRUE);
  std::printf("ctrl_handler installed\n");
  const HWND h6 = makeWindow(L"RWHProbeFull", "F msgonly after ctrl handler",
                             HWND_MESSAGE, 0, 0, 0, 0, 0, FALSE);

  HWND wins[] = {h1, h2, h3, h4, h5, h6};
  for (HWND h : wins) {
    if (h) DestroyWindow(h);
  }
  std::printf("probe done\n");
  return 0;
}
