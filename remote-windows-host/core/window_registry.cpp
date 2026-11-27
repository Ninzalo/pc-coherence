
#include "core/window_registry.hpp"

#include <algorithm>
#include <set>
#include <utility>

#include "core/catalog_service.hpp"  // makeUuid

namespace remotewindows::core {

namespace {

// ADR-0006 role heuristic: standard Windows common dialog class.
constexpr const char* kCommonDialogClass = "#32770";

std::string asciiLower(const std::string& s) {
  std::string out(s);
  for (auto& c : out)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  return out;
}

// ADR-0006: title phrases that mark a file/folder picker (Open/Save/
// Save As/Select Folder/Print families).
bool titleLooksLikeFilePicker(const std::string& title) {
  const std::string t = asciiLower(title);
  static const char* kPhrases[] = {"open", "save", "save as", "select folder",
                                   "choose folder", "select file", "print"};
  for (const char* p : kPhrases)
    if (t.find(p) != std::string::npos) return true;
  return false;
}

}  // namespace

// ------------------------------------------------------------------ sessions

void WindowRegistry::registerSession(const std::string& session_id,
                                     uint32_t primary_pid,
                                     const std::vector<uint32_t>& known_pids) {
  std::lock_guard<std::mutex> lk(mu_);
  Session s;
  s.primary_pid = primary_pid;
  s.known_pids = known_pids;
  sessions_[session_id] = std::move(s);
}

bool WindowRegistry::hasSession(const std::string& session_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  return sessions_.count(session_id) != 0;
}

bool WindowRegistry::pidInSession(const Session& s, uint32_t pid) const {
  if (pid == s.primary_pid) return true;
  return std::find(s.known_pids.begin(), s.known_pids.end(), pid) !=
         s.known_pids.end();
}

SharedSession WindowRegistry::toSharedSession(const std::string& session_id,
                                              const Session& s) const {
  SharedSession ss;
  ss.session_id = session_id;
  ss.primary_pid = s.primary_pid;
  ss.member_hwnds = s.member_hwnds;
  // known_pids + the primary pid: decideAssociation's weak rule must cover
  // the launched process itself as well as its helpers.
  ss.known_pids = s.known_pids;
  ss.known_pids.push_back(s.primary_pid);
  return ss;
}

bool WindowRegistry::setSessionAnchor(const std::string& session_id,
                                      const std::string& window_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto sit = sessions_.find(session_id);
  if (sit == sessions_.end()) return false;
  auto hit = id_to_hwnd_.find(window_id);
  if (hit == id_to_hwnd_.end()) return false;
  WindowRecord& rec = by_hwnd_.at(hit->second);

  bool changed = false;
  if (sit->second.anchor_window_id != window_id)
    sit->second.anchor_window_id = window_id;
  if (rec.app_session_id != session_id) {
    rec.app_session_id = session_id;
    changed = true;
  }
  if (std::find(sit->second.member_hwnds.begin(), sit->second.member_hwnds.end(),
                rec.hwnd) == sit->second.member_hwnds.end())
    sit->second.member_hwnds.push_back(rec.hwnd);
  if (rec.role != WindowRole::Main) {
    rec.role = WindowRole::Main;
    changed = true;
  }
  std::vector<RegistryNotification> out;
  if (changed) {
    refreshModality(rec.hwnd, out);
    emitReannounce(rec, out);
  }
  return true;
}

bool WindowRegistry::associateWindow(const std::string& window_id,
                                     const std::string& session_id) {
  std::lock_guard<std::mutex> lk(mu_);
  auto sit = sessions_.find(session_id);
  if (sit == sessions_.end()) return false;
  auto hit = id_to_hwnd_.find(window_id);
  if (hit == id_to_hwnd_.end()) return false;
  WindowRecord& rec = by_hwnd_.at(hit->second);

  bool changed = false;
  if (rec.app_session_id != session_id) {
    rec.app_session_id = session_id;
    changed = true;
  }
  if (std::find(sit->second.member_hwnds.begin(), sit->second.member_hwnds.end(),
                rec.hwnd) == sit->second.member_hwnds.end())
    sit->second.member_hwnds.push_back(rec.hwnd);
  if (changed) {
    finalizeRole(rec);
    std::vector<RegistryNotification> out;
    refreshModality(rec.hwnd, out);
    emitReannounce(rec, out);
  }
  return true;
}

// -------------------------------------------------------------------- events

std::vector<RegistryNotification> WindowRegistry::applyEvent(
    const WindowEvent& ev) {
  std::vector<RegistryNotification> out;
  std::lock_guard<std::mutex> lk(mu_);
  if (ev.info.hwnd.empty()) return out;

  switch (ev.kind) {
    case WinEventKind::Create: {
      if (by_hwnd_.count(ev.info.hwnd)) {
        // Duplicate CREATE for a LIVE handle (idempotent re-delivery):
        // merge the carried groups, keep the existing window_id (same
        // lifetime), no duplicate announce.
        mergeInto(ev.info.hwnd, ev.info, ev.kind, out);
      } else {
        insertNew(ev.info, out);
      }
      break;
    }
    case WinEventKind::Destroy: {
      auto it = by_hwnd_.find(ev.info.hwnd);
      if (it != by_hwnd_.end()) removeRecord(ev.info.hwnd, "closed", out);
      // Destroy for an unknown handle = idempotent no-op.
      break;
    }
    default: {
      if (by_hwnd_.count(ev.info.hwnd)) {
        mergeInto(ev.info.hwnd, ev.info, ev.kind, out);
      } else {
        // Event for a window we never saw being created (missed CREATE):
        // materialize it — announce with the carried groups (the
        // reconciliation scan does the same for whole-scan recovery).
        insertNew(ev.info, out);
      }
      break;
    }
  }
  return out;
}

// ------------------------------------------------------------------- records

void WindowRegistry::insertNew(const NativeWindowInfo& info,
                               std::vector<RegistryNotification>& out) {
  WindowRecord rec;
  rec.hwnd = info.hwnd;
  // Durable identity: a fresh UUID, assigned exactly once at first sight.
  // Never the native handle.
  rec.window_id = makeUuid();
  rec.pid = info.pid;
  rec.tid = info.tid;
  rec.surface_kind = classifySurface(info);
  rec.popup_style = info.is_popup_style;

  if (info.has_title) {
    rec.title = info.title;
    rec.seq_title = ++seq_;
  }
  if (info.has_class) rec.window_class = info.window_class;
  if (info.has_geometry) {
    rec.x = info.x;
    rec.y = info.y;
    rec.width_px = info.width_px;
    rec.height_px = info.height_px;
    rec.dpi_scale_percent = info.dpi_scale_percent;
    rec.seq_geometry = ++seq_;
  }
  if (info.has_visibility) {
    rec.visible = info.visible;
    rec.seq_visibility = ++seq_;
  }
  if (info.has_state) {
    rec.enabled = info.enabled;
    rec.minimized = info.minimized;
    rec.maximized = info.maximized;
    rec.seq_state = ++seq_;
  }
  if (info.has_z_order) {
    rec.z_order_rank = info.z_order_rank;
    rec.seq_z_order = ++seq_;
  }
  if (info.has_active) {
    rec.is_active = info.active;
    rec.seq_active = ++seq_;
  }
  if (info.has_relationships) {
    rec.owner_hwnd = info.owner_hwnd;
    rec.parent_hwnd = info.parent_hwnd;
    rec.root_owner_hwnd = info.root_owner_hwnd;
    rec.seq_relationships = ++seq_;
  }

  by_hwnd_.emplace(rec.hwnd, rec);
  WindowRecord& r = by_hwnd_.at(rec.hwnd);
  id_to_hwnd_[r.window_id] = r.hwnd;

  // Relationships: if the owner/parent is already tracked, fold the
  // resolved remote ids into the Announce itself (no separate Reannounce).
  resolveRelationships(r.hwnd);

  // Session: anchor heuristic + ADR-0006 evidence correlation.
  correlateWithSession(r);

  // Focus bookkeeping: a window that appears already focused participates
  // in the focus chain (clears the previous foreground's active flag).
  if (r.is_active) noteFocusGained(r, out);

  // Modality: derived at birth (the Announce carries the modal bool).
  r.modality = computeModality(r);

  finalizeRole(r);

  out.push_back({RegistryNotificationKind::Announce, r.window_id, r, {}});

  // The new window may complete relationships/evidence for windows
  // tracked earlier (late owner registration).
  onWindowTracked(r.hwnd, out);
}

WindowRecord& WindowRegistry::mergeInto(const std::string& hwnd,
                                        const NativeWindowInfo& info,
                                        WinEventKind kind,
                                        std::vector<RegistryNotification>& out) {
  WindowRecord& rec = by_hwnd_.at(hwnd);
  const bool had_session = !rec.app_session_id.empty();

  bool wire_changed = false;   // a WindowUpdate-expressible field changed
  bool modality_dirty = false;  // enabled flag changed -> re-infer modality
  bool rel_resolved_now = false;

  if (info.has_title && info.title != rec.title) {
    rec.title = info.title;
    rec.seq_title = ++seq_;
    wire_changed = true;
  }
  if (info.has_class && info.window_class != rec.window_class)
    rec.window_class = info.window_class;  // internal only: no wire field

  if (info.has_geometry &&
      (info.x != rec.x || info.y != rec.y ||
       info.width_px != rec.width_px || info.height_px != rec.height_px ||
       info.dpi_scale_percent != rec.dpi_scale_percent)) {
    rec.x = info.x;
    rec.y = info.y;
    rec.width_px = info.width_px;
    rec.height_px = info.height_px;
    rec.dpi_scale_percent = info.dpi_scale_percent;
    rec.seq_geometry = ++seq_;
    wire_changed = true;
  }

  if (info.has_visibility && info.visible != rec.visible) {
    rec.visible = info.visible;
    rec.seq_visibility = ++seq_;
    wire_changed = true;
  }

  if (info.has_state) {
    const bool enabled_changed = info.enabled != rec.enabled;
    const bool shape_changed = info.minimized != rec.minimized ||
                               info.maximized != rec.maximized;
    if (enabled_changed || shape_changed) {
      rec.enabled = info.enabled;
      rec.minimized = info.minimized;
      rec.maximized = info.maximized;
      rec.seq_state = ++seq_;
      if (shape_changed) wire_changed = true;  // state is on the wire
      if (enabled_changed) modality_dirty = true;  // wire effect via `modal`
    }
  }

  if (info.has_z_order && info.z_order_rank != rec.z_order_rank) {
    rec.z_order_rank = info.z_order_rank;
    rec.seq_z_order = ++seq_;
    wire_changed = true;
  }

  if (info.has_active && info.active != rec.is_active) {
    rec.is_active = info.active;
    rec.seq_active = ++seq_;
    wire_changed = true;
  }

  if (info.has_relationships &&
      (rec.owner_hwnd != info.owner_hwnd ||
       rec.parent_hwnd != info.parent_hwnd ||
       rec.root_owner_hwnd != info.root_owner_hwnd)) {
    rec.owner_hwnd = info.owner_hwnd;
    rec.parent_hwnd = info.parent_hwnd;
    rec.root_owner_hwnd = info.root_owner_hwnd;
    rec.seq_relationships = ++seq_;
    rel_resolved_now = resolveRelationships(rec.hwnd);
  }

  // Focus bookkeeping (FR-03 "activation/focus"): the event kind is
  // authoritative — FocusGained sets active=true, FocusLost clears it
  // (the adapter also carries has_active; the kind wins).
  if (kind == WinEventKind::FocusGained) {
    const bool was_active = rec.is_active;
    rec.is_active = true;
    if (!was_active) {
      rec.seq_active = ++seq_;
      wire_changed = true;
    }
    noteFocusGained(rec, out);
  }
  if (kind == WinEventKind::FocusLost) {
    if (rec.is_active) {
      rec.is_active = false;
      rec.seq_active = ++seq_;
      wire_changed = true;
    }
  }

  const bool now_session = !rec.app_session_id.empty();
  const bool session_changed = had_session != now_session;

  // Modality transitions (this window + every window it owns) first, so a
  // trailing Update/Reannounce snapshot carries the final modality.
  if (modality_dirty) refreshModality(rec.hwnd, out);

  if (wire_changed)
    out.push_back({RegistryNotificationKind::Update, rec.window_id, rec, {}});

  if (rel_resolved_now || session_changed) {
    finalizeRole(rec);
    emitReannounce(rec, out);
  }
  return rec;
}

void WindowRegistry::removeRecord(const std::string& hwnd,
                                  const std::string& reason,
                                  std::vector<RegistryNotification>& out) {
  auto it = by_hwnd_.find(hwnd);
  if (it == by_hwnd_.end()) return;
  const WindowRecord rec = it->second;
  by_hwnd_.erase(it);
  id_to_hwnd_.erase(rec.window_id);

  for (auto& [sid, s] : sessions_) {
    if (s.anchor_window_id == rec.window_id) s.anchor_window_id.reset();
    auto m = std::find(s.member_hwnds.begin(), s.member_hwnds.end(), hwnd);
    if (m != s.member_hwnds.end()) s.member_hwnds.erase(m);
  }
  if (prev_focus_id_ == rec.window_id) prev_focus_id_.reset();

  out.push_back({RegistryNotificationKind::Remove, rec.window_id,
                 WindowRecord{}, reason});

  // Children whose owner/parent just died: clear the resolved remote ids
  // (the native edge stays — a later handle reuse re-resolves it).
  for (auto& [h, r] : by_hwnd_) {
    const bool touches =
        (r.owner_hwnd && *r.owner_hwnd == hwnd) ||
        (r.parent_hwnd && *r.parent_hwnd == hwnd);
    if (touches && resolveRelationships(h)) {
      finalizeRole(r);
      emitReannounce(r, out);
    }
  }
}

// -------------------------------------------------------------- relationships

bool WindowRegistry::resolveRelationships(const std::string& hwnd) {
  auto it = by_hwnd_.find(hwnd);
  if (it == by_hwnd_.end()) return false;
  WindowRecord& rec = it->second;
  bool changed = false;
  auto resolve = [&rec, &changed, this](std::optional<std::string>& edge,
                                        std::optional<std::string>& resolved) {
    if (!edge) {
      if (resolved) {
        resolved.reset();
        changed = true;
      }
      return;
    }
    auto oit = by_hwnd_.find(*edge);
    if (oit != by_hwnd_.end()) {
      if (!resolved || *resolved != oit->second.window_id) {
        resolved = oit->second.window_id;
        changed = true;
      }
    } else if (resolved) {
      resolved.reset();
      changed = true;
    }
  };
  resolve(rec.owner_hwnd, rec.owner_window_id);
  resolve(rec.parent_hwnd, rec.parent_window_id);
  return changed;
}

void WindowRegistry::onWindowTracked(const std::string& new_hwnd,
                                     std::vector<RegistryNotification>& out) {
  for (auto& [h, r] : by_hwnd_) {
    if (h == new_hwnd) continue;
    const bool edge_touches =
        (r.owner_hwnd && *r.owner_hwnd == new_hwnd) ||
        (r.parent_hwnd && *r.parent_hwnd == new_hwnd) ||
        (r.root_owner_hwnd && *r.root_owner_hwnd == new_hwnd);
    if (!edge_touches) continue;

    bool changed = resolveRelationships(h);
    if (r.app_session_id.empty()) {
      // The owner chain may now provide ADR-0006 evidence.
      if (!correlateWithSession(r).empty()) changed = true;
    }
    if (changed) {
      finalizeRole(r);
      emitReannounce(r, out);
    }
  }
}

// ------------------------------------------------------------------- sessions

std::string WindowRegistry::correlateWithSession(WindowRecord& rec) {
  if (!rec.app_session_id.empty()) return rec.app_session_id;

  // Anchor heuristic: the session's first seen top-level window with
  // pid == primary_pid is the application's main window. This is the
  // DEFINITION of the session's own window (launch flow), not PID-only
  // auxiliary correlation — ADR-0006's evidence rules still govern every
  // other window (see decideAssociation; PID alone never associates).
  // Note: the anchor is claimed even if weakly-correlated members
  // (e.g. a visible pid-set window) arrived first — the spec defines
  // the anchor as the primary pid's FIRST top-level, period; the launch
  // flow may still override with setSessionAnchor().
  // The anchor must be a REAL surface: a non-zero client area. On
  // interactive Windows desktops the OS creates transient invisible
  // helper windows in the app's own process (zero client area, often
  // owned by the not-yet-completed main window) whose CREATE events can
  // arrive BEFORE the main window's — without the geometry requirement
  // such a helper would steal the anchor (role Main, session) and the
  // real main window would stay Unknown/unassociated (probe-verified on
  // the runner 2026-11-27). A main window created hidden but sized still
  // anchors. A main observed first with no geometry is NOT anchored by
  // the heuristic (correlation runs at first sight / owner-edge
  // changes only) — the launch flow's setSessionAnchor() is the
  // deterministic path for that edge case.
  for (auto& [sid, s] : sessions_) {
    if (rec.pid == s.primary_pid && !s.anchor_window_id &&
        rec.surface_kind == WindowSurfaceKind::IndependentTopLevel &&
        rec.width_px > 0 && rec.height_px > 0) {
      rec.app_session_id = sid;
      s.anchor_window_id = rec.window_id;
      s.member_hwnds.push_back(rec.hwnd);
      return sid;
    }
  }

  // ADR-0006: evidence-based correlation (owner/root-owner chain to a
  // session member, or corroborated pid-set membership). Never PID alone.
  WindowEvidence ev;
  ev.hwnd = rec.hwnd;
  ev.process_id = rec.pid;
  ev.owner_hwnd = rec.owner_hwnd;
  ev.root_owner_hwnd = rec.root_owner_hwnd;
  ev.is_visible = rec.visible;
  ev.is_modal = rec.modality != WindowModality::None;
  for (auto& [sid, s] : sessions_) {
    const AssociationDecision d =
        decideAssociation(ev, toSharedSession(sid, s));
    if (d.associated) {
      rec.app_session_id = sid;
      s.member_hwnds.push_back(rec.hwnd);
      return sid;
    }
  }
  return {};
}

void WindowRegistry::correlateViaFocusTransition(WindowRecord& rec) {
  if (!rec.app_session_id.empty()) return;
  if (!prev_focus_id_ || *prev_focus_id_ == rec.window_id) return;
  auto it = id_to_hwnd_.find(*prev_focus_id_);
  if (it == id_to_hwnd_.end()) return;
  auto pit = by_hwnd_.find(it->second);
  if (pit == by_hwnd_.end()) return;
  const WindowRecord& prev = pit->second;
  if (prev.app_session_id.empty()) return;

  auto sit = sessions_.find(prev.app_session_id);
  if (sit == sessions_.end() || !pidInSession(sit->second, rec.pid)) return;

  // Shared root-owner chain: the same root owner as the member that just
  // held focus, or W is owned by that member directly.
  const bool chain_ok =
      (rec.root_owner_hwnd && prev.root_owner_hwnd &&
       *rec.root_owner_hwnd == *prev.root_owner_hwnd) ||
      (rec.owner_hwnd && *rec.owner_hwnd == prev.hwnd);
  if (!chain_ok) return;

  rec.app_session_id = prev.app_session_id;
  sit->second.member_hwnds.push_back(rec.hwnd);
}

void WindowRegistry::noteFocusGained(WindowRecord& rec,
                                     std::vector<RegistryNotification>& out) {
  if (prev_focus_id_ && *prev_focus_id_ != rec.window_id) {
    auto jt = id_to_hwnd_.find(*prev_focus_id_);
    if (jt != id_to_hwnd_.end()) {
      auto jw = by_hwnd_.find(jt->second);
      if (jw != by_hwnd_.end() && jw->second.is_active) {
        jw->second.is_active = false;
        jw->second.seq_active = ++seq_;
        out.push_back({RegistryNotificationKind::Update, jw->second.window_id,
                       jw->second, {}});
      }
    }
  }
  correlateViaFocusTransition(rec);
  prev_focus_id_ = rec.window_id;
}

// ------------------------------------------------------------------ modality

WindowModality WindowRegistry::computeModality(const WindowRecord& rec) const {
  if (!rec.owner_hwnd) return WindowModality::None;
  auto oit = by_hwnd_.find(*rec.owner_hwnd);
  // An untracked owner counts as enabled: an owned window is at least
  // window-modal by construction.
  const bool owner_enabled = (oit == by_hwnd_.end()) ? true : oit->second.enabled;
  std::vector<bool> sibling_enabled;
  if (oit != by_hwnd_.end()) {
    for (const auto& [h, r] : by_hwnd_) {
      if (r.hwnd == rec.hwnd || r.hwnd == oit->second.hwnd) continue;
      if (r.pid != oit->second.pid || r.owner_hwnd.has_value()) continue;
      if (r.surface_kind != WindowSurfaceKind::IndependentTopLevel) continue;
      sibling_enabled.push_back(r.enabled);
    }
  }
  return inferModality(true, owner_enabled, sibling_enabled);
}

void WindowRegistry::refreshModality(const std::string& hwnd,
                                     std::vector<RegistryNotification>& out) {
  auto it = by_hwnd_.find(hwnd);
  if (it == by_hwnd_.end()) return;
  WindowRecord& rec = it->second;

  const WindowModality m = computeModality(rec);
  if (m != rec.modality) {
    rec.modality = m;
    out.push_back({RegistryNotificationKind::Update, rec.window_id, rec, {}});
  }

  // Every window owned by this one re-infers from the new owner state.
  for (auto& [h, r] : by_hwnd_) {
    if (h == rec.hwnd) continue;
    if (r.owner_hwnd && *r.owner_hwnd == rec.hwnd) {
      const WindowModality rm = computeModality(r);
      if (rm != r.modality) {
        r.modality = rm;
        out.push_back({RegistryNotificationKind::Update, r.window_id, r, {}});
      }
    }
  }
}

// --------------------------------------------------------------------- roles

void WindowRegistry::finalizeRole(WindowRecord& rec) {
  if (rec.surface_kind == WindowSurfaceKind::ChildControl) {
    rec.role = WindowRole::Unknown;
    return;
  }
  bool anchored = false;
  for (const auto& [sid, s] : sessions_)
    if (s.anchor_window_id && *s.anchor_window_id == rec.window_id)
      anchored = true;
  if (anchored) {
    rec.role = WindowRole::Main;
    return;
  }
  // ADR-0006 style heuristics (class + title), independent of ownership.
  if (rec.window_class == kCommonDialogClass) {
    rec.role = titleLooksLikeFilePicker(rec.title) ? WindowRole::FilePicker
                                                   : WindowRole::Dialog;
    return;
  }
  if (rec.owner_hwnd.has_value()) {
    rec.role = rec.popup_style ? WindowRole::Popup : WindowRole::Dialog;
    return;
  }
  if (!rec.app_session_id.empty()) {
    // A session member without an owner and no style heuristic: an
    // auxiliary surface of the application (weak-evidence case — ADR-0006
    // keeps these as auxiliary rather than claiming a specific role).
    rec.role = WindowRole::Auxiliary;
    return;
  }
  rec.role = WindowRole::Unknown;
}

void WindowRegistry::emitReannounce(const WindowRecord& rec,
                                    std::vector<RegistryNotification>& out) {
  out.push_back({RegistryNotificationKind::Reannounce, rec.window_id, rec, {}});
}

// ------------------------------------------------------------- reconciliation

std::vector<RegistryNotification> WindowRegistry::reconcile(
    const std::vector<NativeWindowInfo>& scan, EventSequence scan_started_at) {
  std::vector<RegistryNotification> out;
  std::lock_guard<std::mutex> lk(mu_);

  std::set<std::string> seen;
  for (const auto& info : scan) {
    if (info.hwnd.empty()) continue;
    seen.insert(info.hwnd);

    auto it = by_hwnd_.find(info.hwnd);
    if (it == by_hwnd_.end()) {
      // Missed CREATE: the scan is the recovery path.
      insertNew(info, out);
      continue;
    }
    WindowRecord& rec = it->second;

    // SCAN WINS for current truth — unless a field group was touched by a
    // newer event (EVENTS WIN for history: a stale scan never clobbers it).
    bool wire_changed = false;
    bool modality_dirty = false;
    bool rel_resolved_now = false;

    if (info.has_title && rec.seq_title <= scan_started_at &&
        info.title != rec.title) {
      rec.title = info.title;
      rec.seq_title = ++seq_;
      wire_changed = true;
    }
    if (info.has_class && info.window_class != rec.window_class)
      rec.window_class = info.window_class;  // internal only
    if (info.has_geometry && rec.seq_geometry <= scan_started_at &&
        (info.x != rec.x || info.y != rec.y ||
         info.width_px != rec.width_px || info.height_px != rec.height_px ||
         info.dpi_scale_percent != rec.dpi_scale_percent)) {
      rec.x = info.x;
      rec.y = info.y;
      rec.width_px = info.width_px;
      rec.height_px = info.height_px;
      rec.dpi_scale_percent = info.dpi_scale_percent;
      rec.seq_geometry = ++seq_;
      wire_changed = true;
    }
    if (info.has_visibility && rec.seq_visibility <= scan_started_at &&
        info.visible != rec.visible) {
      rec.visible = info.visible;
      rec.seq_visibility = ++seq_;
      wire_changed = true;
    }
    if (info.has_state && rec.seq_state <= scan_started_at &&
        (info.enabled != rec.enabled || info.minimized != rec.minimized ||
         info.maximized != rec.maximized)) {
      const bool shape_changed = info.minimized != rec.minimized ||
                                 info.maximized != rec.maximized;
      const bool enabled_changed = info.enabled != rec.enabled;
      rec.enabled = info.enabled;
      rec.minimized = info.minimized;
      rec.maximized = info.maximized;
      rec.seq_state = ++seq_;
      if (shape_changed) wire_changed = true;
      if (enabled_changed) modality_dirty = true;
    }
    if (info.has_z_order && rec.seq_z_order <= scan_started_at &&
        info.z_order_rank != rec.z_order_rank) {
      rec.z_order_rank = info.z_order_rank;
      rec.seq_z_order = ++seq_;
      wire_changed = true;
    }
    if (info.has_active && rec.seq_active <= scan_started_at &&
        info.active != rec.is_active) {
      rec.is_active = info.active;
      rec.seq_active = ++seq_;
      wire_changed = true;
    }
    if (info.has_relationships && rec.seq_relationships <= scan_started_at &&
        (rec.owner_hwnd != info.owner_hwnd ||
         rec.parent_hwnd != info.parent_hwnd ||
         rec.root_owner_hwnd != info.root_owner_hwnd)) {
      rec.owner_hwnd = info.owner_hwnd;
      rec.parent_hwnd = info.parent_hwnd;
      rec.root_owner_hwnd = info.root_owner_hwnd;
      rec.seq_relationships = ++seq_;
      rel_resolved_now = resolveRelationships(rec.hwnd);
    }

    if (modality_dirty) refreshModality(rec.hwnd, out);
    if (wire_changed)
      out.push_back({RegistryNotificationKind::Update, rec.window_id, rec, {}});
    if (rel_resolved_now) {
      finalizeRole(rec);
      emitReannounce(rec, out);
    }
  }

  // Missed DESTROY: tracked but absent from the scan.
  std::vector<std::string> gone;
  for (const auto& [hwnd, rec] : by_hwnd_)
    if (!seen.count(hwnd)) gone.push_back(hwnd);
  for (const auto& hwnd : gone) removeRecord(hwnd, "unavailable", out);

  return out;
}

// -------------------------------------------------------------------- queries

std::optional<WindowRecord> WindowRegistry::find(
    const std::string& window_id) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = id_to_hwnd_.find(window_id);
  if (it == id_to_hwnd_.end()) return std::nullopt;
  auto r = by_hwnd_.find(it->second);
  if (r == by_hwnd_.end()) return std::nullopt;
  return r->second;
}

std::optional<WindowRecord> WindowRegistry::findByHwnd(
    const std::string& hwnd) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_hwnd_.find(hwnd);
  if (it == by_hwnd_.end()) return std::nullopt;
  return it->second;
}

std::vector<WindowRecord> WindowRegistry::all() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<WindowRecord> out;
  out.reserve(by_hwnd_.size());
  for (const auto& [hwnd, rec] : by_hwnd_) out.push_back(rec);
  return out;
}

size_t WindowRegistry::size() const {
  std::lock_guard<std::mutex> lk(mu_);
  return by_hwnd_.size();
}

EventSequence WindowRegistry::eventSequence() const {
  std::lock_guard<std::mutex> lk(mu_);
  return seq_;
}

}  // namespace remotewindows::core
