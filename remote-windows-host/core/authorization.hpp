
#ifndef RWH_CORE_AUTHORIZATION_HPP
#define RWH_CORE_AUTHORIZATION_HPP

// FR-13: capability gating. Unpaired clients may only see hello and
// pairing messages; everything else is rejected before it reaches any
// feature code.

#include <string>

#include "core/protocol_types.hpp"

namespace remotewindows::core {

enum class GateResult {
  ALLOWED = 0,
  DENIED_NOT_PAIRED = 1,
  DENIED_NOT_AUTHORIZED = 2,
  DENIED_UNKNOWN_KIND = 3,
};

struct GateVerdict {
  GateResult result = GateResult::ALLOWED;
  std::string error_code;  // "not_paired" | "not_authorized"
  std::string safe_message;
};

// `pairing_kinds_only` kinds (hello, pairing_challenge, pairing_result,
// heartbeat) are always allowed; all others require pairing, and power
// additionally requires the POWER authorization bit.
class AuthorizationPolicy {
 public:
  explicit AuthorizationPolicy(bool paired, bool power_authorized);
  GateVerdict check(const remotewindows::protocol::Envelope& e) const;
 private:
  bool paired_;
  bool power_authorized_;
};

}  // namespace remotewindows::core

#endif
