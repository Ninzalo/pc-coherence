
#ifndef RWH_CORE_WINDOW_RELATIONSHIPS_HPP
#define RWH_CORE_WINDOW_RELATIONSHIPS_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace remotewindows::core {

// FR-03 / Stage 3A: cross-process dialog correlation. Evidence-based;
// PID alone is never sufficient to include OR exclude a window.
struct WindowEvidence {
  std::string hwnd;                    // opaque native handle string
  uint32_t process_id = 0;
  std::optional<std::string> owner_hwnd;      // GetWindow(GW_OWNER)
  std::optional<std::string> root_owner_hwnd; // GetAncestor(GA_ROOTOWNER)
  std::optional<std::string> uia_parent_window_id; // UI Automation parent
  bool is_visible = false;
  bool is_modal = false;
  std::string title;
};

struct SharedSession {
  std::string session_id;
  uint32_t primary_pid = 0;
  std::vector<std::string> member_hwnds;  // known main/streamed windows
  std::vector<uint32_t> known_pids;       // app-owned processes (may grow)
};

struct AssociationDecision {
  bool associated = false;
  std::string reason;  // stable code: "owner_in_session" |
                       // "root_owner_in_session" | "uia_parent_in_session" |
                       // "pid_in_known_app_processes" (weaker, needs corroboration)
                       // "not_associated"
};

// A window is associated with a shared session when ANY of:
//  1. its owner (or root owner) HWND is a session member (strongest);
//  2. its UI Automation parent is a session member;
//  3. its PID is in known_app_processes AND at least one of (owner/root
//     owner set, modal or dialog-like window style flag provided via
//     `modal` or dialog classification). PID alone never decides.
AssociationDecision decideAssociation(const WindowEvidence& w,
                                      const SharedSession& session);

// Remote identity: never the HWND. Stable UUID assigned by the registry
// on first sight; HWND reuse across process restarts must not collide.
struct WindowIdentityAllocator {
  // Returns a fresh UUID for hwnd if unseen; reuses for the same hwnd
  // while it is alive. release() on destruction frees the handle slot.
  std::string allocate(const std::string& hwnd);
  void release(const std::string& hwnd);
  std::optional<std::string> lookup(const std::string& hwnd) const;
  size_t count() const;
 private:
  std::map<std::string, std::string> hwnd_to_uuid_;
  std::map<std::string, std::string> uuid_to_hwnd_;
  uint64_t next_counter_ = 0;
};

}  // namespace remotewindows::core

#endif
