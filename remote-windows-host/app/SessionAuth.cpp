
#include "app/SessionAuth.hpp"

#include <chrono>
#include <cstdio>

#include "app/Logger.hpp"
#include "core/catalog_service.hpp"

namespace remotewindows::app {

namespace {

uint64_t steadyMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

const char* safeMessageFor(const std::string& error_code) {
  // User-safe, machine-stable descriptions. Never contain secrets.
  if (error_code == "invalid_code")
    return "Pairing code was not accepted";
  if (error_code == "locked")
    return "Too many attempts; pairing is temporarily locked";
  if (error_code == "expired")
    return "Pairing code expired";
  if (error_code == "not_paired")
    return "Pair with the host before sending this message";
  if (error_code == "not_implemented")
    return "Not implemented yet";
  return "Operation not allowed in the current session state";
}

}  // namespace

const char* sessionStateName(SessionState s) {
  switch (s) {
    case SessionState::Unpaired: return "unpaired";
    case SessionState::Pairing: return "pairing";
    case SessionState::Paired: return "paired";
    case SessionState::Locked: return "locked";
  }
  return "unknown";
}

bool isGatedControlKind(const std::string& kind) {
  return kind == "app_catalog_request" || kind == "launch_app" ||
         kind == "input_event" || kind == "resize_request" ||
         kind == "window_action" || kind == "frame_rate_request" ||
         kind == "system_power_request";
}

// --------------------------------------------------------------- PairingHost

PairingHost::PairingHost()
    // PROTOCOL.md §7.4: 3 failures then a 60 s lockout; code TTL per
    // core::PairingConfig default (300 s).
    : mgr_(core::PairingConfig{"", 3, 60, kCodeTtlSeconds}) {}

void PairingHost::beginEpoch(uint64_t now_ms) {
  std::lock_guard<std::mutex> lk(mu_);
  std::string code;
  mgr_.rotateCode(code, now_ms);
  code_ = std::move(code);
  code_created_ms_ = now_ms;
}

bool PairingHost::hasCode() const {
  std::lock_guard<std::mutex> lk(mu_);
  return !code_.empty();
}

bool PairingHost::hasActiveCode(uint64_t now_ms) const {
  std::lock_guard<std::mutex> lk(mu_);
  if (code_.empty()) return false;
  // Same comparison as core::PairingManager::verifyCode (strict >).
  return now_ms - code_created_ms_ <= uint64_t(kCodeTtlSeconds) * 1000;
}

std::string PairingHost::currentCode() const {
  std::lock_guard<std::mutex> lk(mu_);
  return code_;
}

core::PairingState PairingHost::verifyCode(const std::string& presented,
                                           uint64_t now_ms) {
  std::lock_guard<std::mutex> lk(mu_);
  return mgr_.verifyCode(presented, now_ms);
}

void PairingHost::consume() {
  std::lock_guard<std::mutex> lk(mu_);
  code_.clear();
}

// -------------------------------------------------------------- SessionAuth

SessionAuth::SessionAuth(PairingHost* pairing, std::string host_name,
                         std::function<uint64_t()> now_ms,
                         ControlServices* services)
    : pairing_(pairing),
      host_name_(std::move(host_name)),
      now_ms_(std::move(now_ms)),
      services_(services) {}

uint64_t SessionAuth::nowMs() const {
  return now_ms_ ? now_ms_() : steadyMs();
}

protocol::Envelope SessionAuth::helloReply() const {
  protocol::Envelope reply;
  reply.kind = "hello";
  protocol::Hello h;
  h.product_name = "windows-host";
  h.app_version = "0.1.0";
  h.capabilities = {protocol::Capability::APP_CATALOG,
                    protocol::Capability::LAUNCH,
                    protocol::Capability::MULTI_WINDOW,
                    protocol::Capability::INPUT,
                    protocol::Capability::RESIZE,
                    protocol::Capability::POWER,
                    protocol::Capability::AUXILIARY_WINDOWS};
  reply.payload = h;
  return reply;
}

protocol::Envelope SessionAuth::challengeReply() const {
  protocol::Envelope challenge;
  challenge.kind = "pairing_challenge";
  protocol::PairingChallenge pc;
  pc.host_name = host_name_;
  pc.challenge_nonce = core::makeChallengeNonce(nowMs());
  challenge.payload = pc;
  return challenge;
}

protocol::Envelope SessionAuth::pairingReply(bool accepted,
                                             const std::string& error_code,
                                             const std::string& safe_message)
    const {
  protocol::Envelope reply;
  reply.kind = "pairing_result";
  protocol::PairingResult pr;
  pr.accepted = accepted;
  pr.error_code = error_code;
  pr.safe_message = safe_message.empty() ? std::string() : safe_message;
  reply.payload = pr;
  return reply;
}

std::vector<protocol::Envelope> SessionAuth::controlReply(
    const protocol::Envelope& in, bool paired) {
  // Typed result envelope per request kind (PROTOCOL §6: "denied,
  // error_code = not_paired" while unpaired). request_id is echoed on
  // K_RESULT kinds. Two kinds have NO typed result envelope in schema
  // v0.1.0 (input_event has no result kind; app_catalog carries no error
  // field) — those are dropped (empty return) and logged by the handler;
  // a CONTRACT change request covers adding the missing result kinds.
  const std::string& k = in.kind;

  if (k == "input_event") return {};

  if (k == "app_catalog_request") {
    if (!paired) return {};  // no error field on app_catalog; drop
    protocol::Envelope out;
    out.kind = "app_catalog";
    out.request_id = in.request_id;
    if (services_ && services_->catalog) {
      // RW-H-008: real catalog from the shared CatalogService (FR-01).
      bool force = false;
      if (const auto* req =
              std::get_if<protocol::AppCatalogRequest>(&in.payload))
        force = req->force_rescan;
      out.payload = services_->catalog->snapshot(force);
    } else {
      protocol::AppCatalog cat;
      cat.catalog_revision = 1;  // placeholder; empty entries (catalog
                                 // scanner is a later task — do not invent)
      cat.entries = {};
      out.payload = cat;
    }
    return {out};
  }

  if (k == "launch_app") {
    protocol::Envelope out;
    out.kind = "launch_result";
    out.request_id = in.request_id;
    protocol::LaunchResult lr;
    if (const auto* la = std::get_if<protocol::LaunchApp>(&in.payload))
      lr.app_id = la->app_id;
    if (paired && services_ && services_->launch) {
      // RW-H-008: real launch through the allow-listed LaunchService
      // (FR-02); the client sent only an app_id.
      lr = services_->launch->handleLaunch(lr.app_id);
    } else {
      lr.state = protocol::LaunchState::FAILED;
      lr.error_code = paired ? "not_implemented" : "not_paired";
      lr.safe_message = safeMessageFor(lr.error_code);
    }
    out.payload = lr;
    return {out};
  }

  if (k == "resize_request") {
    protocol::Envelope out;
    out.kind = "resize_ack";
    out.request_id = in.request_id;
    protocol::ResizeAck ra;
    if (const auto* rr = std::get_if<protocol::ResizeRequest>(&in.payload))
      ra.window_id = rr->window_id;
    ra.ok = false;
    ra.error_code = paired ? "not_implemented" : "not_paired";
    out.payload = ra;
    return {out};
  }

  if (k == "window_action") {
    protocol::Envelope out;
    out.kind = "window_action_result";
    out.request_id = in.request_id;
    protocol::WindowActionResult war;
    if (const auto* wa = std::get_if<protocol::WindowAction>(&in.payload)) {
      war.window_id = wa->window_id;
      war.kind = wa->kind;
    }
    war.state = protocol::WindowActionResultState::DENIED;
    war.error_code = paired ? "not_implemented" : "not_paired";
    war.safe_message = safeMessageFor(war.error_code);
    out.payload = war;
    return {out};
  }

  if (k == "frame_rate_request") {
    protocol::Envelope out;
    out.kind = "stream_status";
    protocol::StreamStatus ss;
    if (const auto* fr = std::get_if<protocol::FrameRateRequest>(&in.payload))
      ss.window_id = fr->window_id;
    ss.state = protocol::StreamState::ERROR;
    ss.error_code = paired ? "not_implemented" : "not_paired";
    out.payload = ss;
    return {out};
  }

  if (k == "system_power_request") {
    protocol::Envelope out;
    out.kind = "system_power_result";
    out.request_id = in.request_id;
    protocol::SystemPowerResult spr;
    if (const auto* sp =
            std::get_if<protocol::SystemPowerRequest>(&in.payload))
      spr.action = sp->action;
    spr.state = protocol::PowerResultState::DENIED;
    spr.error_code = paired ? "not_implemented" : "not_paired";
    spr.safe_message = safeMessageFor(spr.error_code);
    out.payload = spr;
    return {out};
  }

  return {};  // defensive: unknown gated kind
}

std::vector<protocol::Envelope> SessionAuth::handle(
    const protocol::Envelope& in) {
  // ---- HELLO: always accepted; (re)issues the challenge when unpaired ----
  if (in.kind == "hello") {
    std::vector<protocol::Envelope> out{helloReply()};
    if (state_ != SessionState::Paired) {
      // Fresh nonce per challenge; the previous one stops being valid.
      nonce_ = core::makeChallengeNonce(nowMs());
      out.push_back(challengeReply());
      state_ = SessionState::Pairing;
    }
    return out;
  }

  // ---- PAIRING (client presents code + echoed nonce) --------------------
  if (in.kind == "pairing_challenge") {
    if (state_ == SessionState::Paired)
      return {pairingReply(true, "", "")};  // idempotent
    if (state_ == SessionState::Unpaired)
      return {pairingReply(false, "invalid_state",
                           "Send the pairing code after the host challenge")};

    // Pairing or Locked: verify nonce echo AND code.
    const auto* pc = std::get_if<protocol::PairingChallenge>(&in.payload);
    const bool nonce_ok =
        pc && pc->challenge_nonce.has_value() &&
        *pc->challenge_nonce == nonce_;
    const bool code_available = pairing_ && pairing_->hasCode();

    if (!nonce_ok || !code_available) {
      // Count the failed attempt (shared manager enforces the attempt
      // limit and lockout) but never pair: empty presentation can only
      // ever mismatch.
      const core::PairingState st =
          pairing_ ? pairing_->verifyCode("", nowMs())
                   : core::PairingState{};
      const std::string err = st.locked ? "locked" : "invalid_code";
      state_ = st.locked ? SessionState::Locked : SessionState::Pairing;
      return {pairingReply(false, err, safeMessageFor(err))};
    }

    const std::string presented = pc->one_time_code.value_or("");
    const core::PairingState st = pairing_->verifyCode(presented, nowMs());
    if (st.paired) {
      state_ = SessionState::Paired;
      session_key_ =
          core::PairingManager::deriveSessionKey(presented, host_name_);
      pairing_->consume();  // one-time code: single use
      return {pairingReply(true, "", "")};
    }
    state_ = st.locked ? SessionState::Locked : SessionState::Pairing;
    return {pairingReply(false, st.error, safeMessageFor(st.error))};
  }

  // ---- pairing_result is host->client only (PROTOCOL §3) ----------------
  if (in.kind == "pairing_result") {
    return {pairingReply(false, "invalid_state",
                         "pairing_result is sent by the host only")};
  }

  // ---- HEARTBEAT: allowed in every state (FR-02/FR-05 gating) -----------
  if (in.kind == "heartbeat") {
    protocol::Envelope out;
    out.kind = "heartbeat";
    protocol::Heartbeat hb;
    hb.sender_time_ms = static_cast<int64_t>(nowMs());
    out.payload = hb;
    std::vector<protocol::Envelope> replies{std::move(out)};
    // RW-H-008 heartbeat piggyback (thread-local: this connection's
    // replies only — net::Connection is owned by its worker thread).
    // Two concurrent workers may both observe the revision change and
    // both notify; a duplicate app_catalog_changed is idempotent for the
    // client, so no cross-connection coordination is needed.
    if (services_ && services_->catalog) {
      const uint32_t rev = services_->catalog->revision();
      if (rev != services_->catalog->lastBroadcastRevision()) {
        services_->catalog->markBroadcast(rev);
        protocol::Envelope ev;
        ev.kind = "app_catalog_changed";
        protocol::AppCatalogChanged ac;
        ac.catalog_revision = rev;
        ev.payload = ac;
        replies.push_back(std::move(ev));
      }
    }
    if (services_ && services_->launch) {
      for (auto st : services_->launch->sweep()) {
        protocol::Envelope ev;
        ev.kind = "app_status";
        ev.payload = std::move(st);
        replies.push_back(std::move(ev));
      }
    }
    return replies;
  }

  // ---- gated control kinds ----------------------------------------------
  if (isGatedControlKind(in.kind))
    return controlReply(in, state_ == SessionState::Paired);

  // Host->client kinds and anything else reaching the handler: drop.
  // (The server already dropped unknown/malformed envelopes before this
  // point; direction misuse is logged by the handler.)
  return {};
}

// ---------------------------------------------------- SignalingSessionHandler

SignalingSessionHandler::SignalingSessionHandler(std::string host_name,
                                                 Logger* log,
                                                 ControlServices* services)
    : host_name_(std::move(host_name)), log_(log), services_(services) {
  pairing_.beginEpoch(nowMs());
  if (log_)
    log_->write(LogLevel::Info, "host.pairing_epoch",
                {{"host", host_name_}});  // no code material in logs
}

std::shared_ptr<SessionAuth> SignalingSessionHandler::sessionFor(
    net::Connection* c) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = sessions_.find(c);
  if (it == sessions_.end()) {
    std::function<uint64_t()> default_clock;
    auto s = std::make_shared<SessionAuth>(
        &pairing_, host_name_, default_clock, services_);
    it = sessions_.emplace(c, std::move(s)).first;
  }
  return it->second;
}

size_t SignalingSessionHandler::sessionCount() const {
  std::lock_guard<std::mutex> lk(mu_);
  return sessions_.size();
}

std::string SignalingSessionHandler::rotateOrCurrentCode() {
  const uint64_t now = nowMs();
  if (!pairing_.hasActiveCode(now)) pairing_.beginEpoch(now);
  return pairing_.currentCode();  // never logged
}

bool SignalingSessionHandler::onEnvelope(net::Connection& c,
                                         const protocol::Envelope& e) {
  auto session = sessionFor(&c);
  std::vector<protocol::Envelope> replies = session->handle(e);

  if (log_) {
    // Structured, secret-free audit log: peer, kind, state, and stable
    // error codes only. Never log codes, nonces, or session keys.
    std::vector<std::pair<std::string, std::string>> fields = {
        {"peer", c.peerName()},
        {"kind", e.kind},
        {"state", sessionStateName(session->state())}};
    const char* event = "session.message";
    if (e.kind == "hello") {
      event = "session.hello";
    } else if (e.kind == "pairing_challenge" || e.kind == "pairing_result") {
      event = "session.pairing";
      if (!replies.empty() && replies[0].kind == "pairing_result") {
        if (const auto* pr =
                std::get_if<protocol::PairingResult>(&replies[0].payload)) {
          fields.push_back(
              {"accepted", pr->accepted ? "true" : "false"});
          if (!pr->error_code.empty())
            fields.push_back({"error", pr->error_code});
        }
      }
    } else if (e.kind == "app_catalog_request" &&
               session->state() == SessionState::Paired) {
      // RW-H-008: real catalog reply. Stable fields only — never paths.
      event = "session.catalog";
      if (const auto* req =
              std::get_if<protocol::AppCatalogRequest>(&e.payload))
        fields.push_back({"force", req->force_rescan ? "true" : "false"});
      if (!replies.empty() && replies[0].kind == "app_catalog")
        if (const auto* cat =
                std::get_if<protocol::AppCatalog>(&replies[0].payload)) {
          fields.push_back({"entries", std::to_string(cat->entries.size())});
          fields.push_back(
              {"revision", std::to_string(cat->catalog_revision)});
        }
    } else if (e.kind == "launch_app" &&
               session->state() == SessionState::Paired) {
      // RW-H-008: real launch result. app_id only — never the launch
      // target, paths, or OS errors.
      event = "session.launch";
      if (const auto* la = std::get_if<protocol::LaunchApp>(&e.payload))
        fields.push_back({"app_id", la->app_id});
      if (!replies.empty() && replies[0].kind == "launch_result")
        if (const auto* lr =
                std::get_if<protocol::LaunchResult>(&replies[0].payload)) {
          fields.push_back({"state", protocol::launchStateName(lr->state)});
          if (!lr->error_code.empty())
            fields.push_back({"error", lr->error_code});
        }
    } else if (isGatedControlKind(e.kind) &&
               session->state() != SessionState::Paired) {
      event = "session.gated";  // dropped or not_paired rejection
    }
    log_->write(LogLevel::Info, event, fields);
  }

  std::string err;
  for (auto& r : replies)
    if (!c.sendEnvelope(r, err)) return false;
  return true;
}

void SignalingSessionHandler::onDisconnection(net::Connection& c) {
  std::shared_ptr<SessionAuth> session;
  {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = sessions_.find(&c);
    if (it != sessions_.end()) {
      session = it->second;
      sessions_.erase(it);
    }
  }
  if (log_ && session)
    log_->write(LogLevel::Info, "session.disconnect",
                {{"peer", c.peerName()},
                 {"state", sessionStateName(session->state())}});
}

uint64_t SignalingSessionHandler::nowMs() { return steadyMs(); }

}  // namespace remotewindows::app
