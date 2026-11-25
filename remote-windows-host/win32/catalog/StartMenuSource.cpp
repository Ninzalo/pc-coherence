
#include "win32/catalog/StartMenuSource.hpp"

#ifdef _WIN32

#include "app/Win32Env.hpp"  // <windows.h> pin + SDK macro undefs

#include <cwchar>

#include <objbase.h>  // CoInitializeEx / CoCreateInstance
#include <rpc.h>      // RPC_E_CHANGED_MODE (WIN32_LEAN_AND_MEAN skips it)
#include <shlobj.h>   // SLGP_UNCPRIORITY GetPath flag
#include <shobjidl.h>  // IShellLinkW, CLSID_ShellLink

namespace remotewindows::win32 {

namespace {

constexpr int kMaxDepth = 10;  // Start Menu nesting cap (design note)

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

std::string wideToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(),
                                    static_cast<int>(w.size()), nullptr, 0,
                                    nullptr, nullptr);
  if (n <= 0) return {};
  std::string out(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                      out.data(), n, nullptr, nullptr);
  return out;
}

std::wstring fileName(const std::wstring& path) {
  const size_t i = path.find_last_of(L"\\/");
  return i == std::wstring::npos ? path : path.substr(i + 1);
}

// Trim ASCII whitespace (descriptions are user-set; stray spaces must not
// create a distinct identity for the same app).
std::wstring trim(const std::wstring& s) {
  size_t b = 0, e = s.size();
  while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' ||
                   s[b] == L'\n'))
    ++b;
  while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' ||
                   s[e - 1] == L'\n'))
    --e;
  return s.substr(b, e - b);
}

// PROTOCOL §5: display_name is 1..256 chars. Cap at a valid UTF-8
// character boundary; a malformed byte stops the count (valid prefix kept).
std::string capDisplayChars(const std::string& s) {
  size_t chars = 0, i = 0;
  while (i < s.size() && chars < 256) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) i += 1;
    else if ((c & 0xC0) == 0xC0) i += 2;
    else if ((c & 0xE0) == 0xE0) i += 3;
    else if ((c & 0xF0) == 0xF0) i += 4;
    else break;
    ++chars;
  }
  return s.substr(0, i);
}

// display_name: description if non-empty, else the .lnk file name without
// extension, else the target's file name.
std::string displayName(const std::wstring& description,
                        const std::wstring& lnk_path,
                        const std::wstring& target) {
  const std::wstring d = trim(description);
  if (!d.empty()) return capDisplayChars(wideToUtf8(d));
  std::wstring base = fileName(lnk_path);
  const size_t dot = base.rfind(L'.');
  if (dot != std::wstring::npos && dot > 0) base = base.substr(0, dot);
  if (!base.empty()) return capDisplayChars(wideToUtf8(base));
  return capDisplayChars(wideToUtf8(fileName(target)));
}

bool targetLooksLikeMsUri(const std::wstring& t) {
  const std::wstring lt = toLower(t);
  return lt.rfind(L"ms-appx:", 0) == 0 || lt.rfind(L"ms-app://", 0) == 0;
}

// Non-Microsoft-app URI target (https://, ftp://, ...): web links and
// generic protocol handlers are not apps (FR-01) — skip. A real
// filesystem path never contains "://"; the scheme must start the
// string (cap 32 keeps odd targets like "C:\foo://bar" classified as
// paths).
bool targetLooksLikeOtherUri(const std::wstring& t) {
  if (targetLooksLikeMsUri(t)) return false;
  const size_t i = t.find(L"://");
  return i != std::wstring::npos && i <= 32;
}

bool targetAvailable(const std::wstring& target) {
  // ms-appx:/ms-app:// URIs cannot be verified offline; report available
  // (honest per design note) — a launch failure would surface as
  // launch_failed with a safe message.
  if (targetLooksLikeMsUri(target)) return true;
  // Drive paths (X:\...) and any other resolvable path (UNC/relative):
  // existence check.
  return GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
}

}  // namespace

StartMenuSource::StartMenuSource(std::vector<std::wstring> roots) {
  if (!roots.empty()) {
    roots_ = std::move(roots);
    return;
  }
  // Per-user: %APPDATA%\Microsoft\Windows\Start Menu\Programs (APPDATA is
  // per-user, so this is the per-user root; missing APPDATA -> skip).
  wchar_t appdata[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) > 0) {
    roots_.push_back(std::wstring(appdata) +
                     L"\\Microsoft\\Windows\\Start Menu\\Programs");
  }
  // All-users.
  roots_.push_back(
      L"C:\\ProgramData\\Microsoft\\Windows\\Start Menu\\Programs");
}

void StartMenuSource::walkDir(const std::wstring& dir, int depth,
                              std::vector<core::CatalogRecord>& out) const {
  if (depth > kMaxDepth) return;
  WIN32_FIND_DATAW fd{};
  const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;  // missing/unreadable root: skip
  do {
    if (fd.cFileName[0] == L'.') continue;  // "." and ".."
    // HIDDEN/SYSTEM entries are skipped at every level (files AND
    // directories — a hidden subdirectory is not walked).
    if (fd.dwFileAttributes &
        (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
      continue;
    const std::wstring full = dir + L"\\" + fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      walkDir(full, depth + 1, out);
    } else {
      std::wstring name = toLower(fd.cFileName);
      if (name.size() >= 4 && name.compare(name.size() - 4, 4, L".lnk") == 0)
        parseLnk(full, out);
    }
  } while (FindNextFileW(h, &fd) != 0);
  FindClose(h);
}

void StartMenuSource::parseLnk(const std::wstring& lnk_path,
                               std::vector<core::CatalogRecord>& out) const {
  IShellLinkW* link = nullptr;
  if (CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IShellLinkW,
                       reinterpret_cast<void**>(&link)) != S_OK ||
      !link)
    return;
  IPersistFile* file = nullptr;
  if (FAILED(link->QueryInterface(IID_IPersistFile,
                                  reinterpret_cast<void**>(&file))) ||
      !file) {
    link->Release();
    return;
  }
  const bool ok = SUCCEEDED(file->Load(lnk_path.c_str(), STGM_READ));
  file->Release();
  if (!ok) {
    link->Release();
    return;
  }

  WCHAR target[MAX_PATH] = {};
  // pfd == NULL requires SLGP_UNCPRIORITY (MSDN).
  //
  // Deliberately NO SLGP_FILESYSTEM: that flag makes GetPath FAIL for
  // URI targets (ms-appx:/ms-app:// shortcuts — real apps in the Start
  // Menu), which would silently drop them from the catalog. URI targets
  // come back as strings and are classified after GetPath: ms-appx:/
  // ms-app:// are kept (targetAvailable), other schemes are skipped
  // (web links are not apps, FR-01).
  //
  // SDK 10.0.26100 notes (plan rule 2, verified against the actual MSVC
  // diagnostics on the runner): (a) the C++ IShellLinkW::GetPath takes
  // FOUR arguments (the historical 3-argument form does not exist in
  // this SDK); (b) SLGP_LONGPATH is a documented Microsoft SLGP flag
  // that this SDK's <shlobj.h> does NOT declare under this project's
  // WINVER/_WIN32_WINNT pins — defined locally with #ifndef so a
  // future SDK that declares it wins; (c) there is no
  // FILENORMALIZELONGPATH constant anywhere.
#ifndef SLGP_LONGPATH
#define SLGP_LONGPATH 0x00000020
#endif
  const HRESULT hrg =
      link->GetPath(target, MAX_PATH, nullptr, SLGP_UNCPRIORITY | SLGP_LONGPATH);
  // GetPath returns S_FALSE (SUCCEEDED!) with an empty buffer on failure;
  // the empty-buffer check is the real gate.
  if (FAILED(hrg) || target[0] == 0) {
    link->Release();
    return;  // unparseable shortcut: skip
  }

  const std::wstring wtarget(target);
  if (targetLooksLikeOtherUri(wtarget)) {
    link->Release();
    return;  // web link / generic URI: not an app (FR-01)
  }

  WCHAR desc[512] = {};
  link->GetDescription(desc, 512);  // best-effort; empty on failure
  link->Release();

  core::CatalogRecord rec;
  rec.display_name = displayName(desc, lnk_path, wtarget);
  if (rec.display_name.empty()) return;  // cannot derive a name: skip
  rec.identity_key = core::normalizeIdentity(rec.display_name);
  rec.launch_target = wideToUtf8(lnk_path);
  rec.source = "start_menu";
  rec.priority = 10;
  rec.available = targetAvailable(wtarget);
  rec.icon_ref = "";  // MVP placeholder (later task)
  out.push_back(std::move(rec));
}

std::vector<core::CatalogRecord> StartMenuSource::scan() {
  std::vector<core::CatalogRecord> out;
  // Per-call COM; scan() may run on any session worker thread.
  // RPC_E_CHANGED_MODE (thread already STA) means do NOT CoUninitialize.
  const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return out;  // COM down
  for (const auto& root : roots_) walkDir(root, 0, out);
  if (SUCCEEDED(hr)) CoUninitialize();
  return out;
}

}  // namespace remotewindows::win32

#endif  // _WIN32
