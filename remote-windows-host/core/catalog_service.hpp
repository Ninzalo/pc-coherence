
#ifndef RWH_CORE_CATALOG_SERVICE_HPP
#define RWH_CORE_CATALOG_SERVICE_HPP

// RW-H-008 (H-CATALOG): portable catalog + launch services (FR-01/FR-02).
// Portable (no platform headers) so both services are unit-testable on any
// host; the Win32 implementations of CatalogSource/AppLauncher/ProcessProbe
// land in RW-H-009.

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/catalog.hpp"
#include "core/protocol_types.hpp"

namespace remotewindows::core {

// Pluggable catalog source (Win32 Start Menu walk = RW-H-009; tests use an
// in-memory fake). scan() runs on the calling thread.
class CatalogSource {
 public:
  virtual ~CatalogSource() = default;
  virtual std::vector<CatalogRecord> scan() = 0;
};

// Thread-safe (shared across per-connection worker threads).
class CatalogService {
 public:
  explicit CatalogService(CatalogSource* source);
  // First call or force_rescan -> source->scan() + merge into CatalogStore
  // (dedupe per core::dedupeCatalog semantics via CatalogStore::addOrUpdate).
  // Returns a protocol-ready snapshot: catalog_revision = store revision
  // (floored at 1: 0 is not wire-valid, PROTOCOL §5), entries mapped
  // CatalogRecord -> protocol::AppEntry (source string -> AppType:
  // start_menu -> WIN32_SHORTCUT, packaged -> PACKAGED,
  // user_added -> USER_ADDED; anything else -> WIN32_SHORTCUT).
  protocol::AppCatalog snapshot(bool force_rescan = false);
  // Scans once if not scanned yet (idempotent; force_rescan stays the only
  // rescan trigger). Launch may arrive before the first catalog request.
  // const: the service is internally synchronized (logical constness).
  void ensureScanned() const;
  // Protocol-visible revision (store revision floored at 1).
  uint32_t revision() const;
  // nullptr if absent. Valid until the next rescan (the store vector may be
  // reallocated by a later addOrUpdate).
  const CatalogRecord* find(const std::string& app_id) const;
  // app_catalog_changed bookkeeping (heartbeat piggyback).
  uint32_t lastBroadcastRevision() const;
  void markBroadcast(uint32_t revision);

 private:
  // Caller holds mu_: scans the source and merges into store_.
  void rescanLocked() const;

  mutable std::mutex mu_;
  CatalogSource* source_;
  mutable CatalogStore store_;
  mutable bool scanned_ = false;
  uint32_t last_broadcast_ = 0;
};

// Launch requests carry ONLY catalog-resolved data (FR-02): the client
// never sends paths or commands, and the launcher interface cannot receive
// anything but what the catalog resolved (plan rule 9).
struct LaunchRequest {
  std::string app_id;
  std::string launch_target;  // from the catalog; never client input
  protocol::AppType type = protocol::AppType::WIN32_SHORTCUT;
};

struct LaunchOutcome {
  bool ok = false;
  std::string error_code;    // "" on success; e.g. "launch_failed"
  std::string safe_message;  // stable, user-safe; no paths, no OS raw errors
  uint32_t pid = 0;          // 0 = unknown
};

// Win32 implementation (ShellExecuteExW) = RW-H-009; tests use a fake.
class AppLauncher {
 public:
  virtual ~AppLauncher() = default;
  virtual LaunchOutcome launch(const LaunchRequest& req) = 0;
};

// Win32 implementation = RW-H-009; tests use a fake.
class ProcessProbe {
 public:
  virtual ~ProcessProbe() = default;
  virtual bool isAlive(uint32_t pid) = 0;
};

struct LaunchSession {
  std::string app_id;
  std::string application_session_id;  // makeUuid()
  protocol::LaunchState state = protocol::LaunchState::UNSPECIFIED;
  uint32_t pid = 0;
  uint64_t started_ms = 0;
  // Dead-process transition already reported via app_status. (The wire
  // LaunchState has no CLOSED value — app_status carries the
  // AppRunState::CLOSED; this flag is bookkeeping only.)
  bool closed = false;
};

// Tracks launch sessions by app_id. Thread-safe. Process start vs window
// appearance are separate events (FR-02): this service stops at STARTED;
// window detection arrives later via window_announce (H-WINDOWS).
class LaunchService {
 public:
  LaunchService(const CatalogService& catalog, AppLauncher* launcher,
                ProcessProbe* probe, std::function<uint64_t()> now_ms = {});
  // Unknown app_id -> FAILED + error_code "unknown_app_id" and the launcher
  // is NEVER called. Unavailable entry -> FAILED + "launch_failed".
  // Launcher failure -> FAILED + launcher's error_code/safe_message.
  // Success -> new session (fresh uuid, state STARTED, pid from outcome),
  // result: state STARTED, application_session_id set, error_code "".
  // Re-launch of a still-live app_id is allowed (the launcher/OS decides
  // whether a second instance starts); the session record is replaced.
  protocol::LaunchResult handleLaunch(const std::string& app_id);
  // Sweep live sessions (not yet reported, pid != 0): dead process ->
  // mark closed; return one AppStatus{app_id, CLOSED, session_id} per
  // transition (each session is reported at most once).
  std::vector<protocol::AppStatus> sweep();
  const std::map<std::string, LaunchSession>& sessions() const;

 private:
  mutable std::mutex mu_;
  const CatalogService& catalog_;
  AppLauncher* launcher_;
  ProcessProbe* probe_;
  std::function<uint64_t()> now_ms_;
  std::map<std::string, LaunchSession> sessions_;
};

// UUID-like v4: 8-4-4-4-12 lowercase hex, version nibble '4', variant in
// {8,9,a,b}; std::random_device based. Identity only, not a security token.
std::string makeUuid();

}  // namespace remotewindows::core

#endif
