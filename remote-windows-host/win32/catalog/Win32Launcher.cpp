
#include "win32/catalog/Win32Launcher.hpp"

#ifdef _WIN32

#include "app/Win32Env.hpp"  // <windows.h> pin + SDK macro undefs

#include <cwchar>

#include <objbase.h>   // CoInitializeEx / CoCreateInstance
#include <rpc.h>       // RPC_E_CHANGED_MODE (WIN32_LEAN_AND_MEAN skips it)
#include <shlobj.h>    // SLGP_* GetPath flags
#include <shobjidl.h>  // IShellLinkW, CLSID_ShellLink
#include <shellapi.h>  // SHELLEXECUTEINFOW / ShellExecuteExW

// SDK 10.0.26100 note (plan rule 2, verified against the actual MSVC
// diagnostics on the runner): SLGP_LONGPATH is a documented Microsoft
// SLGP flag that this SDK's <shlobj.h> does NOT declare under this
// project's WINVER/_WIN32_WINNT pins — defined locally with #ifndef so
// a future SDK that declares it wins.
#ifndef SLGP_LONGPATH
#define SLGP_LONGPATH 0x00000020
#endif

namespace remotewindows::win32 {

namespace {

// Deterministic ASCII-only fold. std::towlower is NOT in std under
// MSVC 14.51 even with <cctype>/<cwchar> included (verified by the
// runner's MSVC diagnostics; plan rule 2) — and the C locale folds
// exactly this range, so behavior is unchanged.
wchar_t foldLower(wchar_t c) {
  return (c >= L'A' && c <= L'Z')
             ? static_cast<wchar_t>(c + (L'a' - L'A'))
             : c;
}

std::wstring toLower(std::wstring s) {
  for (auto& c : s) c = foldLower(c);
  return s;
}

std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                    static_cast<int>(s.size()), nullptr, 0);
  if (n <= 0) return {};
  std::wstring out(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                      out.data(), n);
  return out;
}

// Normalized path for prefix matching: backslash separators, lowercase,
// no trailing backslash (a bare drive "C:\\" normalizes to "C:" so the
// "nr + L'\\'" prefix check still matches paths on that drive).
std::wstring normalizePath(std::wstring p) {
  for (auto& c : p) {
    if (c == L'/') c = L'\\';
    c = foldLower(c);
  }
  while (p.size() > 1 && p.back() == L'\\') p.pop_back();
  return p;
}

bool endsWithLnk(const std::wstring& p) {
  return p.size() >= 4 && toLower(p.substr(p.size() - 4)) == L".lnk";
}

}  // namespace

Win32Launcher::Win32Launcher(std::vector<std::wstring> allowed_roots) {
  if (!allowed_roots.empty()) {
    allowed_roots_ = std::move(allowed_roots);
    return;
  }
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0) {
    allowed_roots_.push_back(std::wstring(appdata) +
                             L"\\Microsoft\\Windows\\Start Menu\\Programs");
  }
  allowed_roots_.push_back(
      L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs");
}

core::LaunchOutcome Win32Launcher::launch(const core::LaunchRequest& req) {
  const std::wstring target = utf8ToWide(req.launch_target);

  // --- allow-list gate: validation in order; NEVER executes on failure ---
  if (!endsWithLnk(target)) {
    return {false, "unauthorized",
            "Launch target is not an allow-listed shortcut", 0};
  }
  const DWORD attrs = GetFileAttributesW(target.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES ||
      (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return {false, "launch_failed",
            "The launch target no longer exists", 0};
  }
  const std::wstring norm = normalizePath(target);
  bool under_root = false;
  for (const auto& root : allowed_roots_) {
    const std::wstring nr = normalizePath(root);
    if (nr.empty()) continue;
    if (norm == nr ||
        (norm.size() > nr.size() + 1 &&
         norm.compare(0, nr.size() + 1, nr + L"\\") == 0)) {
      under_root = true;
      break;
    }
  }
  if (!under_root) {
    return {false, "unauthorized",
            "Launch target is not an allow-listed shortcut", 0};
  }

  // --- resolve the shortcut BEFORE executing (never let the shell
  // resolve a broken target): ShellExecuteExW on a .lnk whose target is
  // gone can block indefinitely in a headless session-0 context
  // (observed on the 24H2 runner, 2026-11-25 — the per-test 300 s
  // ctest timeout fired). GetPath itself is fast: a missing-target
  // shortcut returns its stored path (verified: the StartMenuSource
  // missing-target fixture scans in milliseconds).
  std::wstring resolved;
  {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool com_here = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
      // COM down: the shortcut cannot be verified to resolve.
      return {false, "launch_failed",
              "The application could not be started", 0};
    }
    IShellLinkW* link = nullptr;
    if (CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IShellLinkW,
                         reinterpret_cast<void**>(&link)) == S_OK &&
        link) {
      IPersistFile* file = nullptr;
      if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                         reinterpret_cast<void**>(&file))) &&
          file) {
        if (SUCCEEDED(file->Load(target.c_str(), STGM_READ))) {
          WCHAR buf[MAX_PATH] = {};
          // GetPath returns S_FALSE (SUCCEEDED!) with an empty buffer on
          // failure; the empty-buffer check is the real gate.
          if (SUCCEEDED(link->GetPath(
                  buf, MAX_PATH, nullptr, SLGP_UNCPRIORITY | SLGP_LONGPATH)) &&
              buf[0] != 0)
            resolved = buf;
        }
        file->Release();
      }
      link->Release();
    }
    if (com_here) CoUninitialize();
  }
  if (resolved.empty()) {
    // The shortcut no longer resolves to anything.
    return {false, "launch_failed", "The launch target no longer exists", 0};
  }
  {
    // URI targets (ms-appx:/ms-app:// etc.): no filesystem existence
    // check — app-model activation happens at execute time (the shell).
    const size_t scheme = resolved.find(L"://");
    const bool is_uri = scheme != std::wstring::npos && scheme <= 32;
    if (!is_uri &&
        GetFileAttributesW(resolved.c_str()) == INVALID_FILE_ATTRIBUTES) {
      return {false, "launch_failed", "The launch target no longer exists", 0};
    }
  }

  // --- execute: the shortcut is the unit of launch (FR-02) ---
  SHELLEXECUTEINFOW si{};
  si.cbSize = sizeof(si);
  si.fMask = SEE_MASK_NOCLOSEPROCESS;
  si.lpFile = target.c_str();
  si.nShow = SW_SHOWNORMAL;
  // lpParameters / lpDirectory left NULL: no arguments, no cwd override.
  if (!ShellExecuteExW(&si)) {
    // Stable generic message: never the raw OS error, never the path.
    return {false, "launch_failed",
            "The application could not be started", 0};
  }
  uint32_t pid = 0;
  core::LaunchOutcome out;
  out.ok = true;
  if (si.hProcess) {
    // Some launches report no process (e.g. some shell-handled targets):
    // pid 0 = unknown, still a success.
    pid = static_cast<uint32_t>(GetProcessId(si.hProcess));
    // SEE_MASK_NOCLOSEPROCESS: the session owns this handle now. We
    // deliberately do NOT CloseHandle it — a retained HANDLE keeps the
    // process object alive after exit, which is what makes
    // GetExitCodeProcess-based liveness reliable (Win32ProcessProbe::
    // isHandleAlive). LaunchService releases it via
    // ProcessProbe::closeProcessHandle when the session ends or is
    // replaced by a re-launch.
    out.process_handle = si.hProcess;
  }
  out.pid = pid;
  return out;
}

}  // namespace remotewindows::win32

#endif  // _WIN32
