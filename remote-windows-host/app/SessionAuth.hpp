#ifndef RWH_APP_SESSIONAUTH_HPP
#define RWH_APP_SESSIONAUTH_HPP

// RW-H-004 (H-NET): per-connection session authorization and the pairing
// state machine over the signaling channel (FR-13; PROTOCOL.md §6/§7).
//
// Portable (no Win32 APIs) so the state machine is unit-testable on any
// host. Components:
//   - PairingHost:             shared host-side one-time-code store.
//   - SessionAuth:             per-connection state machine; pure
//                              handle(envelope) -> replies (no I/O).
//   - SignalingSessionHandler: thin net::SignalingServer::Handler wiring.
//
// Content policy (AGENTS.md rule 9): structured logs only. Secret material
// (one-time codes, challenge nonces, derived session keys) is NEVER logged.

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/pairing.hpp"
#include "core/protocol_types.hpp"
#include "network/signaling_server.hpp"

namespace remotewindows::core {
class CatalogService;
class LaunchService;
}

namespace remotewindows::app {

class Logger;  // forward declared; handler takes an optional pointer

// Per-connection session state (task RW-H-004 requirement 1).
enum class SessionState {
  Unpaired = 0,  // no HELLO yet (or reset)
  Pairing = 1,   // challenge issued, awaiting a valid pairing attempt
  Paired = 2,    // pairing succeeded; session key derived
  Locked = 3,    // attempt limit hit; core lockout in effect
};

const char* sessionStateName(SessionState s);

// RW-H-008: shared cross-session control services (catalog + launch),
// injected by the app shell (RW-H-009 wiring). nullptr = legacy placeholder
// behavior (empty catalog / not_implemented) so the gating semantics and
// all pre-injection behavior stay byte-identical.
struct ControlServices {
  core::CatalogService* catalog = nullptr;
  core::LaunchService* launch = nullptr;
};


// Kinds gated by pairing (task RW-H-004 requirement 3 / PROTOCOL §6).
// While unpaired none of these may be processed; after pairing the
// catalog returns an empty placeholder and the rest answer
// not_implemented until their owning task lands.
bool isGatedControlKind(const std::string& kind);

// Host-side one-time-code store (FR-13: "one-time code shown on the Host,
// single use"). One shared instance per Host process; every unpaired
// session verifies against it, so the attempt limit and lockout are
// code-level (conservative) rather than per-client.
class PairingHost {
 public:
  PairingHost();
  PairingHost(const PairingHost&) = delete;
  PairingHost& operator=(const PairingHost&) = delete;

  // Starts (or restarts) a one-time-code epoch: generates the 6-digit
  // code via core::PairingManager::rotateCode and stamps its creation
  // time (drives the code TTL).
  void beginEpoch(uint64_t now_ms);

  // True while a generated, not-yet-consumed code is available.
  bool hasCode() const;

  // True while the generated code is available AND within its TTL
  // (core::PairingConfig code_ttl_seconds). Consumed OR expired codes
  // report false — the tray's rotate path (FR-13) must not display a
  // dead code.
  bool hasActiveCode(uint64_t now_ms) const;

  // The current code (in-process access, e.g. the selftest client).
  // NEVER log this value.
  std::string currentCode() const;

  // Verifies a presented code; updates attempts / lockout / expiry state.
  core::PairingState verifyCode(const std::string& presented, uint64_t now_ms);

  // Marks the current code consumed (successful pairing).
  void consume();

 private:
  // Must stay in sync with the PairingConfig passed to mgr_ below.
  static constexpr uint32_t kCodeTtlSeconds = 300;
  core::PairingManager mgr_;
  mutable std::mutex mu_;
  std::string code_;  // never logged
  uint64_t code_created_ms_ = 0;  // stamped in beginEpoch (drives TTL)
};

// Per-connection session state machine. Construct one per TCP connection
// and feed it that connection's envelopes in order (the signaling server
// runs one worker thread per connection, so no internal locking is
// needed).
class SessionAuth {
 public:
  explicit SessionAuth(PairingHost* pairing, std::string host_name,
                       std::function<uint64_t()> now_ms = {},
                       ControlServices* services = nullptr);

  SessionState state() const { return state_; }
  const std::string& hostName() const { return host_name_; }

  // Derived session key (empty until paired). Kept in memory for later
  // MAC use (PROTOCOL §7.4, ADR-0001 lightweight path) — never logged.
  const std::string& sessionKey() const { return session_key_; }

  // Processes one client envelope and returns the envelopes to send back
  // (0..2). No I/O. An empty result means "drop" (no reply): unpaired
  // control kinds that have no typed result envelope in schema v0.1.0
  // (input_event, app_catalog_request) and host->client kinds sent in the
  // wrong direction.
  std::vector<protocol::Envelope> handle(const protocol::Envelope& in);

 private:
  uint64_t nowMs() const;
  protocol::Envelope helloReply() const;
  protocol::Envelope challengeReply() const;
  protocol::Envelope pairingReply(bool accepted, const std::string& error_code,
                                  const std::string& safe_message) const;
  std::vector<protocol::Envelope> controlReply(const protocol::Envelope& in,
                                               bool paired);

  PairingHost* pairing_;
  std::string host_name_;
  std::function<uint64_t()> now_ms_;
  ControlServices* services_;  // RW-H-008; nullptr = legacy placeholders
  SessionState state_ = SessionState::Unpaired;
  std::string nonce_;       // issued challenge nonce; never logged
  std::string session_key_;  // never logged
};

// SignalingServer::Handler that maintains one SessionAuth per connection
// and routes envelopes through it. `log` is optional (nullptr in unit
// tests) and only receives structured, secret-free fields.
class SignalingSessionHandler : public net::SignalingServer::Handler {
 public:
  explicit SignalingSessionHandler(std::string host_name, Logger* log = nullptr,
                                   ControlServices* services = nullptr);

  bool onEnvelope(net::Connection& c,
                  const protocol::Envelope& e) override;
  void onDisconnection(net::Connection& c) override;

  // In-process access to the current one-time code (selftest only;
  // never logged).
  std::string currentPairingCode() const { return pairing_.currentCode(); }
  size_t sessionCount() const;

  // FR-13 UI channel (tray "Show pairing code...", RW-H-005): returns
  // the current active one-time code; if the code was already consumed
  // or expired (no active code) it starts a fresh epoch via
  // PairingHost::beginEpoch (core::PairingManager::rotateCode) and
  // returns the new code. Display-only: the value is shown via
  // MessageBox by the caller and is NEVER logged or written to a file.
  std::string rotateOrCurrentCode();

 private:
  std::shared_ptr<SessionAuth> sessionFor(net::Connection* c);

  PairingHost pairing_;
  std::string host_name_;
  Logger* log_;
  ControlServices* services_;  // RW-H-008; passed to every SessionAuth
  mutable std::mutex mu_;
  std::unordered_map<const net::Connection*, std::shared_ptr<SessionAuth>>
      sessions_;
  static uint64_t nowMs();
};

}  // namespace remotewindows::app

#endif
