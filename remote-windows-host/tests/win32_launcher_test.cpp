
// RW-H-009: Win32 allow-listed launcher test (FR-02, plan rule 9).
// Compiled and run ONLY on the Windows runner (MSVC + Windows SDK); on
// macOS this test is NOT RUN. Includes one REAL launch: a temp .lnk under
// a temp allowed root pointing at %SystemRoot%\System32\cmd.exe (a plain
// Win32 image — notepad is a UWP app alias and cannot be launched from
// the headless session-0 runner context; see cmdPath) — deliberate
// side effect on the dev runner: cmd opens briefly and the test kills
// it BY PID (never taskkill /IM). The temp tree is cleaned up after
// each test.

#if !defined(_WIN32)
#error "win32_launcher_test.cpp is a Windows-only test (ShellExecuteExW)"
#endif

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "app/Win32Env.hpp"

#include <objbase.h>  // CoInitializeEx / CoCreateInstance
#include <rpc.h>      // RPC_E_CHANGED_MODE (WIN32_LEAN_AND_MEAN skips it)
#include <shobjidl.h>  // IShellLinkW, CLSID_ShellLink

#include "core/catalog.hpp"
#include "core/catalog_service.hpp"
#include "win32/catalog/Win32Launcher.hpp"
#include "win32/catalog/Win32ProcessProbe.hpp"

using namespace remotewindows;
using core::makeAppId;

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
      deleteTree(full);
    else
      DeleteFileW(full.c_str());
  } while (FindNextFileW(h, &fd) != 0);
  FindClose(h);
  RemoveDirectoryW(dir.c_str());
}

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
        ADD_FAILURE() << "CreateDirectoryW: " << GetLastError();
    }
  }
  ~TempTree() { deleteTree(base_); }

  std::wstring base_, user_, all_, outside_;
};

// A REAL launch target for the kill test: %SystemRoot%\System32\cmd.exe.
// Target recorded per plan rule 2 (real behavior observed on the runner,
// 2026-11-25): on Windows 11 24H2, System32\notepad.exe is an App
// Execution Alias stub for the PACKAGED Notepad. ShellExecuteExW from
// the headless session-0 (sshd) context hands the activation to the app
// model, which never completes for a non-interactive session — the launch
// blocked for hours with no process ever appearing and the whole runner
// hung. cmd.exe is a plain Win32 image: it starts, gets a real PID +
// NOCLOSEPROCESS handle, idles on stdin, and is killable — exactly the
// lifecycle the probe/kill assertions exercise.
std::wstring cmdPath() {
  wchar_t sr[MAX_PATH] = {};
  if (GetEnvironmentVariableW(L"SystemRoot", sr, MAX_PATH) == 0)
    return {};
  const std::wstring p = std::wstring(sr) + L"\\System32\\cmd.exe";
  return GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES ? std::wstring{}
                                                                  : p;
}

core::LaunchRequest request(const std::string& launch_target) {
  // The launcher validates launch_target only; app_id is catalog-shaped
  // for realism (it is never used by the launcher).
  return {makeAppId("launcher-test", launch_target), launch_target,
          protocol::AppType::WIN32_SHORTCUT};
}

}  // namespace

TEST(Win32Launcher, OutsideAllowedRootIsUnauthorized) {
  // A .lnk OUTSIDE the allowed roots is rejected. Crucially, no process
  // was started: the validation order (Win32Launcher::launch) returns
  // before ShellExecuteExW is touched on any failure path, and no
  // assertion beyond the return value can prove non-execution — that is
  // the contract (allow-list gate, plan rule 9).
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;
  const std::wstring cmd = cmdPath();
  ASSERT_FALSE(cmd.empty()) << "cmd.exe missing on this runner";

  const std::wstring lnk = tree.outside_ + L"\\app.lnk";
  ASSERT_TRUE(makeLnk(lnk, cmd, L"Outside"));

  win32::Win32Launcher launcher({tree.user_});  // allow-list: temp user root only
  const core::LaunchOutcome out = launcher.launch(request(wideToUtf8(lnk)));
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(out.error_code, "unauthorized");
  EXPECT_EQ(out.safe_message, "Launch target is not an allow-listed shortcut");
  EXPECT_EQ(out.pid, 0u);
}

TEST(Win32Launcher, NonLnkTargetIsUnauthorized) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;

  // A non-.lnk launch target (even under an allowed root) is rejected
  // by the first check — the allow-list is shortcuts, not paths.
  win32::Win32Launcher launcher({tree.user_, tree.all_, tree.outside_});
  const core::LaunchOutcome out =
      launcher.launch(request(wideToUtf8(tree.outside_)));
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(out.error_code, "unauthorized");
  EXPECT_EQ(out.safe_message, "Launch target is not an allow-listed shortcut");
}

TEST(Win32Launcher, DeletedTargetIsLaunchFailed) {
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;

  // Allow-listed .lnk whose target file was deleted: launch_failed
  // (distinct from unauthorized — the shortcut itself is still valid).
  const std::wstring target = tree.user_ + L"\\target.exe";
  const HANDLE hf =
      CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                  0, nullptr);
  ASSERT_NE(hf, INVALID_HANDLE_VALUE);
  CloseHandle(hf);
  const std::wstring lnk = tree.user_ + L"\\gone.lnk";
  ASSERT_TRUE(makeLnk(lnk, target, L"Gone"));
  ASSERT_TRUE(DeleteFileW(target.c_str()));

  win32::Win32Launcher launcher({tree.user_});
  const core::LaunchOutcome out = launcher.launch(request(wideToUtf8(lnk)));
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(out.error_code, "launch_failed");
  EXPECT_EQ(out.safe_message, "The launch target no longer exists");
  EXPECT_EQ(out.pid, 0u);
}

TEST(Win32Launcher, RealProcessLaunchProbeAndKill) {
  // REAL launch on the dev runner: temp .lnk (under a temp allowed root)
  // -> %SystemRoot%\System32\cmd.exe (see cmdPath() for why not notepad
  // — UWP app-model activation from session 0 never completes).
  // Deliberate side effect: cmd opens briefly (idle, reading stdin) and
  // the test kills it by PID.
  ComInit com;
  ASSERT_TRUE(com.ok());
  TempTree tree;

  // Existence check BEFORE launching: if cmd.exe is missing on the
  // runner, FAIL LOUDLY (NOT RUN reporting is for orchestrators, not
  // tests).
  const std::wstring cmd = cmdPath();
  ASSERT_FALSE(cmd.empty()) << "cmd.exe missing on this runner";

  const std::wstring lnk = tree.user_ + L"\\cmd.lnk";
  ASSERT_TRUE(makeLnk(lnk, cmd, L"Command Prompt"));

  win32::Win32Launcher launcher({tree.user_});
  win32::Win32ProcessProbe probe;
  const core::LaunchOutcome out = launcher.launch(request(wideToUtf8(lnk)));
  ASSERT_TRUE(out.ok) << out.error_code << ": " << out.safe_message;
  ASSERT_GT(out.pid, 0u);
  EXPECT_TRUE(out.error_code.empty());

  // Liveness: alive now; dead after TerminateProcess + WaitForSingleObject.
  // The authoritative check is the launcher-RETAINED handle
  // (out.process_handle): LaunchService's sweep uses isHandleAlive on the
  // session's retained handle, and a held HANDLE keeps the process object
  // alive after exit, so the exit code is always readable — by pid alone a
  // fully-exited process can be unopenable and the probe stays conservative
  // (fail -> alive). The kill handle also stays open across the pid check.
  EXPECT_TRUE(probe.isAlive(out.pid));
  ASSERT_NE(out.process_handle, nullptr);  // NOCLOSEPROCESS handle retained
  EXPECT_TRUE(probe.isHandleAlive(out.process_handle));
  const HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, out.pid);
  ASSERT_NE(h, nullptr);
  ASSERT_TRUE(TerminateProcess(h, 1));
  EXPECT_EQ(WAIT_OBJECT_0, WaitForSingleObject(h, 10000));
  EXPECT_FALSE(probe.isHandleAlive(out.process_handle));  // exit 1 != STILL_ACTIVE
  EXPECT_FALSE(probe.isAlive(out.pid));  // kill handle open: readable too
  probe.closeProcessHandle(out.process_handle);  // the session's release path
  CloseHandle(h);
}
