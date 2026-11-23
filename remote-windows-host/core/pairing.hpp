
#ifndef RWH_CORE_PAIRING_HPP
#define RWH_CORE_PAIRING_HPP

// FR-13 / Stage 9: explicit LAN pairing with a one-time code or a
// trusted challenge-response. Secret material never leaves this object
// (and is never logged).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace remotewindows::core {

struct PairingConfig {
  // One-time code shown on the Host (6 digits). Single use.
  std::string one_time_code;
  uint32_t max_attempts = 5;
  uint32_t lockout_seconds = 60;
  uint32_t code_ttl_seconds = 300;
};

struct PairingState {
  bool paired = false;
  bool locked = false;
  uint32_t attempts_used = 0;
  uint64_t lockout_until_ms = 0;
  std::string error;  // stable code: "invalid_code" | "locked" | "expired"
};

// Host-side pairing verifier. Time is injected for testability.
class PairingManager {
 public:
  explicit PairingManager(PairingConfig config);
  // Client presents the code (from the Host display) to pair.
  PairingState verifyCode(const std::string& presented, uint64_t now_ms);
  // Regenerates the one-time code (Host UI: "Show pairing code").
  void rotateCode(std::string& new_code_out, uint64_t now_ms);
  const PairingState& state() const;
  // Derive a stable session key from (code, host_name) — used as the
  // MAC key for the signaling channel (TLS is the stronger path; this
  // is the v0.x lightweight path, see ADR-0001).
  static std::string deriveSessionKey(const std::string& code,
                                      const std::string& host_name);
 private:
  PairingConfig cfg_;
  PairingState state_;
  uint64_t code_created_ms_ = 0;
};

// 32-hex random nonce for pairing challenges (injectable RNG for tests).
std::string makeChallengeNonce(uint64_t seed);

}  // namespace remotewindows::core

#endif
