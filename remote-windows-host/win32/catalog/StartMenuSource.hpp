
#ifndef RWH_WIN32_CATALOG_STARTMENUSOURCE_HPP
#define RWH_WIN32_CATALOG_STARTMENUSOURCE_HPP

// RW-H-009 (H-CATALOG): Windows Start Menu catalog source (FR-01).
// WIN32-only: IShellLinkW parsing of *.lnk under the per-user and
// all-users Start Menu "Programs" folders. Implements core::CatalogSource;
// unit-tested only on the Windows runner (NOT RUN on macOS).

#ifdef _WIN32

#include <string>
#include <vector>

#include "core/catalog_service.hpp"

namespace remotewindows::win32 {

// Scans the Start Menu roots for *.lnk shortcuts: FindFirstFileW/
// FindNextFileW walk (recursive, subdirectories), entries with HIDDEN or
// SYSTEM attribute skipped, depth capped at 10. Each shortcut is parsed
// with IShellLinkW (CoCreateInstance per scan).
//
// COM: CoInitializeEx(COINIT_MULTITHREADED) per scan() call; scan() may
// run on any per-connection worker thread, so RPC_E_CHANGED_MODE is
// treated as "already initialized" and no CoUninitialize happens in that
// case (standard pattern).
//
// Record mapping (core::CatalogRecord):
//   launch_target = full .lnk path (UTF-8);
//   display_name  = shortcut description if non-empty, else the .lnk file
//                   name without extension, else the target's file name
//                   (capped at 256 UTF-8 chars, PROTOCOL §5);
//   identity_key  = core::normalizeIdentity(display_name);
//   source "start_menu", priority 10, icon_ref "" (MVP: client shows a
//   placeholder; icons are a later task).
//   available = target looks like a drive path (X:\...) or other
//   resolvable path -> GetFileAttributesW != INVALID_FILE_ATTRIBUTES;
//   target is an ms-appx:/ms-app:// URI -> true (cannot verify offline;
//   honest). URL-only shortcuts (no filesystem target, GetPath fails)
//   are SKIPPED — web links are not apps (FR-01).
//
// app_id is left empty: CatalogStore derives it via core::makeAppId
// (stable across re-scans). CatalogService handles cross-root dedupe by
// identity_key. Never logs scanned file names or paths — counts and
// durations only (structured log discipline; the app shell logs
// catalog.ready with entry count + revision + duration_ms).
class StartMenuSource : public core::CatalogSource {
 public:
  // roots: explicit Start Menu "Programs" folders (unit tests use temp
  // dirs). Empty -> per-user %APPDATA%\Microsoft\Windows\Start Menu\
  // Programs + all-users C:\ProgramData\Microsoft\Windows\Start Menu\
  // Programs.
  explicit StartMenuSource(std::vector<std::wstring> roots = {});

  std::vector<core::CatalogRecord> scan() override;

 private:
  void walkDir(const std::wstring& dir, int depth,
               std::vector<core::CatalogRecord>& out) const;
  // Parses one .lnk; appends a record on success. COM must be
  // initialized by the caller (scan()).
  void parseLnk(const std::wstring& lnk_path,
                std::vector<core::CatalogRecord>& out) const;

  std::vector<std::wstring> roots_;
};

}  // namespace remotewindows::win32

#endif  // _WIN32

#endif  // RWH_WIN32_CATALOG_STARTMENUSOURCE_HPP
