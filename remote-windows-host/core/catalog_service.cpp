
#include "core/catalog_service.hpp"

#include <array>
#include <cstdio>
#include <random>

namespace remotewindows::core {

namespace {

protocol::AppType appTypeFromSource(const std::string& source) {
  if (source == "packaged") return protocol::AppType::PACKAGED;
  if (source == "user_added") return protocol::AppType::USER_ADDED;
  return protocol::AppType::WIN32_SHORTCUT;  // "start_menu" + anything else
}

// Same floor the wire codec sees (PROTOCOL §5: catalog_revision >= 1):
// an unscanned or empty store still reports a valid revision.
uint32_t protocolRevision(uint32_t store_revision) {
  return store_revision == 0 ? 1 : store_revision;
}

}  // namespace

// ------------------------------------------------------------- CatalogService

CatalogService::CatalogService(CatalogSource* source) : source_(source) {}

void CatalogService::rescanLocked() const {
  const std::vector<CatalogRecord> recs = source_->scan();
  for (auto r : recs) {
    if (r.identity_key.empty())
      r.identity_key = normalizeIdentity(r.display_name);
    store_.addOrUpdate(std::move(r));
  }
  scanned_ = true;
}

protocol::AppCatalog CatalogService::snapshot(bool force_rescan) {
  std::lock_guard<std::mutex> lk(mu_);
  if (force_rescan || !scanned_) rescanLocked();
  protocol::AppCatalog out;
  out.catalog_revision = protocolRevision(store_.revision());
  out.entries.reserve(store_.records().size());
  for (const auto& r : store_.records()) {
    protocol::AppEntry e;
    e.app_id = r.app_id;
    e.display_name = r.display_name;
    e.type = appTypeFromSource(r.source);
    e.icon_ref = r.icon_ref;
    e.available = r.available;
    out.entries.push_back(std::move(e));
  }
  return out;
}

void CatalogService::ensureScanned() const {
  std::lock_guard<std::mutex> lk(mu_);
  if (!scanned_) rescanLocked();
}

uint32_t CatalogService::revision() const {
  std::lock_guard<std::mutex> lk(mu_);
  return protocolRevision(store_.revision());
}

std::optional<CatalogRecord> CatalogService::find(const std::string& app_id)
    const {
  std::lock_guard<std::mutex> lk(mu_);
  for (const auto& r : store_.records())
    if (r.app_id == app_id) return r;  // copy: safe after lock release
  return std::nullopt;
}

uint32_t CatalogService::lastBroadcastRevision() const {
  std::lock_guard<std::mutex> lk(mu_);
  return last_broadcast_;
}

void CatalogService::markBroadcast(uint32_t revision) {
  std::lock_guard<std::mutex> lk(mu_);
  last_broadcast_ = revision;
}

// ------------------------------------------------------------- LaunchService

LaunchService::LaunchService(const CatalogService& catalog,
                             AppLauncher* launcher, ProcessProbe* probe,
                             std::function<uint64_t()> now_ms)
    : catalog_(catalog),
      launcher_(launcher),
      probe_(probe),
      now_ms_(std::move(now_ms)) {}

protocol::LaunchResult LaunchService::handleLaunch(const std::string& app_id) {
  protocol::LaunchResult res;
  res.app_id = app_id;
  res.state = protocol::LaunchState::FAILED;

  // Allow-list check FIRST: unknown ids never reach the launcher (FR-02).
  // A launch may arrive before the first catalog request: scan once.
  catalog_.ensureScanned();
  const std::optional<CatalogRecord> rec = catalog_.find(app_id);
  if (!rec) {
    res.error_code = "unknown_app_id";
    res.safe_message = "Unknown application";
    return res;
  }
  if (!rec->available) {
    res.error_code = "launch_failed";
    res.safe_message = "Application is not available";
    return res;
  }

  const LaunchRequest req{rec->app_id, rec->launch_target,
                          appTypeFromSource(rec->source)};
  const LaunchOutcome out = launcher_ ? launcher_->launch(req)
                                      : LaunchOutcome{};  // no launcher: fail
  if (!out.ok) {
    res.error_code = out.error_code.empty() ? "launch_failed" : out.error_code;
    res.safe_message =
        out.safe_message.empty() ? "Application could not be launched"
                                 : out.safe_message;
    return res;
  }

  LaunchSession sess;
  sess.app_id = app_id;
  sess.application_session_id = makeUuid();
  sess.state = protocol::LaunchState::STARTED;
  sess.pid = out.pid;
  sess.started_ms = now_ms_ ? now_ms_() : 0;
  const std::string session_id = sess.application_session_id;
  {
    std::lock_guard<std::mutex> lk(mu_);
    sessions_[app_id] = std::move(sess);  // re-launch replaces the record
  }

  res.state = protocol::LaunchState::STARTED;
  res.error_code = "";
  res.safe_message = "";
  res.application_session_id = session_id;
  return res;
}

std::vector<protocol::AppStatus> LaunchService::sweep() {
  std::vector<protocol::AppStatus> out;
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& [app_id, sess] : sessions_) {
    if (sess.closed || sess.pid == 0)
      continue;  // already reported, or liveness unknown
    if (probe_ && !probe_->isAlive(sess.pid)) {
      sess.closed = true;
      protocol::AppStatus st;
      st.app_id = sess.app_id;
      st.state = protocol::AppRunState::CLOSED;
      st.application_session_id = sess.application_session_id;
      out.push_back(std::move(st));
    }
  }
  return out;
}

// Returns a copy (locked): callers never observe the internal map while a
// concurrent handleLaunch/sweep mutates it (review fix, RW-H-008
// integration). Callers may bind the copy to a const reference.
std::map<std::string, LaunchSession> LaunchService::sessions() const {
  std::lock_guard<std::mutex> lk(mu_);
  return sessions_;
}

// ------------------------------------------------------------------ makeUuid

std::string makeUuid() {
  static const char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::array<uint8_t, 16> b{};
  for (auto& x : b) x = static_cast<uint8_t>(rd() & 0xFF);
  b[6] = static_cast<uint8_t>((b[6] & 0x0F) | 0x40);  // version 4
  b[8] = static_cast<uint8_t>((b[8] & 0x3F) | 0x80);  // variant 10xx
  char buf[37];
  const int n = std::snprintf(
      buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
      "%02x%02x%02x%02x%02x%02x",
      b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10],
      b[11], b[12], b[13], b[14], b[15]);
  return std::string(buf, n > 0 ? static_cast<size_t>(n) : 0);
}

}  // namespace remotewindows::core
