
// RW-H-010 (H-WINDOWS): portable window registry tests — window_id
// lifecycle (UUID, never HWND), HWND recycling, event state machine,
// snapshot reconciliation, relationship graph, cross-PID correlation
// (ADR-0006: never PID alone), surface classification, modality
// inference, application-session grouping.
//
// Portable: no Win32 — native handles are opaque strings, events are
// scripted (the same seam the Win32 adapter drives in production).

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include "core/window_registry.hpp"

using namespace remotewindows;

namespace {

bool isUuidFormat(const std::string& s) {
  if (s.size() != 36) return false;
  const auto hex = [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  };
  for (int p = 0; p < 36; ++p) {
    if (p == 8 || p == 13 || p == 18 || p == 23) {
      if (s[p] != '-') return false;
    } else if (!hex(s[p])) {
      return false;
    }
  }
  return s[14] == '4' &&
         (s[19] == '8' || s[19] == '9' || s[19] == 'a' || s[19] == 'b');
}

// A full first-sight observation (what a Win32 CREATE event carries).
core::NativeWindowInfo makeInfo(const std::string& hwnd, uint32_t pid) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.pid = pid;
  i.tid = pid * 10;
  i.has_title = true;
  i.title = "T";
  i.has_class = true;
  i.window_class = "App";
  i.has_geometry = true;
  i.x = 10;
  i.y = 20;
  i.width_px = 100;
  i.height_px = 50;
  i.dpi_scale_percent = 100;
  i.has_visibility = true;
  i.visible = true;
  i.has_state = true;
  i.enabled = true;
  i.minimized = false;
  i.maximized = false;
  i.has_z_order = true;
  i.z_order_rank = 0;
  i.has_relationships = true;  // all edges nullopt unless set
  return i;
}

core::WindowEvent ev(core::WinEventKind k, core::NativeWindowInfo i) {
  return {k, std::move(i)};
}

core::NativeWindowInfo minimal(const std::string& hwnd) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  return i;
}

core::WindowEvent nameChange(const std::string& hwnd, const std::string& t) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_title = true;
  i.title = t;
  return {core::WinEventKind::NameChange, i};
}

core::WindowEvent moveResize(const std::string& hwnd, int32_t x, int32_t y,
                             uint32_t w, uint32_t h, uint32_t dpi) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_geometry = true;
  i.x = x;
  i.y = y;
  i.width_px = w;
  i.height_px = h;
  i.dpi_scale_percent = dpi;
  return {core::WinEventKind::LocationChange, i};
}

core::WindowEvent zOrder(const std::string& hwnd, int32_t rank) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_z_order = true;
  i.z_order_rank = rank;
  return {core::WinEventKind::LocationChange, i};
}

core::WindowEvent setVisible(const std::string& hwnd, bool visible) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_visibility = true;
  i.visible = visible;
  return {core::WinEventKind::Show, i};
}

core::WindowEvent setMinimized(const std::string& hwnd, bool minimized) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_state = true;
  i.enabled = true;
  i.minimized = minimized;
  i.maximized = false;
  return {minimized ? core::WinEventKind::Minimize
                    : core::WinEventKind::Restore,
          i};
}

core::WindowEvent setEnabled(const std::string& hwnd, bool enabled) {
  core::NativeWindowInfo i;
  i.hwnd = hwnd;
  i.has_state = true;
  i.enabled = enabled;
  i.minimized = false;
  i.maximized = false;
  return {core::WinEventKind::EnableChange, i};
}

core::WindowEvent focusGained(const std::string& hwnd) {
  return {core::WinEventKind::FocusGained, minimal(hwnd)};
}

// Create a window and return its remote id.
std::string create(core::WindowRegistry& reg, const std::string& hwnd,
                   uint32_t pid, const std::string& title = "T",
                   std::optional<std::string> owner_hwnd = std::nullopt,
                   std::optional<std::string> root_owner_hwnd = std::nullopt,
                   bool popup_style = false) {
  auto info = makeInfo(hwnd, pid);
  info.title = title;
  info.owner_hwnd = owner_hwnd;
  info.root_owner_hwnd = root_owner_hwnd;
  info.is_popup_style = popup_style;
  auto n = reg.applyEvent(ev(core::WinEventKind::Create, info));
  EXPECT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Announce);
  return n.empty() ? std::string() : n[0].window_id;
}

}  // namespace

// ---------------------------------------------------------------- lifecycle

TEST(WindowRegistryLifecycle, CreateAnnouncesAllFieldsOnce) {
  core::WindowRegistry reg;
  auto info = makeInfo("0x1000", 42);
  info.title = "Main";
  info.window_class = "MainWindow";
  auto notifs = reg.applyEvent(ev(core::WinEventKind::Create, info));

  ASSERT_EQ(notifs.size(), 1u);
  EXPECT_EQ(notifs[0].kind, core::RegistryNotificationKind::Announce);
  const core::WindowRecord& r = notifs[0].record;
  EXPECT_EQ(notifs[0].window_id, r.window_id);
  EXPECT_TRUE(isUuidFormat(r.window_id)) << r.window_id;
  EXPECT_NE(r.window_id, r.hwnd);  // HWND is never the durable identity
  EXPECT_EQ(r.hwnd, "0x1000");
  EXPECT_EQ(r.pid, 42u);
  EXPECT_EQ(r.tid, 420u);
  EXPECT_EQ(r.title, "Main");
  EXPECT_EQ(r.window_class, "MainWindow");
  EXPECT_EQ(r.x, 10);
  EXPECT_EQ(r.y, 20);
  EXPECT_EQ(r.width_px, 100u);
  EXPECT_EQ(r.height_px, 50u);
  EXPECT_EQ(r.dpi_scale_percent, 100u);
  EXPECT_TRUE(r.visible);
  EXPECT_TRUE(r.enabled);
  EXPECT_FALSE(r.minimized);
  EXPECT_FALSE(r.maximized);
  EXPECT_EQ(r.z_order_rank, 0);
  EXPECT_FALSE(r.is_active);
  EXPECT_EQ(r.role, core::WindowRole::Unknown);
  EXPECT_EQ(r.modality, core::WindowModality::None);
  EXPECT_EQ(r.app_session_id, "");
  EXPECT_EQ(reg.size(), 1u);
  EXPECT_EQ(reg.find(r.window_id).value().hwnd, "0x1000");
  EXPECT_EQ(reg.findByHwnd("0x1000").value().window_id, r.window_id);

  // Idempotent re-delivery of the same CREATE: no second announce.
  EXPECT_TRUE(reg.applyEvent(ev(core::WinEventKind::Create, info)).empty());
  EXPECT_EQ(reg.size(), 1u);
}

TEST(WindowRegistryLifecycle, HwndRecyclingAssignsNewWindowId) {
  core::WindowRegistry reg;
  const std::string id1 = create(reg, "0x2000", 7);
  EXPECT_TRUE(isUuidFormat(id1));

  auto rm = reg.applyEvent(
      ev(core::WinEventKind::Destroy, minimal("0x2000")));
  ASSERT_EQ(rm.size(), 1u);
  EXPECT_EQ(rm[0].kind, core::RegistryNotificationKind::Remove);
  EXPECT_EQ(rm[0].window_id, id1);
  EXPECT_EQ(rm[0].remove_reason, "closed");
  EXPECT_EQ(reg.size(), 0u);

  // Same HWND value reappears after destruction: a NEW window, fresh UUID.
  const std::string id2 = create(reg, "0x2000", 7);
  EXPECT_NE(id2, id1);
  EXPECT_TRUE(isUuidFormat(id2));
  EXPECT_EQ(reg.findByHwnd("0x2000").value().window_id, id2);
  EXPECT_EQ(reg.find(id1), std::nullopt);
  EXPECT_EQ(reg.size(), 1u);
}

TEST(WindowRegistryLifecycle, NameChangeEmitsUpdateOnlyWhenDifferent) {
  core::WindowRegistry reg;
  const std::string id = create(reg, "0x1000", 1);
  auto u = reg.applyEvent(nameChange("0x1000", "New Title"));
  ASSERT_EQ(u.size(), 1u);
  EXPECT_EQ(u[0].kind, core::RegistryNotificationKind::Update);
  EXPECT_EQ(u[0].window_id, id);
  EXPECT_EQ(u[0].record.title, "New Title");
  // Same title again: nothing.
  EXPECT_TRUE(reg.applyEvent(nameChange("0x1000", "New Title")).empty());
}

TEST(WindowRegistryLifecycle, LocationChangeEmitsUpdate) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);
  auto u = reg.applyEvent(moveResize("0x1000", 300, 200, 640, 480, 150));
  ASSERT_EQ(u.size(), 1u);
  EXPECT_EQ(u[0].record.x, 300);
  EXPECT_EQ(u[0].record.y, 200);
  EXPECT_EQ(u[0].record.width_px, 640u);
  EXPECT_EQ(u[0].record.height_px, 480u);
  EXPECT_EQ(u[0].record.dpi_scale_percent, 150u);
  EXPECT_TRUE(reg.applyEvent(moveResize("0x1000", 300, 200, 640, 480, 150))
                  .empty());
}

TEST(WindowRegistryLifecycle, ZOrderChangeEmitsUpdate) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);
  auto u = reg.applyEvent(zOrder("0x1000", 5));
  ASSERT_EQ(u.size(), 1u);
  EXPECT_EQ(u[0].record.z_order_rank, 5);
  EXPECT_TRUE(reg.applyEvent(zOrder("0x1000", 5)).empty());
}

TEST(WindowRegistryLifecycle, HideThenShowTogglesVisible) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);

  auto hide = minimal("0x1000");
  hide.has_visibility = true;
  hide.visible = false;
  auto h = reg.applyEvent(ev(core::WinEventKind::Hide, hide));
  ASSERT_EQ(h.size(), 1u);
  EXPECT_EQ(h[0].record.visible, false);

  auto show = minimal("0x1000");
  show.has_visibility = true;
  show.visible = true;
  auto s = reg.applyEvent(ev(core::WinEventKind::Show, show));
  ASSERT_EQ(s.size(), 1u);
  EXPECT_EQ(s[0].record.visible, true);
}

TEST(WindowRegistryLifecycle, MinimizeAndRestoreState) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);
  auto m = reg.applyEvent(setMinimized("0x1000", true));
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].record.minimized, true);
  auto r = reg.applyEvent(setMinimized("0x1000", false));
  ASSERT_EQ(r.size(), 1u);
  EXPECT_EQ(r[0].record.minimized, false);
  // Restore while not minimized: idempotent no-op.
  EXPECT_TRUE(reg.applyEvent(setMinimized("0x1000", false)).empty());
}

TEST(WindowRegistryLifecycle, FocusMovesActiveBetweenWindows) {
  core::WindowRegistry reg;
  const std::string ida = create(reg, "0xA", 1);
  const std::string idb = create(reg, "0xB", 2);

  auto fa = reg.applyEvent(focusGained("0xA"));
  ASSERT_EQ(fa.size(), 1u);
  EXPECT_EQ(fa[0].window_id, ida);
  EXPECT_EQ(fa[0].record.is_active, true);

  auto fb = reg.applyEvent(focusGained("0xB"));
  ASSERT_EQ(fb.size(), 2u);
  // Previous foreground cleared first, then the new active window.
  EXPECT_EQ(fb[0].window_id, ida);
  EXPECT_EQ(fb[0].record.is_active, false);
  EXPECT_EQ(fb[1].window_id, idb);
  EXPECT_EQ(fb[1].record.is_active, true);

  EXPECT_EQ(reg.find(ida).value().is_active, false);
  EXPECT_EQ(reg.find(idb).value().is_active, true);
}

TEST(WindowRegistryLifecycle, DestroyRemovesWindowParentSurvives) {
  core::WindowRegistry reg;
  const std::string ido = create(reg, "0xO", 1);
  const std::string idd =
      create(reg, "0xD", 1, "D", std::string("0xO"), std::string("0xO"));

  // The owned dialog's announce already carries the resolved owner id.
  EXPECT_EQ(reg.find(idd).value().owner_window_id, ido);

  auto rm = reg.applyEvent(ev(core::WinEventKind::Destroy, minimal("0xD")));
  ASSERT_EQ(rm.size(), 1u);
  EXPECT_EQ(rm[0].kind, core::RegistryNotificationKind::Remove);
  EXPECT_EQ(rm[0].window_id, idd);
  EXPECT_EQ(rm[0].remove_reason, "closed");

  // Parent survives, untouched (removal without parent teardown).
  auto o = reg.find(ido);
  ASSERT_TRUE(o.has_value());
  EXPECT_EQ(o->visible, true);
  EXPECT_EQ(o->modality, core::WindowModality::None);
  EXPECT_EQ(reg.size(), 1u);

  // Destroy the same handle again: idempotent no-op.
  EXPECT_TRUE(
      reg.applyEvent(ev(core::WinEventKind::Destroy, minimal("0xD"))).empty());
}

TEST(WindowRegistryLifecycle, DestroyingOwnerClearsChildResolvedOwner) {
  core::WindowRegistry reg;
  const std::string ido = create(reg, "0xO", 1);
  const std::string idd = create(reg, "0xD", 1, "D", std::string("0xO"),
                                 std::string("0xO"));
  EXPECT_EQ(reg.find(idd).value().owner_window_id, ido);

  auto rm = reg.applyEvent(ev(core::WinEventKind::Destroy, minimal("0xO")));
  // [Remove owner, Reannounce child with the resolved owner cleared].
  ASSERT_EQ(rm.size(), 2u);
  EXPECT_EQ(rm[0].kind, core::RegistryNotificationKind::Remove);
  EXPECT_EQ(rm[0].window_id, ido);
  EXPECT_EQ(rm[1].kind, core::RegistryNotificationKind::Reannounce);
  EXPECT_EQ(rm[1].window_id, idd);
  EXPECT_EQ(rm[1].record.owner_window_id, std::nullopt);
  // The native edge stays (a later handle reuse re-resolves it).
  EXPECT_EQ(reg.find(idd).value().owner_hwnd, std::string("0xO"));
}

TEST(WindowRegistryLifecycle, EventForUntrackedWindowMaterializesIt) {
  core::WindowRegistry reg;
  auto n = reg.applyEvent(nameChange("0x7777", "Late"));
  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Announce);
  EXPECT_EQ(n[0].record.title, "Late");
  EXPECT_TRUE(isUuidFormat(n[0].record.window_id));
}

// ------------------------------------------------------------ reconciliation

TEST(Reconciliation, ScanAnnouncesWindowMissedByEventStream) {
  core::WindowRegistry reg;
  // No CREATE event was ever delivered for 0x9000 — the scan recovers it.
  std::vector<core::NativeWindowInfo> scan = {makeInfo("0x9000", 5)};
  auto n = reg.reconcile(scan, reg.eventSequence());
  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Announce);
  EXPECT_EQ(reg.findByHwnd("0x9000").value().title, "T");

  // A second identical scan emits nothing (idempotent).
  EXPECT_TRUE(reg.reconcile(scan, reg.eventSequence()).empty());
}

TEST(Reconciliation, ScanRemovesWindowThatDisappeared) {
  core::WindowRegistry reg;
  const std::string id1 = create(reg, "0x1000", 1);
  // The window vanished without a DESTROY event; the scan no longer sees it.
  std::vector<core::NativeWindowInfo> scan = {makeInfo("0x2000", 2)};
  auto n = reg.reconcile(scan, reg.eventSequence());
  ASSERT_EQ(n.size(), 2u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Announce);
  EXPECT_EQ(n[1].kind, core::RegistryNotificationKind::Remove);
  EXPECT_EQ(n[1].window_id, id1);
  EXPECT_EQ(n[1].remove_reason, "unavailable");  // missed destroy, not closed
  EXPECT_EQ(reg.size(), 1u);
}

TEST(Reconciliation, ScanWinsForCurrentTruth) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);
  // No event moved the window; the scan sees its current position.
  core::NativeWindowInfo s = makeInfo("0x1000", 1);
  s.x = 999;
  s.y = 888;
  std::vector<core::NativeWindowInfo> scan = {s};
  auto n = reg.reconcile(scan, reg.eventSequence());
  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Update);
  EXPECT_EQ(n[0].record.x, 999);
  EXPECT_EQ(n[0].record.y, 888);
  EXPECT_EQ(reg.find(n[0].window_id).value().x, 999);
}

TEST(Reconciliation, EventsWinForHistoryOverStaleScan) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);

  // The scan starts...
  const core::EventSequence seq = reg.eventSequence();
  // ...and a name change lands while the (slow) enumeration is in flight.
  reg.applyEvent(nameChange("0x1000", "B"));

  // The scan snapshot still carries the OLD title "A" but a fresh
  // geometry. The stale title must NOT clobber the event; the geometry
  // (not touched by a newer event) is taken from the scan.
  core::NativeWindowInfo s = makeInfo("0x1000", 1);
  s.title = "A";
  s.x = 500;
  std::vector<core::NativeWindowInfo> scan = {s};
  auto n = reg.reconcile(scan, seq);

  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Update);
  EXPECT_EQ(n[0].record.title, "B");  // event value kept
  EXPECT_EQ(n[0].record.x, 500);      // scan value applied
  EXPECT_EQ(reg.find(n[0].window_id).value().title, "B");
}

TEST(Reconciliation, ScanIsIdempotent) {
  core::WindowRegistry reg;
  create(reg, "0x1000", 1);
  create(reg, "0x2000", 2);
  std::vector<core::NativeWindowInfo> scan = {makeInfo("0x1000", 1),
                                              makeInfo("0x2000", 2)};
  EXPECT_TRUE(reg.reconcile(scan, reg.eventSequence()).empty());
  EXPECT_TRUE(reg.reconcile(scan, reg.eventSequence()).empty());
  EXPECT_EQ(reg.size(), 2u);
}

TEST(Reconciliation, ScriptedSequenceRecoversMultipleMissedEvents) {
  // Scripted missed-event timeline:
  //   1. CREATE M        (delivered)
  //   2. M renamed       (MISSED)
  //   3. M moved         (MISSED)
  //   4. CREATE dialog D (MISSED)
  //   5. D renamed       (MISSED)
  //   6. reconcile scan  -> recovers 2..5
  //   7. D destroyed     (MISSED)
  //   8. reconcile scan  -> recovers 7
  core::WindowRegistry reg;
  const std::string idm = create(reg, "0x1", 10, "M");

  core::NativeWindowInfo m = makeInfo("0x1", 10);
  m.title = "M renamed";
  m.x = 77;
  core::NativeWindowInfo d = makeInfo("0x2", 10);
  d.title = "Open";
  d.window_class = "#32770";
  d.owner_hwnd = "0x1";
  d.root_owner_hwnd = "0x1";
  std::vector<core::NativeWindowInfo> scan1 = {m, d};
  auto n1 = reg.reconcile(scan1, reg.eventSequence());

  // Update for M (title + geometry) and an Announce for D (with the
  // resolved owner id and the file-picker role heuristic).
  ASSERT_EQ(n1.size(), 2u);
  EXPECT_EQ(n1[0].kind, core::RegistryNotificationKind::Update);
  EXPECT_EQ(n1[0].window_id, idm);
  EXPECT_EQ(n1[0].record.title, "M renamed");
  EXPECT_EQ(n1[0].record.x, 77);
  EXPECT_EQ(n1[1].kind, core::RegistryNotificationKind::Announce);
  EXPECT_EQ(n1[1].record.owner_window_id, idm);
  EXPECT_EQ(n1[1].record.role, core::WindowRole::FilePicker);
  const std::string idd = n1[1].window_id;

  // D is destroyed (missed); the next scan no longer lists it.
  std::vector<core::NativeWindowInfo> scan2 = {m};
  auto n2 = reg.reconcile(scan2, reg.eventSequence());
  ASSERT_EQ(n2.size(), 1u);
  EXPECT_EQ(n2[0].kind, core::RegistryNotificationKind::Remove);
  EXPECT_EQ(n2[0].window_id, idd);
  EXPECT_EQ(n2[0].remove_reason, "unavailable");
  EXPECT_EQ(reg.size(), 1u);
}

// ------------------------------------------------------------- relationships

TEST(Relationships, OwnerRegisteredFirstAnnouncesWithOwnerId) {
  core::WindowRegistry reg;
  const std::string ido = create(reg, "0xO", 1);
  const std::string idd = create(reg, "0xD", 1, "D", std::string("0xO"),
                                 std::string("0xO"));
  EXPECT_EQ(reg.find(idd).value().owner_window_id, ido);
  EXPECT_EQ(reg.find(idd).value().parent_window_id, std::nullopt);
}

TEST(Relationships, LateOwnerResolutionReannouncesChild) {
  core::WindowRegistry reg;
  // Dialog first, owner not yet tracked: the announce has no owner id.
  const std::string idd = create(reg, "0xD", 1, "D", std::string("0xO"),
                                 std::string("0xO"));
  EXPECT_EQ(reg.find(idd).value().owner_window_id, std::nullopt);

  // The owner appears: [Announce owner, Reannounce dialog].
  auto n = reg.applyEvent(ev(core::WinEventKind::Create, makeInfo("0xO", 1)));
  ASSERT_EQ(n.size(), 2u);
  EXPECT_EQ(n[0].kind, core::RegistryNotificationKind::Announce);
  EXPECT_EQ(n[1].kind, core::RegistryNotificationKind::Reannounce);
  EXPECT_EQ(n[1].window_id, idd);
  EXPECT_EQ(n[1].record.owner_window_id, n[0].window_id);
}

TEST(Relationships, NestedDialogRootOwnerChainResolves) {
  core::WindowRegistry reg;
  const std::string idm = create(reg, "0xM", 1);
  const std::string idd1 = create(reg, "0xD1", 1, "D1", std::string("0xM"),
                                  std::string("0xM"));
  const std::string idd2 = create(reg, "0xD2", 1, "D2", std::string("0xD1"),
                                  std::string("0xM"));
  // Direct-owner resolution at every level of the chain.
  EXPECT_EQ(reg.find(idd1).value().owner_window_id, idm);
  EXPECT_EQ(reg.find(idd2).value().owner_window_id, idd1);
  // The root owner is tracked on the record for correlation purposes.
  EXPECT_EQ(reg.find(idd2).value().root_owner_hwnd, std::string("0xM"));
}

TEST(Relationships, BrokeredDialogCrossPidCorrelatesViaOwnerChain) {
  // ADR-0006: a dialog brokered into a DIFFERENT process correlates with
  // the session via the owner chain — the PID is irrelevant here.
  core::WindowRegistry reg;
  reg.registerSession("sess", 100, {100});
  const std::string idm = create(reg, "0xM", 100, "Main");
  EXPECT_EQ(reg.find(idm).value().app_session_id, "sess");
  EXPECT_EQ(reg.find(idm).value().role, core::WindowRole::Main);

  const std::string idd =
      create(reg, "0xD", 999, "Open", std::string("0xM"), std::string("0xM"));
  // pid 999 is unknown to the session — correlation came from the owner.
  EXPECT_EQ(reg.find(idd).value().app_session_id, "sess");
  EXPECT_EQ(reg.find(idd).value().role, core::WindowRole::Dialog);
}

TEST(Relationships, PidAloneNeverCorrelates) {
  // Explicit anti-pattern (A4): the same PID can belong to multiple
  // sessions' pid sets; with no owner/root-owner/modal evidence, PID
  // membership alone must NOT associate the window with either session.
  core::WindowRegistry reg;
  reg.registerSession("sessA", 100, {100, 101});
  reg.registerSession("sessB", 101, {101, 102});
  const std::string idma = create(reg, "0xA", 100);  // anchors sessA
  const std::string idmb = create(reg, "0xB", 101);  // anchors sessB
  EXPECT_EQ(reg.find(idma).value().app_session_id, "sessA");
  EXPECT_EQ(reg.find(idmb).value().app_session_id, "sessB");

  // W's pid (101) is in BOTH sessA.known_pids and sessB's primary set.
  const std::string idw = create(reg, "0xW", 101);
  EXPECT_EQ(reg.find(idw).value().app_session_id, "");
  EXPECT_EQ(reg.find(idw).value().role, core::WindowRole::Unknown);
}

TEST(Relationships, FocusTransitionCorrelatesAfterLateAnchor) {
  // Focus transitions + app session + owner chain: W (helper pid) is
  // owned by M, but the session/anchor only exists after both windows
  // were created. When M held focus and then W gained it, W correlates.
  core::WindowRegistry reg;
  const std::string idm = create(reg, "0xM", 100);
  const std::string idw = create(reg, "0xW", 200, "W", std::string("0xM"),
                                 std::string("0xM"));
  // No session yet: W is uncorrelated (no session to correlate to).
  EXPECT_EQ(reg.find(idw).value().app_session_id, "");

  reg.registerSession("sess", 100, {100, 200});
  EXPECT_TRUE(reg.setSessionAnchor("sess", idm));
  EXPECT_EQ(reg.find(idm).value().app_session_id, "sess");

  reg.applyEvent(focusGained("0xM"));
  auto n = reg.applyEvent(focusGained("0xW"));
  // [Update M inactive, Update W active, Reannounce W (session + role)].
  ASSERT_EQ(n.size(), 3u);
  EXPECT_EQ(n[2].kind, core::RegistryNotificationKind::Reannounce);
  EXPECT_EQ(n[2].window_id, idw);
  EXPECT_EQ(reg.find(idw).value().app_session_id, "sess");
  EXPECT_EQ(reg.find(idw).value().role, core::WindowRole::Dialog);
}

// -------------------------------------------------------------------- surface

TEST(SurfaceClassification, ChildControlIsNotOwnSurface) {
  auto i = makeInfo("0xC1", 5);
  i.is_child_window = true;
  i.parent_hwnd = "0xC0";
  EXPECT_EQ(core::classifySurface(i), core::WindowSurfaceKind::ChildControl);
  core::WindowRecord r;
  r.surface_kind = core::WindowSurfaceKind::ChildControl;
  EXPECT_FALSE(core::requiresOwnCaptureSurface(r));
}

TEST(SurfaceClassification, OwnedTopLevelPopupIsOwnSurface) {
  auto i = makeInfo("0xC2", 5);
  i.is_popup_style = true;
  i.owner_hwnd = "0xC0";
  EXPECT_EQ(core::classifySurface(i),
            core::WindowSurfaceKind::IndependentTopLevel);
  core::WindowRecord r;
  r.surface_kind = core::WindowSurfaceKind::IndependentTopLevel;
  EXPECT_TRUE(core::requiresOwnCaptureSurface(r));
}

TEST(SurfaceClassification, ChildInsideTrackedParentSkipsDuplicateStream) {
  // The capture task (RW-H-011) must not allocate a stream for child
  // controls already contained in the parent frame.
  core::WindowRegistry reg;
  const std::string idp = create(reg, "0xC0", 5, "Parent");
  auto child = makeInfo("0xC1", 5);
  child.is_child_window = true;
  child.parent_hwnd = "0xC0";
  auto n = reg.applyEvent(ev(core::WinEventKind::Create, child));
  ASSERT_EQ(n.size(), 1u);
  EXPECT_EQ(n[0].record.surface_kind, core::WindowSurfaceKind::ChildControl);
  EXPECT_EQ(n[0].record.role, core::WindowRole::Unknown);

  EXPECT_TRUE(core::requiresOwnCaptureSurface(reg.find(idp).value()));
  EXPECT_FALSE(core::requiresOwnCaptureSurface(reg.find(n[0].window_id).value()));
}

// ------------------------------------------------------------------- modality

TEST(Modality, NoOwnerNeverModal) {
  EXPECT_EQ(core::inferModality(false, true, {}), core::WindowModality::None);
  EXPECT_EQ(core::inferModality(false, false, {false}),
            core::WindowModality::None);

  core::WindowRegistry reg;
  const std::string idm = create(reg, "0xM", 1);
  EXPECT_EQ(reg.find(idm).value().modality, core::WindowModality::None);
}

TEST(Modality, OwnedWithEnabledOwnerIsWindowModal) {
  // Pure function (faked inputs):
  EXPECT_EQ(core::inferModality(true, true, {false}),
            core::WindowModality::WindowModal);

  // Registry: owned dialog, owner enabled -> WindowModal at birth.
  core::WindowRegistry reg;
  create(reg, "0xO", 1);
  const std::string idd = create(reg, "0xD", 1, "D", std::string("0xO"),
                                 std::string("0xO"));
  EXPECT_EQ(reg.find(idd).value().modality, core::WindowModality::WindowModal);
}

TEST(Modality, OwnerDisabledAllSiblingsDisabledIsAppModal) {
  // Pure function (faked inputs):
  EXPECT_EQ(core::inferModality(true, false, {false, false}),
            core::WindowModality::AppModal);
  EXPECT_EQ(core::inferModality(true, false, {}),
            core::WindowModality::AppModal);  // no siblings observed

  // Registry (faked enable events): O, sibling S, dialog D (owner O),
  // same pid. Disable S, then O -> D becomes AppModal; re-enable O ->
  // back to WindowModal.
  core::WindowRegistry reg;
  create(reg, "0xO", 55);
  create(reg, "0xS", 55);
  const std::string idd = create(reg, "0xD", 55, "D", std::string("0xO"),
                                 std::string("0xO"));
  EXPECT_EQ(reg.find(idd).value().modality, core::WindowModality::WindowModal);

  auto ns = reg.applyEvent(setEnabled("0xS", false));
  EXPECT_TRUE(ns.empty());  // S has no owner: no modality effect, no wire
  auto no = reg.applyEvent(setEnabled("0xO", false));
  ASSERT_EQ(no.size(), 1u);
  EXPECT_EQ(no[0].window_id, idd);
  EXPECT_EQ(no[0].record.modality, core::WindowModality::AppModal);

  auto nr = reg.applyEvent(setEnabled("0xO", true));
  ASSERT_EQ(nr.size(), 1u);
  EXPECT_EQ(nr[0].window_id, idd);
  EXPECT_EQ(nr[0].record.modality, core::WindowModality::WindowModal);
}

TEST(Modality, OwnerDisabledSomeSiblingsEnabledIsWindowModal) {
  // Pure function (faked inputs): one enabled sibling keeps it
  // window-modal.
  EXPECT_EQ(core::inferModality(true, false, {false, true, false}),
            core::WindowModality::WindowModal);

  // Registry: O, sibling S (enabled), dialog D (owner O). Disabling O
  // alone does NOT escalate to AppModal while S is enabled.
  core::WindowRegistry reg;
  create(reg, "0xO", 55);
  create(reg, "0xS", 55);
  const std::string idd = create(reg, "0xD", 55, "D", std::string("0xO"),
                                 std::string("0xO"));
  auto n = reg.applyEvent(setEnabled("0xO", false));
  EXPECT_TRUE(n.empty());  // no transition: still WindowModal
  EXPECT_EQ(reg.find(idd).value().modality, core::WindowModality::WindowModal);
}

// -------------------------------------------------------------------- sessions

TEST(AppSession, FirstPrimaryPidTopLevelBecomesAnchor) {
  core::WindowRegistry reg;
  reg.registerSession("sess", 100);
  const std::string idm = create(reg, "0xM", 100);
  EXPECT_EQ(reg.find(idm).value().app_session_id, "sess");
  EXPECT_EQ(reg.find(idm).value().role, core::WindowRole::Main);

  // A second top-level of the primary pid without owner/modal evidence
  // is NOT auto-associated (PID alone never correlates an auxiliary).
  const std::string idn = create(reg, "0xN", 100);
  EXPECT_EQ(reg.find(idn).value().app_session_id, "");
}

TEST(AppSession, ZeroGeometryHelperDoesNotStealAnchor) {
  // Regression (runner probe 2026-11-27, interactive session): the OS
  // creates transient invisible helper windows in the app's own
  // process (zero client area, owned by the not-yet-complete main
  // window) whose CREATE events can arrive BEFORE the main window's.
  // Such a helper must not steal the session anchor; the real main
  // (non-zero client area) gets it.
  core::WindowRegistry reg;
  reg.registerSession("sess", 100);

  // Helper first: same pid, NO geometry, invisible, owned by the
  // main window (not yet observed). It may correlate via the WEAK
  // evidence path (pid in the session's pid set + owner edge set —
  // ADR-0006 rule 3), but it must NOT become the anchor.
  auto helper = makeInfo("0xH", 100);
  helper.has_geometry = false;
  helper.width_px = 0;
  helper.height_px = 0;
  helper.visible = false;
  helper.owner_hwnd = "0xM";
  reg.applyEvent(ev(core::WinEventKind::Create, helper));
  const auto h_before = reg.findByHwnd("0xH").value();
  EXPECT_EQ(h_before.app_session_id, "sess");  // weak evidence
  EXPECT_NE(h_before.role, core::WindowRole::Main);  // NOT the anchor

  // The real main window, with a real client area: it anchors. (Its
  // CREATE also re-resolves the helper's owner edge -> a second
  // notification, hence no create() helper here.)
  reg.applyEvent(ev(core::WinEventKind::Create, makeInfo("0xM", 100)));
  const auto main = reg.findByHwnd("0xM").value();
  EXPECT_EQ(main.app_session_id, "sess");
  EXPECT_EQ(main.role, core::WindowRole::Main);

  // The helper remains a non-anchor member with a stable identity.
  const auto h_after = reg.findByHwnd("0xH").value();
  EXPECT_EQ(h_after.window_id, h_before.window_id);
  EXPECT_NE(h_after.role, core::WindowRole::Main);
}

TEST(AppSession, AssociateWindowExplicit) {
  core::WindowRegistry reg;
  reg.registerSession("sess", 100);
  const std::string idm = create(reg, "0xM", 100);
  const std::string idx = create(reg, "0xX", 300);
  EXPECT_EQ(reg.find(idx).value().app_session_id, "");

  reg.associateWindow(idx, "sess");
  // The association is observable through the record (a Reannounce was
  // emitted internally at call time; its shape is covered by
  // LateOwnerResolutionReannouncesChild).
  EXPECT_EQ(reg.find(idx).value().app_session_id, "sess");
  EXPECT_EQ(reg.find(idx).value().role, core::WindowRole::Auxiliary);
  EXPECT_EQ(reg.find(idm).value().app_session_id, "sess");
}
