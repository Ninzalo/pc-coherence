
#include "win32/catalog/Win32Launcher.hpp"

#ifdef _WIN32

#include "app/Win32Env.hpp"  // <windows.h> pin + SDK macro undefs

#include <cwchar>

#include <shellapi.h>  // SHELLEXECUTEINFOW / ShellExecuteExW

namespace remotewindows::win32 {

namespace {

std::wstring toLower(std::wstring s) {
  for (auto& c : s) c = (wchar_t)std::towlower(c);
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
    c = (wchar_t)std::towlower(c);
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
  if (si.hProcess) {
    // Some launches report no process (e.g. some shell-handled targets):
    // pid 0 = unknown, still a success.
    pid = static_cast<uint32_t>(GetProcessId(si.hProcess));
    // SEE_MASK_NOCLOSEPROCESS: WE own this handle.
    CloseHandle(si.hProcess);
  }
  return {true, "", "", pid};
}

}  // namespace remotewindows::win32

#endif  // _WIN32
