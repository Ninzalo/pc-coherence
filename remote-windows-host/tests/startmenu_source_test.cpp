
// RW-H-009: Win32 Start Menu catalog source test (FR-01).
// Compiled and run ONLY on the Windows runner (MSVC + Windows SDK); on
// macOS this test is NOT RUN. The .lnk fixture files are created with
// IShellLinkW (SetPath + SetDescription + IPersistFile::Save) under temp
// roots; the temp tree is cleaned up after each test.

#if !defined(_WIN32)
#error "startmenu_source_test.cpp is a Windows-only test (IShellLinkW)"
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app/Win32Env.hpp"

#include <objbase.h>  // CoInitializeEx / CoCreateInstance
#include <rpc.h>      // RPC_E_CHANGED_MODE (WIN32_LEAN_AND_MEAN skips it)
#include <shobjidl.h>  // IShellLinkW, CLSID_ShellLink

#include "core/catalog.hpp"
#include "core/catalog_service.hpp"
#include "win32/catalog/StartMenuSource.hpp"

using namespace remotewindows;
using core::makeAppId;
using core::normalizeIdentity;

namespace {

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

// COM for the test thread (same per-call pattern the source uses; the
// test inits MTA first, so StartMenuSource::scan() then sees
// RPC_E_CHANGED_MODE/S_FALSE and must balance accordingly).
class ComInit {
 public:
  ComInit() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ok_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    owns_ = SUCCEEDED(hr);
  }
  ~ComInit() {
    if (owns_) CoUninitialize();
  }
  bool ok() const { return ok_; }

 private:
  bool ok_ = false;
  bool owns_ = false;
};

// Creates a .lnk at `path` pointing at `target` with `description` (may
// be empty — exercises the display-name fallback). Uses the documented
// IShellLinkW fixture recipe: CoCreateInstance -> SetPath ->
// SetDescription -> IPersistFile::Save.
bool makeLnk(const std::wstring& path, const std::wstring& target,
             const std::wstring& description) {
  IShellLinkW* link = nullptr;
  if (CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                       IID_IShellLinkW,
                       reinterpret_cast<void**>(&link)) != S_OK ||
      !link)
    return false;
  IPersistFile* file = nullptr;
  bool ok = false;
  if (SUCCEEDED(link->QueryInterface(IID_IPersistFile,
                                     reinterpret_cast<void**>(&file))) &&
      file) {
    // SDK 10.0.26100 C++ signature: SetPath takes the path only (the
    // historical WIN32_FIND_DATA* parameter is not part of it).
    const HRESULT hrp = link->SetPath(target.c_str());
    const HRESULT hrd =
        description.empty() ? S_OK : link->SetDescription(description.c_str());
    const HRESULT hrs = (hrp == S_OK && SUCCEEDED(hrd))
                            ? file->Save(path.c_str(), TRUE)
                            : static_cast<HRESULT>(S_FALSE);
    ok = SUCCEEDED(hrs);
    file->Release();
  }
  link->Release();
  return ok;
}

void deleteTree(const std::wstring& dir) {
  WIN32_FIND_DATAW fd{};
  const HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    if (fd.cFileName[0] == L'.') continue;
    const std::wstring full = dir + L"\\" + fd.cFileName;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      deleteTree(full);  // depth-first post-order
    else
      DeleteFileW(full.c_str());
  } while (FindNextFileW(h, &fd) != 0);
  FindClose(h);
  RemoveDirectoryW(dir.c_str());
}

// Per-test temp tree: <tmp>\rwh-h009-<pid>-<tick>-<seq>\{user,all,outside}.
class TempTree {
 public:
  TempTree() {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    static std::atomic<int> seq{0};
    base_ = std::wstring(tmp) + L"rwh-h009-" +
            std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(GetTickCount64()) + L"-" +
            std::to_wstring(++seq);
    user_ = base_ + L"\\user";
    all_ = base_ + L"\\all";
    outside_ = base_ + L"\\outside";
    for (const auto& d : {base_, user_, all_, outside_}) {
      if (!CreateDirectoryW(d.c_str(), nullptr) &&
          GetLastError() != ERROR_ALREADY_EXISTS)
        ADD_FAILURE() << "CreateDirectoryW(" << wideToUtf8(d) << "): "
                      << GetLastError();
    }
  }
  ~TempTree() { deleteTree(base_); }

  std::wstring base_, user_, all_, outside_;
};

// %SystemRoot%\System32\notepad.exe — a real, always-present target on
// the runner. Fails loudly if missing (tests must not silently skip).
std::wstring notepadPath() {
  wchar_t sr[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"SystemRoot", sr, MAX_PATH) == 0)
    return {};
  const std::wstring p = std::wstring(sr) + L"\\System32\\notepad.exe";
  return GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring{}
                                                                  : p;
}

const core::CatalogRecord* findByLnk(const std::vector<core::CatalogRecord>& recs,
                                     const std::wstring& lnk_path) {
  const std::string u = wideToUtf8(lnk_path);
  for (const auto& r : recs)
    if (r.launch_target == u) return &r;
  return nullptr;
}

}  // namespace

TEST(StartMenuSource, DisplayNameFallbacksAndAvailability) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;
  const std::wstring notepad = notepadPath();
  ASSERT_FALSE(notepad.empty()) << "notepad.exe missing on this runner";

  // Description wins over the file name.
  const std::wstring good = tree.user_ + L"\\good.lnk";
  ASSERT_TRUE(makeLnk(good, notepad, L"Note Pad"));
  // No description: display_name = .lnk file name without extension.
  const std::wstring nodefault = tree.user_ + L"\\nodefault.lnk";
  ASSERT_TRUE(makeLnk(nodefault, notepad, L""));
  // Existing vs missing target drives `available`.
  const std::wstring missing = tree.all_ + L"\\missing.lnk";
  ASSERT_TRUE(makeLnk(missing, L"C:\\rwh-h009-definitely-missing\\nope.exe",
                      L"Gone App"));
  // ms-appx: URI target: available = true (cannot verify offline; honest).
  const std::wstring msuri = tree.all_ + L"\\msuri.lnk";
  ASSERT_TRUE(makeLnk(
      msuri,
      L"ms-appx://microsoft.microsoftedge_8wekyb3d8bbwe/microsoft-edge",
      L"Edge URI"));

  win32::StartMenuSource src({tree.user_, tree.all_});
  const std::vector<core::CatalogRecord> recs = src.scan();

  const core::CatalogRecord* r = findByLnk(recs, good);
  ASSERT_NE(r, nullptr) << "good.lnk not parsed";
  EXPECT_EQ(r->display_name, "Note Pad");
  EXPECT_EQ(r->identity_key, normalizeIdentity("Note Pad"));
  EXPECT_EQ(r->source, "start_menu");
  EXPECT_EQ(r->priority, 10u);
  EXPECT_EQ(r->icon_ref, "");
  EXPECT_TRUE(r->available);

  r = findByLnk(recs, nodefault);
  ASSERT_NE(r, nullptr) << "nodefault.lnk not parsed";
  EXPECT_EQ(r->display_name, "nodefault");
  EXPECT_TRUE(r->available);

  r = findByLnk(recs, missing);
  ASSERT_NE(r, nullptr) << "missing.lnk not parsed";
  EXPECT_EQ(r->display_name, "Gone App");
  EXPECT_FALSE(r->available);

  r = findByLnk(recs, msuri);
  ASSERT_NE(r, nullptr) << "msuri.lnk not parsed";
  EXPECT_EQ(r->display_name, "Edge URI");
  EXPECT_TRUE(r->available);
}

TEST(StartMenuSource, CrossRootDedupeToOneRecord) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;
  const std::wstring notepad = notepadPath();
  ASSERT_FALSE(notepad.empty());

  // Same app (same display name) offered from BOTH roots via different
  // .lnk paths: the raw scan has two records, CatalogService collapses
  // them to one entry via identity_key.
  ASSERT_TRUE(makeLnk(tree.user_ + L"\\dup.lnk", notepad, L"Duplicate App"));
  ASSERT_TRUE(makeLnk(tree.all_ + L"\\dup2.lnk", notepad, L"Duplicate App"));

  win32::StartMenuSource src({tree.user_, tree.all_});
  const std::vector<core::CatalogRecord> recs = src.scan();
  size_t raw = 0;
  for (const auto& r : recs)
    if (r.identity_key == normalizeIdentity("Duplicate App")) ++raw;
  EXPECT_EQ(raw, 2u) << "both roots must yield a record";

  core::CatalogService svc(&src);
  const protocol::AppCatalog snap = svc.snapshot();
  size_t entries = 0;
  for (const auto& e : snap.entries)
    if (e.display_name == "Duplicate App") ++entries;
  EXPECT_EQ(entries, 1u) << "cross-root dedupe by identity_key";
  EXPECT_GE(snap.catalog_revision, 1u);
}

TEST(StartMenuSource, AppIdsStableAcrossRescans) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;
  const std::wstring notepad = notepadPath();
  ASSERT_FALSE(notepad.empty());

  ASSERT_TRUE(makeLnk(tree.user_ + L"\\a.lnk", notepad, L"Stable A"));
  ASSERT_TRUE(makeLnk(tree.all_ + L"\\b.lnk", notepad, L"Stable B"));

  win32::StartMenuSource src({tree.user_, tree.all_});
  core::CatalogService svc(&src);
  const protocol::AppCatalog s1 = svc.snapshot();
  const protocol::AppCatalog s2 = svc.snapshot(/*force_rescan=*/true);
  ASSERT_EQ(s1.entries.size(), 2u);
  ASSERT_EQ(s1.entries.size(), s2.entries.size());
  for (size_t i = 0; i < s1.entries.size(); ++i)
    EXPECT_EQ(s1.entries[i].app_id, s2.entries[i].app_id)
        << "app_id must be stable across rescans";
  // app_id = core::makeAppId(identity_key, launch_target) — derived, not
  // source-provided. (The wire protocol::AppEntry carries no
  // launch_target; the expected target is the .lnk path the fixture
  // created.)
  const std::string a_id =
      makeAppId(normalizeIdentity("Stable A"),
                wideToUtf8(tree.user_ + L"\\a.lnk"));
  const std::string b_id =
      makeAppId(normalizeIdentity("Stable B"),
                wideToUtf8(tree.all_ + L"\\b.lnk"));
  bool saw_a = false, saw_b = false;
  for (const auto& e : s1.entries) {
    if (e.app_id == a_id)
      saw_a = true;
    else if (e.app_id == b_id)
      saw_b = true;
    else
      FAIL() << "unexpected app_id " << e.app_id;
    EXPECT_FALSE(e.app_id.empty());
  }
  EXPECT_TRUE(saw_a);
  EXPECT_TRUE(saw_b);
}

TEST(StartMenuSource, UrlOnlyShortcutSkipped) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;

  // URL-only shortcut (SetPath stores a URL target; GetPath with
  // SLGP_FILESYSTEM fails for it) must NOT appear: web links are not
  // apps (FR-01).
  const std::wstring web = tree.user_ + L"\\web.lnk";
  ASSERT_TRUE(makeLnk(web, L"https://example.com/", L"Website"));

  win32::StartMenuSource src({tree.user_, tree.all_});
  const std::vector<core::CatalogRecord> recs = src.scan();
  const std::string u = wideToUtf8(web);
  for (const auto& r : recs)
    EXPECT_NE(r.launch_target, u) << "URL-only shortcut must be skipped";
}

TEST(StartMenuSource, HiddenEntriesSkipped) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;
  const std::wstring notepad = notepadPath();
  ASSERT_FALSE(notepad.empty());

  // Control: a visible .lnk at the root.
  const std::wstring visible = tree.user_ + L"\\visible.lnk";
  ASSERT_TRUE(makeLnk(visible, notepad, L"Visible"));

  // Hidden subdirectory: a valid .lnk inside must not be walked.
  const std::wstring hdir = tree.user_ + L"\\hidden";
  ASSERT_TRUE(CreateDirectoryW(hdir.c_str(), nullptr));
  ASSERT_TRUE(SetFileAttributesW(hdir.c_str(), FILE_ATTRIBUTE_HIDDEN));
  const std::wstring secret = hdir + L"\\secret.lnk";
  ASSERT_TRUE(makeLnk(secret, notepad, L"Secret"));

  // Hidden file at the root: skipped even though the file is a valid
  // shortcut.
  const std::wstring ghost = tree.user_ + L"\\ghost.lnk";
  ASSERT_TRUE(makeLnk(ghost, notepad, L"Ghost"));
  ASSERT_TRUE(SetFileAttributesW(ghost.c_str(), FILE_ATTRIBUTE_HIDDEN));

  win32::StartMenuSource src({tree.user_, tree.all_});
  const std::vector<core::CatalogRecord> recs = src.scan();
  const bool hasVisible = findByLnk(recs, visible) != nullptr;
  const bool hasSecret = findByLnk(recs, secret) != nullptr;
  const bool hasGhost = findByLnk(recs, ghost) != nullptr;
  EXPECT_TRUE(hasVisible) << "control: visible .lnk must be found";
  EXPECT_FALSE(hasSecret) << "hidden subdirectory must not be walked";
  EXPECT_FALSE(hasGhost) << "hidden file must be skipped";
}
