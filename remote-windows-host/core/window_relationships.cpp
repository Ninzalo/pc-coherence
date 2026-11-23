
#include "core/window_relationships.hpp"

#include <algorithm>

#include "core/sha256.hpp"

namespace remotewindows::core {

namespace {
bool inMembers(const std::vector<std::string>& members, const std::string& hwnd) {
  return std::find(members.begin(), members.end(), hwnd) != members.end();
}
}  // namespace

AssociationDecision decideAssociation(const WindowEvidence& w,
                                      const SharedSession& session) {
  if (w.owner_hwnd && inMembers(session.member_hwnds, *w.owner_hwnd))
    return {true, "owner_in_session"};
  if (w.root_owner_hwnd && inMembers(session.member_hwnds, *w.root_owner_hwnd))
    return {true, "root_owner_in_session"};
  if (w.uia_parent_window_id &&
      !w.uia_parent_window_id->empty()) {
    // UIA parents reference remote window ids when streaming, else hwnds.
    for (const auto& m : session.member_hwnds)
      if (w.uia_parent_window_id->find(m) != std::string::npos)
        return {true, "uia_parent_in_session"};
  }
  bool pidKnown = std::find(session.known_pids.begin(),
                            session.known_pids.end(), w.process_id) !=
                  session.known_pids.end();
  // PID membership is corroborating evidence only: it must pair with a
  // structural hint (owner/root-owner present, or modal/dialog flag).
  if (pidKnown && (w.owner_hwnd || w.root_owner_hwnd || w.is_modal))
    return {true, "pid_in_known_app_processes"};
  return {false, "not_associated"};
}

std::string WindowIdentityAllocator::allocate(const std::string& hwnd) {
  auto it = hwnd_to_uuid_.find(hwnd);
  if (it != hwnd_to_uuid_.end()) return it->second;
  // Deterministic but collision-free across process/handle reuse:
  // uuid v4-like from sha256(seed|hwnd|counter). The counter advances per
  // allocation so a reused HWND in a new lifetime gets a fresh id.
  static const char* kSeed = "rwh-window-identity-v1";
  std::string seed = std::string(kSeed) + "|" + hwnd + "|" +
                     std::to_string(next_counter_++);
  auto digest = Sha256::digest_hex(seed);
  std::string uuid = "8f1c";
  uuid += digest.substr(0, 2) + "-";
  uuid += digest.substr(2, 4) + "-4";
  uuid += digest.substr(6, 3) + "-9";
  uuid += digest.substr(9, 3) + "-";
  uuid += digest.substr(12, 12);
  hwnd_to_uuid_[hwnd] = uuid;
  uuid_to_hwnd_[uuid] = hwnd;
  return uuid;
}

void WindowIdentityAllocator::release(const std::string& hwnd) {
  auto it = hwnd_to_uuid_.find(hwnd);
  if (it == hwnd_to_uuid_.end()) return;
  uuid_to_hwnd_.erase(it->second);
  hwnd_to_uuid_.erase(it);
}

std::optional<std::string> WindowIdentityAllocator::lookup(
    const std::string& hwnd) const {
  auto it = hwnd_to_uuid_.find(hwnd);
  if (it == hwnd_to_uuid_.end()) return std::nullopt;
  return it->second;
}

size_t WindowIdentityAllocator::count() const { return hwnd_to_uuid_.size(); }

}  // namespace remotewindows::core
