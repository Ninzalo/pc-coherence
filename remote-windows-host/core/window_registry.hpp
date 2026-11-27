
#ifndef RWH_CORE_WINDOW_REGISTRY_HPP
#define RWH_CORE_WINDOW_REGISTRY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/protocol_types.hpp"
#include "core/window_relationships.hpp"

namespace remotewindows::core {

// RW-H-010 (H-WINDOWS, plan FR-03 "Window Discovery, Relationships, and
// Registry"): a live registry of top-level windows with durable remote
// identity, owner/parent relationship graph, cross-PID auxiliary-window
// correlation (ADR-0006 — never PID alone), modality tracking, and
// snapshot reconciliation that recovers missed events.
//
// PORTABLE: no Win32 includes. Native handles are opaque strings; the
// Win32 adapter (win32/windows/) maps SetWinEventHook events and
// EnumWindows scans onto this interface.
//
// Durable identity: each tracked window gets a UUID `window_id` assigned
// exactly once at first sight. HWNDs are transient (Windows reuses handle
// values) and are NEVER the remote identity.

// Window role. `Auxiliary` is the host-internal classification of a
// session-correlated window without a stronger style-based role. The wire
// WindowRole enum (protocol v0.1.0) has no auxiliary value: toProtocolRole()
// maps it (see the mapping comment below).
enum class WindowRole {
  Unspecified,  // wire: unknown (the wire rejects unspecified announces)
  Main,
  Document,
  Dialog,
  FilePicker,
  Popup,
  Tool,
  Auxiliary,    // wire: dialog (documented mapping, see toProtocolRole)
  Unknown,
};

// Wire-safe role for protocol v0.1.0 WindowRole. Auxiliary has no wire
// counterpart (correlation, not a window style) and maps to Dialog: a
// correlated auxiliary without a style heuristic is presented as a dialog.
// A file-picker style heuristic always wins over the Auxiliary fallback.
inline protocol::WindowRole toProtocolRole(WindowRole role) {
  switch (role) {
    case WindowRole::Main:       return protocol::WindowRole::MAIN;
    case WindowRole::Document:   return protocol::WindowRole::DOCUMENT;
    case WindowRole::Dialog:     return protocol::WindowRole::DIALOG;
    case WindowRole::FilePicker: return protocol::WindowRole::FILE_PICKER;
    case WindowRole::Popup:      return protocol::WindowRole::POPUP;
    case WindowRole::Tool:       return protocol::WindowRole::TOOL;
    case WindowRole::Auxiliary:  return protocol::WindowRole::DIALOG;
    case WindowRole::Unknown:
    case WindowRole::Unspecified:
    default:                     return protocol::WindowRole::UNKNOWN;
  }
}

// Modality. Inferred from owner + owner-enabled state (portable seam; the
// Win32 adapter fills the enabled flags from IsWindowEnabled). The wire
// carries a single `modal` bool: WindowModal and AppModal both serialize
// as modal=true (the finer state is host-internal for the capture layer).
enum class WindowModality {
  None,         // no owner relationship
  WindowModal,  // owned; the owner (and sibling windows) remain usable
  AppModal,     // the owner and its sibling top-levels are disabled
};

// Surface classification for the capture layer (FR-03: "Distinguish true
// child controls contained within the captured parent surface from
// separately rendered top-level/owned/popup surfaces. Do not create a
// duplicate stream for content already included in the parent frame."):
enum class WindowSurfaceKind {
  IndependentTopLevel,  // rendered on its own; its own capture stream
  ChildControl,         // WS_CHILD-style: inside the parent surface —
                        // NEVER a separate stream
};

// One tracked window. Geometry is in physical Windows pixels, DPI-aware
// (dpi_scale_percent = the window's effective DPI, e.g. 150 at 150%).
struct WindowRecord {
  // Durable remote identity (UUID v4, assigned once at first sight).
  // Never the native handle.
  std::string window_id;
  // Transient native handle (opaque). Windows may reuse the value for a
  // different window after destruction — the registry treats a re-created
  // handle as a NEW window with a fresh window_id.
  std::string hwnd;
  uint32_t pid = 0;
  uint32_t tid = 0;
  std::string title;
  std::string window_class;  // internal only (role heuristics); not on the wire
  // Client-area geometry (the WGC capture surface), physical pixels.
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width_px = 0;
  uint32_t height_px = 0;
  uint32_t dpi_scale_percent = 100;
  bool visible = true;      // OS visibility (IsWindowVisible; a minimized
                            // window is still "visible" to the OS)
  bool enabled = true;      // IsWindowEnabled
  bool minimized = false;
  bool maximized = false;
  // Visible top-level z-order: 0 = topmost; -1 = not currently visible.
  int32_t z_order_rank = 0;
  bool is_active = false;   // currently holds focus (foreground window)

  // Native relationship edges (opaque handles).
  std::optional<std::string> owner_hwnd;       // GetWindow(GW_OWNER)
  std::optional<std::string> parent_hwnd;      // GetWindow(GW_PARENT)
  std::optional<std::string> root_owner_hwnd;  // GetAncestor(GA_ROOTOWNER)
  // Resolved remote identities (registry-maintained; nullopt until the
  // other end is itself tracked).
  std::optional<std::string> owner_window_id;
  std::optional<std::string> parent_window_id;

  WindowRole role = WindowRole::Unknown;
  WindowModality modality = WindowModality::None;
  // WS_POPUP-style top-level without an owner/parent frame (role heuristic).
  bool popup_style = false;
  // Empty = not associated with a tracked application session. The wire's
  // WindowAnnounce REQUIRES a non-empty session id (protocol v0.1.0
  // validation: invalid_session_id), so the app layer announces only
  // session-associated windows.
  std::string app_session_id;
  WindowSurfaceKind surface_kind = WindowSurfaceKind::IndependentTopLevel;

  // Per-field recency (reconciliation, "events win for history"): the
  // registry event sequence that last changed each field group.
  uint64_t seq_title = 0;
  uint64_t seq_geometry = 0;
  uint64_t seq_visibility = 0;
  uint64_t seq_state = 0;       // enabled / minimized / maximized
  uint64_t seq_relationships = 0;
  uint64_t seq_z_order = 0;
  uint64_t seq_active = 0;
};

// A single native window observation as delivered by one WinEvent or one
// reconciliation scan entry. `has_*` markers say which field groups the
// observation carries: an event carries only what it knows (a name-change
// event carries only the title), a scan entry carries everything.
struct NativeWindowInfo {
  std::string hwnd;
  uint32_t pid = 0;
  uint32_t tid = 0;

  bool has_title = false;
  std::string title;
  bool has_class = false;
  std::string window_class;
  bool has_geometry = false;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width_px = 0;
  uint32_t height_px = 0;
  uint32_t dpi_scale_percent = 100;
  bool has_visibility = false;
  bool visible = false;
  bool has_state = false;      // enabled / minimized / maximized
  bool enabled = true;
  bool minimized = false;
  bool maximized = false;
  bool has_z_order = false;
  int32_t z_order_rank = 0;
  bool has_active = false;
  bool active = false;
  bool has_relationships = false;
  std::optional<std::string> owner_hwnd;
  std::optional<std::string> parent_hwnd;
  std::optional<std::string> root_owner_hwnd;
  // WS_CHILD-style window (inside a parent surface). The adapter does not
  // track these; the classifier is still exercised portably.
  bool is_child_window = false;
  // WS_POPUP-style top-level without an owner/parent frame.
  bool is_popup_style = false;
};

// The registry's event state machine (FR-03: "creation, showing/hiding,
// destruction, title changes, activation/focus, minimization/restoration,
// location changes, and modal state"):
enum class WinEventKind {
  Create,
  Show,
  Hide,
  Destroy,
  NameChange,
  LocationChange,
  FocusGained,
  FocusLost,
  Minimize,
  Restore,
  EnableChange,  // IsWindowEnabled transition (owner state => modality)
};

struct WindowEvent {
  WinEventKind kind;
  NativeWindowInfo info;  // Create: full info; others: carried groups only
};

// What the registry emits to the app layer (which turns these into
// WindowAnnounce / WindowUpdate / WindowRemove envelopes):
enum class RegistryNotificationKind {
  Announce,    // new window -> window_announce
  Update,      // field change expressible in WindowUpdate
  Reannounce,  // session / role / owner+parent window_id changed. The
               // contract's WindowUpdate has NO parent/owner/session/role
               // fields (v0.1.0), so the app layer re-sends window_announce
               // (idempotent upsert on window_id). CONTRACT gap recorded in
               // docs/STATUS.md.
  Remove,      // window gone -> window_remove
};

struct RegistryNotification {
  RegistryNotificationKind kind;
  std::string window_id;
  // Full record snapshot at notification time (Announce/Update/
  // Reannounce); empty for Remove.
  WindowRecord record;
  // Remove only, a protocol WindowRemovalReason name (lower_snake):
  //   "closed"      - explicit destroy event
  //   "unavailable" - disappeared from a reconciliation scan without a
  //                   destroy event (missed destroy)
  std::string remove_reason;
};

// ------------------------------------------------------------- pure helpers

// Surface classification (FR-03, A5): a WS_CHILD-style control is already
// contained in the parent surface — a separate capture stream would
// duplicate pixels. An owned or popup top-level is independently rendered:
// it needs its own stream.
inline WindowSurfaceKind classifySurface(const NativeWindowInfo& info) {
  if (info.is_child_window ||
      (info.parent_hwnd.has_value() && !info.owner_hwnd.has_value() &&
       !info.is_popup_style))
    return WindowSurfaceKind::ChildControl;
  return WindowSurfaceKind::IndependentTopLevel;
}

// True when the capture layer must allocate its own stream for the record
// (independent top-levels only; child controls are inside the parent).
inline bool requiresOwnCaptureSurface(const WindowRecord& record) {
  return record.surface_kind == WindowSurfaceKind::IndependentTopLevel;
}

// Modality inference (documented rules, portable seam — the inputs are
// faked in tests, read from IsWindowEnabled/EnumWindows in Win32):
//   - no owner                              -> None
//   - owner enabled                         -> WindowModal
//   - owner disabled, and ALL of the owner's sibling top-levels (same pid,
//     no owner, excluding the window itself) are disabled
//                                             -> AppModal
//   - owner disabled, and at least one sibling top-level is enabled (or no
//     siblings were observed)                -> WindowModal
// `sibling_enabled` lists IsWindowEnabled() for each of the owner's
// sibling top-levels (same pid, no owner, excluding the window in
// question and the owner itself).
inline WindowModality inferModality(bool has_owner, bool owner_enabled,
                                    const std::vector<bool>& sibling_enabled) {
  if (!has_owner) return WindowModality::None;
  if (owner_enabled) return WindowModality::WindowModal;
  bool all_siblings_disabled = true;
  for (const bool en : sibling_enabled)
    if (en) { all_siblings_disabled = false; break; }
  return all_siblings_disabled ? WindowModality::AppModal
                               : WindowModality::WindowModal;
}

using EventSequence = uint64_t;

// The live registry: event-driven state machine + snapshot reconciliation
// + relationship graph + application-session correlation.
//
// Thread-safe: every public method may be called from any thread. The
// Win32 adapter delivers events and scans from its single hook thread, so
// event/scan ordering is naturally serial there; the mutex protects the
// portable core regardless.
class WindowRegistry {
 public:
  // Register an application session (e.g. from the launch flow).
  // `primary_pid` is the launched process; `known_pids` are app-owned
  // helper processes (a brokered dialog may live in one). The FIRST
  // top-level window seen with pid == primary_pid becomes the session's
  // anchor (the application's main window — the definition of the session,
  // not a PID-only correlation of an auxiliary window); the launch flow
  // may instead call setSessionAnchor() when it detects the window.
  void registerSession(const std::string& session_id, uint32_t primary_pid,
                       const std::vector<uint32_t>& known_pids = {});

  bool hasSession(const std::string& session_id) const;

  // Explicit anchor (launch flow: window_detected for this session).
  // Associates the window with the session, makes it the anchor, and
  // finalizes its role to Main. Returns false for an unknown session or
  // window.
  bool setSessionAnchor(const std::string& session_id,
                        const std::string& window_id);

  // Explicit association (e.g. a window detected for a launched app).
  // Returns false for an unknown window or session.
  bool associateWindow(const std::string& window_id,
                       const std::string& session_id);

  // Apply one event, in delivery order. Returns the notifications the app
  // layer should emit (possibly empty: idempotent re-delivery and
  // no-change updates produce nothing).
  std::vector<RegistryNotification> applyEvent(const WindowEvent& ev);

  // Reconcile against a full snapshot scan: the list of live top-level
  // windows as enumerated by the OS (z-order ranks assigned by the
  // scanner). `scan_started_at` is the registry event sequence captured
  // BEFORE the scan started (call eventSequence() on the adapter side).
  // Semantics (FR-03 "reconciliation scans to recover missed events"):
  //  - SCAN WINS for current truth: any field group not touched by a
  //    newer event is overwritten with the scanned value.
  //  - EVENTS WIN for history: a field group changed by an event after
  //    `scan_started_at` keeps the event's value; the stale scan never
  //    clobbers it.
  //  - A window in the scan but not tracked is announced (missed CREATE).
  //  - A window tracked but absent from the scan is removed with reason
  //    "unavailable" (missed DESTROY).
  //  - Idempotent: a second identical scan emits nothing.
  std::vector<RegistryNotification> reconcile(
      const std::vector<NativeWindowInfo>& scan, EventSequence scan_started_at);

  // Queries (value copies; safe to use after the registry is destroyed).
  std::optional<WindowRecord> find(const std::string& window_id) const;
  std::optional<WindowRecord> findByHwnd(const std::string& hwnd) const;
  std::vector<WindowRecord> all() const;
  size_t size() const;
  // Current event sequence — pass as `scan_started_at` to reconcile().
  EventSequence eventSequence() const;

 private:
  struct Session {
    uint32_t primary_pid = 0;
    std::vector<uint32_t> known_pids;
    std::optional<std::string> anchor_window_id;
    std::vector<std::string> member_hwnds;  // anchor + correlated members
  };

  // Insert a brand-new window (fresh UUID) and emit its Announce.
  void insertNew(const NativeWindowInfo& info,
                 std::vector<RegistryNotification>& out);
  // Merge the carried groups into an existing record; emit an Update if a
  // wire-visible field changed. Returns the record (by reference).
  WindowRecord& mergeInto(const std::string& hwnd, const NativeWindowInfo& info,
                          WinEventKind kind,
                          std::vector<RegistryNotification>& out);
  // Remove a record (destroy/missed-destroy) + emit a Remove.
  void removeRecord(const std::string& hwnd, const std::string& reason,
                    std::vector<RegistryNotification>& out);
  // Resolve owner/parent remote ids for `hwnd`. Returns true when a
  // previously unresolved (or newly invalidated) relationship changed —
  // the caller emits the Reannounce (a freshly announced window folds the
  // resolution into its Announce instead).
  bool resolveRelationships(const std::string& hwnd);
  // Re-infer modality for `hwnd` (owner + owner-enabled state) and for
  // every window it owns; emit Updates on transitions.
  void refreshModality(const std::string& hwnd,
                       std::vector<RegistryNotification>& out);
  // Derived modality from current registry state (no notifications).
  WindowModality computeModality(const WindowRecord& rec) const;
  // Session correlation for a newly announced window (ADR-0006 rules via
  // decideAssociation; anchor heuristic for the primary pid's first
  // top-level). Mutates the record + session bookkeeping; emits nothing.
  // Returns the assigned session id (possibly empty).
  std::string correlateWithSession(WindowRecord& rec);
  // Focus-transition correlation (ADR-0006 + plan FR-03 focus transitions):
  // an uncorrelated window that gains focus right after a session member,
  // sharing a root-owner chain and belonging to the session's pid set.
  // Mutates the record; emits nothing (the caller emits the Reannounce).
  void correlateViaFocusTransition(WindowRecord& rec);
  // Shared focus bookkeeping for a window that just gained focus: clear
  // the previous foreground window's active flag (emitting its Update),
  // run focus-transition correlation, and record it as the new
  // prev-focus window.
  void noteFocusGained(WindowRecord& rec, std::vector<RegistryNotification>& out);
  // A window became tracked: re-resolve relationships of windows that
  // reference it (late owner/parent registration), re-run session
  // correlation for windows whose owner chain now provides evidence, and
  // emit Reannounces for everything that changed.
  void onWindowTracked(const std::string& new_hwnd,
                       std::vector<RegistryNotification>& out);
  // Final role assignment (style heuristics per ADR-0006 + session role).
  void finalizeRole(WindowRecord& rec);
  // A window changed session/role/relationships -> Reannounce (the wire's
  // WindowUpdate cannot express those).
  void emitReannounce(const WindowRecord& rec,
                      std::vector<RegistryNotification>& out);

  bool pidInSession(const Session& s, uint32_t pid) const;
  SharedSession toSharedSession(const std::string& session_id,
                                const Session& s) const;

  mutable std::mutex mu_;
  std::map<std::string, WindowRecord> by_hwnd_;
  std::map<std::string, std::string> id_to_hwnd_;
  std::map<std::string, Session> sessions_;
  EventSequence seq_ = 0;
  // Last top-level window that gained focus (focus-chain correlation).
  std::optional<std::string> prev_focus_id_;
};

}  // namespace remotewindows::core

#endif  // RWH_CORE_WINDOW_REGISTRY_HPP
