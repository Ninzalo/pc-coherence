
// RW-H-008: portable CatalogService + LaunchService (FR-01/FR-02) over
// fakes — no platform headers, fully exercised on any host.

#include <gtest/gtest.h>

#include <cctype>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/catalog_service.hpp"

using namespace remotewindows;
namespace proto = remotewindows::protocol;
using core::makeAppId;
using core::makeUuid;

namespace {

core::CatalogRecord makeRecord(const std::string& display_name,
                               const std::string& identity_key,
                               const std::string& launch_target,
                               const std::string& source, uint32_t priority,
                               bool available = true) {
  core::CatalogRecord r;
  r.display_name = display_name;
  r.identity_key = identity_key;
  r.launch_target = launch_target;
  r.source = source;
  r.priority = priority;
  r.available = available;
  r.app_id = makeAppId(identity_key, launch_target);
  return r;
}

// In-memory fake source; counts scan() calls.
class FakeSource : public core::CatalogSource {
 public:
  std::vector<core::CatalogRecord> records;
  int scan_calls = 0;
  std::vector<core::CatalogRecord> scan() override {
    ++scan_calls;
    return records;
  }
};

// Records the requests it receives; returns a configurable outcome.
class FakeLauncher : public core::AppLauncher {
 public:
  core::LaunchOutcome next{true, "", "", 4242};
  int call_count = 0;
  core::LaunchRequest last;
  core::LaunchOutcome launch(const core::LaunchRequest& req) override {
    ++call_count;
    last = req;
    return next;
  }
};

// Per-pid liveness table; unknown pids report alive. handle_alive is
// consulted by isHandleAlive for non-null handles (null -> conservative
// alive). closeProcessHandle records what the platform released.
class FakeProbe : public core::ProcessProbe {
 public:
  std::map<uint32_t, bool> alive;
  bool handle_alive = false;
  std::vector<void*> closed_handles;
  bool isAlive(uint32_t pid) override {
    auto it = alive.find(pid);
    return it == alive.end() || it->second;
  }
  bool isHandleAlive(void* h) override { return h ? handle_alive : true; }
  void closeProcessHandle(void* h) override { closed_handles.push_back(h); }
};

void expectUuidFormat(const std::string& u) {
  EXPECT_EQ(u.size(), 36u) << u;
  if (u.size() != 36u) return;
  for (int i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      EXPECT_EQ(u[i], '-') << u << " at " << i;
      continue;
    }
    EXPECT_TRUE(std::isxdigit((unsigned char)u[i])) << u << " at " << i;
    if (std::isalpha((unsigned char)u[i]))
      EXPECT_NE(u[i], std::toupper((unsigned char)u[i])) << u << " at " << i;
  }
  EXPECT_EQ(u[14], '4') << u;          // version nibble
  EXPECT_TRUE(u[19] == '8' || u[19] == '9' || u[19] == 'a' || u[19] == 'b')
      << u;  // variant nibble
}

}  // namespace

// ---------------------------------------------------------------- CatalogService

TEST(CatalogService, FirstSnapshotDedupesAndIsProtocolReady) {
  FakeSource src;
  const std::string t1 = "C:\\start\\explorer.lnk";
  const std::string t2 = "ms-app://explorer";
  src.records = {
      makeRecord("File Explorer", "file explorer", t1, "start_menu", 10),
      makeRecord("File Explorer", "file explorer", t2, "packaged", 20),
      makeRecord("Notepad", "notepad", "C:\\start\\notepad.lnk",
                 "start_menu", 10),
  };
  core::CatalogService svc(&src);
  auto snap = svc.snapshot();
  EXPECT_EQ(src.scan_calls, 1);
  ASSERT_EQ(snap.entries.size(), 2u);  // same identity_key -> one entry
  EXPECT_GE(snap.catalog_revision, 1u);
  const proto::AppEntry* ex = nullptr;
  const proto::AppEntry* np = nullptr;
  for (const auto& e : snap.entries) {
    if (e.app_id == makeAppId("file explorer", t1)) ex = &e;
    if (e.app_id == makeAppId("notepad", "C:\\start\\notepad.lnk")) np = &e;
  }
  ASSERT_NE(ex, nullptr);
  ASSERT_NE(np, nullptr);
  EXPECT_EQ(ex->display_name, "File Explorer");
  EXPECT_EQ(ex->type, proto::AppType::PACKAGED);  // higher priority (20) wins
  EXPECT_TRUE(ex->available);
  EXPECT_EQ(np->type, proto::AppType::WIN32_SHORTCUT);
  // user_added mapping (separate service: no ASSERT in a valued lambda).
  auto user_type = [&] {
    FakeSource s2;
    s2.records = {makeRecord("My App", "my app", "app.lnk", "user_added", 30)};
    core::CatalogService svc2(&s2);
    auto c = svc2.snapshot();
    if (c.entries.size() != 1u) return proto::AppType::UNSPECIFIED;
    return c.entries[0].type;
  };
  EXPECT_EQ(user_type(), proto::AppType::USER_ADDED);
}

TEST(CatalogService, SecondSnapshotDoesNotRescan) {
  FakeSource src;
  src.records = {makeRecord("Notepad", "notepad", "t.lnk", "start_menu", 10)};
  core::CatalogService svc(&src);
  auto s1 = svc.snapshot();
  auto s2 = svc.snapshot();
  EXPECT_EQ(src.scan_calls, 1);
  EXPECT_EQ(s1.catalog_revision, s2.catalog_revision);
  EXPECT_EQ(s1.entries.size(), s2.entries.size());
  for (size_t i = 0; i < s1.entries.size(); ++i)
    EXPECT_EQ(s1.entries[i].app_id, s2.entries[i].app_id);
}

TEST(CatalogService, ForcedRescanBumpsRevisionAndBroadcastMarks) {
  FakeSource src;
  src.records = {makeRecord("Notepad", "notepad", "t.lnk", "start_menu", 10)};
  core::CatalogService svc(&src);
  const uint32_t rev1 = svc.snapshot().catalog_revision;
  EXPECT_GE(rev1, 1u);
  EXPECT_EQ(svc.lastBroadcastRevision(), 0u);  // never broadcast yet

  svc.markBroadcast(rev1);
  EXPECT_EQ(svc.lastBroadcastRevision(), rev1);

  // Source changes: a new app appears.
  src.records.push_back(makeRecord("Paint", "paint", "p.lnk",
                                   "start_menu", 10));
  auto s2 = svc.snapshot(/*force_rescan=*/true);
  EXPECT_EQ(src.scan_calls, 2);
  EXPECT_GT(s2.catalog_revision, rev1);
  EXPECT_EQ(s2.entries.size(), 2u);
  // Until the heartbeat piggyback marks it, the broadcast lags the store.
  EXPECT_EQ(svc.lastBroadcastRevision(), rev1);
  svc.markBroadcast(s2.catalog_revision);
  EXPECT_EQ(svc.lastBroadcastRevision(), s2.catalog_revision);
}

TEST(CatalogService, FindHitAndMiss) {
  FakeSource src;
  const auto np = makeRecord("Notepad", "notepad", "t.lnk", "start_menu", 10);
  src.records = {np};
  core::CatalogService svc(&src);
  svc.snapshot();
  const auto hit = svc.find(np.app_id);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->display_name, "Notepad");
  EXPECT_EQ(hit->launch_target, "t.lnk");
  EXPECT_FALSE(svc.find("app-0000000000000000").has_value());
  EXPECT_FALSE(svc.find("").has_value());
}

TEST(CatalogService, UnavailableSourceUnionAndTypeMapping) {
  FakeSource src;
  const std::string t = "C:\\start\\x.lnk";
  src.records = {
      makeRecord("X", "x", t, "start_menu", 10, /*available=*/false),
      makeRecord("X", "x", t, "packaged", 20, /*available=*/true),
  };
  core::CatalogService svc(&src);
  auto snap = svc.snapshot();
  ASSERT_EQ(snap.entries.size(), 1u);
  EXPECT_TRUE(snap.entries[0].available);  // union: any source available
}

TEST(CatalogService, EmptySourceStillProtocolReady) {
  FakeSource src;  // no records
  core::CatalogService svc(&src);
  auto snap = svc.snapshot();
  EXPECT_TRUE(snap.entries.empty());
  EXPECT_GE(snap.catalog_revision, 1u);  // 0 is not wire-valid (PROTOCOL §5)
}

// ---------------------------------------------------------------- LaunchService

namespace {
struct LaunchFixture {
  core::CatalogRecord app;
  FakeSource src;
  core::CatalogService catalog;
  FakeLauncher launcher;
  FakeProbe probe;
  core::LaunchService launch;
  uint64_t now = 1000;

  LaunchFixture(bool available = true)
      : app(makeRecord("Notepad", "notepad", "C:\\start\\notepad.lnk",
                       "start_menu", 10, available)),
        catalog(&src),
        launch(catalog, &launcher, &probe, [this] { return now; }) {
    src.records = {app};
  }
};
}  // namespace

TEST(LaunchService, SuccessStartsSessionWithoutStatusEvent) {
  LaunchFixture fx;
  const auto res = fx.launch.handleLaunch(fx.app.app_id);
  EXPECT_EQ(res.state, proto::LaunchState::STARTED);
  EXPECT_TRUE(res.error_code.empty());
  EXPECT_EQ(res.app_id, fx.app.app_id);
  ASSERT_TRUE(res.application_session_id.has_value());
  expectUuidFormat(*res.application_session_id);

  // Session stored with the outcome pid; launcher got catalog-resolved data.
  const auto& sessions = fx.launch.sessions();
  ASSERT_EQ(sessions.size(), 1u);
  EXPECT_EQ(sessions.at(fx.app.app_id).application_session_id,
            *res.application_session_id);
  EXPECT_EQ(sessions.at(fx.app.app_id).pid, 4242u);
  EXPECT_EQ(fx.launcher.last.app_id, fx.app.app_id);
  EXPECT_EQ(fx.launcher.last.launch_target, fx.app.launch_target);
  EXPECT_EQ(fx.launcher.last.type, proto::AppType::WIN32_SHORTCUT);

  // Probe reports alive: no AppStatus yet (start and close are separate
  // events; closing is reported by the sweep, not at launch time).
  EXPECT_TRUE(fx.launch.sweep().empty());
}

TEST(LaunchService, UnknownAppIdNeverReachesLauncher) {
  LaunchFixture fx;
  const auto res = fx.launch.handleLaunch("app-0000000000000000");
  EXPECT_EQ(res.state, proto::LaunchState::FAILED);
  EXPECT_EQ(res.error_code, "unknown_app_id");
  EXPECT_FALSE(res.safe_message.empty());
  EXPECT_FALSE(res.application_session_id.has_value());
  EXPECT_EQ(fx.launcher.call_count, 0);  // allow-list proven
  EXPECT_TRUE(fx.launch.sessions().empty());
}

TEST(LaunchService, UnavailableEntryFailsWithoutLauncher) {
  LaunchFixture fx(/*available=*/false);
  const auto res = fx.launch.handleLaunch(fx.app.app_id);
  EXPECT_EQ(res.state, proto::LaunchState::FAILED);
  EXPECT_EQ(res.error_code, "launch_failed");
  EXPECT_FALSE(res.application_session_id.has_value());
  EXPECT_EQ(fx.launcher.call_count, 0);
  EXPECT_TRUE(fx.launch.sessions().empty());
}

TEST(LaunchService, LauncherFailurePropagatesTypedOutcome) {
  LaunchFixture fx;
  fx.launcher.next = {false, "launch_failed", "Application could not start",
                      0};
  const auto res = fx.launch.handleLaunch(fx.app.app_id);
  EXPECT_EQ(fx.launcher.call_count, 1);
  EXPECT_EQ(res.state, proto::LaunchState::FAILED);
  EXPECT_EQ(res.error_code, "launch_failed");
  EXPECT_EQ(res.safe_message, "Application could not start");
  EXPECT_FALSE(res.application_session_id.has_value());
  EXPECT_TRUE(fx.launch.sessions().empty());

  // Empty launcher error_code falls back to the documented core code.
  fx.launcher.next = {false, "", "", 0};
  const auto res2 = fx.launch.handleLaunch(fx.app.app_id);
  EXPECT_EQ(res2.error_code, "launch_failed");
  EXPECT_FALSE(res2.safe_message.empty());
}

TEST(LaunchService, SweepReportsDeadProcessOnce) {
  LaunchFixture fx;
  const auto res = fx.launch.handleLaunch(fx.app.app_id);
  ASSERT_TRUE(res.application_session_id.has_value());

  EXPECT_TRUE(fx.launch.sweep().empty());  // alive: no event
  fx.probe.alive[4242] = false;
  auto events = fx.launch.sweep();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].app_id, fx.app.app_id);
  EXPECT_EQ(events[0].state, proto::AppRunState::CLOSED);
  EXPECT_EQ(events[0].application_session_id, *res.application_session_id);
  EXPECT_TRUE(fx.launch.sessions().at(fx.app.app_id).closed);

  // Same session is reported at most once.
  EXPECT_TRUE(fx.launch.sweep().empty());
}

TEST(LaunchService, SweepUsesRetainedHandleWhenPidIsBlind) {
  // The pid probe cannot see this process (reports alive — on Windows
  // that is what a reaped process actually looks like), but the retained
  // handle can see it exited. The handle path must be authoritative:
  // CLOSED is emitted even though isAlive(pid) says alive, and the
  // session's handle is released once.
  LaunchFixture fx;
  char fake_handle = 'h';  // stands in for a platform process handle
  fx.launcher.next = core::LaunchOutcome{true, "", "", 4242, &fake_handle};
  const auto res = fx.launch.handleLaunch(fx.app.app_id);
  ASSERT_TRUE(res.application_session_id.has_value());
  EXPECT_EQ(fx.launch.sessions().at(fx.app.app_id).process_handle,
            static_cast<void*>(&fake_handle));

  fx.probe.alive[4242] = true;    // pid path: alive (blind)
  fx.probe.handle_alive = true;   // handle path: alive
  EXPECT_TRUE(fx.launch.sweep().empty());

  fx.probe.handle_alive = false;  // process exited; pid still blind
  auto events = fx.launch.sweep();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].app_id, fx.app.app_id);
  EXPECT_EQ(events[0].state, proto::AppRunState::CLOSED);
  EXPECT_EQ(events[0].application_session_id, *res.application_session_id);
  // The released handle is recorded exactly once; the session retains
  // nothing afterwards (no double release on a re-sweep).
  EXPECT_EQ(fx.probe.closed_handles, (std::vector<void*>{&fake_handle}));
  EXPECT_EQ(fx.launch.sessions().at(fx.app.app_id).process_handle,
            static_cast<void*>(nullptr));
  EXPECT_TRUE(fx.launch.sweep().empty());
}

TEST(LaunchService, RelaunchReleasesSupersededHandle) {
  LaunchFixture fx;
  char h1 = '1', h2 = '2';  // stand in for platform process handles
  fx.launcher.next = core::LaunchOutcome{true, "", "", 4242, &h1};
  const auto r1 = fx.launch.handleLaunch(fx.app.app_id);
  ASSERT_TRUE(r1.application_session_id.has_value());

  fx.launcher.next = core::LaunchOutcome{true, "", "", 4242, &h2};
  const auto r2 = fx.launch.handleLaunch(fx.app.app_id);
  ASSERT_TRUE(r2.application_session_id.has_value());
  EXPECT_NE(*r1.application_session_id, *r2.application_session_id);

  // The re-launch releases the superseded session's handle (the old
  // process may still be running) and retains only the new one.
  const auto s = fx.launch.sessions().at(fx.app.app_id);
  EXPECT_EQ(s.process_handle, static_cast<void*>(&h2));
  EXPECT_EQ(fx.probe.closed_handles, (std::vector<void*>{&h1}));
}

TEST(LaunchService, RelaunchReplacesSession) {
  LaunchFixture fx;
  const auto r1 = fx.launch.handleLaunch(fx.app.app_id);
  ASSERT_TRUE(r1.application_session_id.has_value());

  fx.launcher.next = {true, "", "", 4243};
  fx.now += 500;
  const auto r2 = fx.launch.handleLaunch(fx.app.app_id);
  ASSERT_TRUE(r2.application_session_id.has_value());
  EXPECT_NE(*r1.application_session_id, *r2.application_session_id);
  expectUuidFormat(*r2.application_session_id);

  const auto s = fx.launch.sessions().at(fx.app.app_id);
  EXPECT_EQ(s.application_session_id, *r2.application_session_id);
  EXPECT_EQ(s.pid, 4243u);
  EXPECT_EQ(s.started_ms, 1500u);
}

// -------------------------------------------------------------------- makeUuid

TEST(Uuid, FormatVersionAndVariantNibbles) {
  for (int i = 0; i < 64; ++i) expectUuidFormat(makeUuid());
}

TEST(Uuid, UniqueAcross1000Samples) {
  std::unordered_set<std::string> seen;
  for (int i = 0; i < 1000; ++i) seen.insert(makeUuid());
  EXPECT_EQ(seen.size(), 1000u);
}
