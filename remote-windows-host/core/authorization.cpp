
#include "core/authorization.hpp"

#include "core/protocol_validation.hpp"

namespace remotewindows::core {

AuthorizationPolicy::AuthorizationPolicy(bool paired, bool power_authorized)
    : paired_(paired), power_authorized_(power_authorized) {}

GateVerdict AuthorizationPolicy::check(
    const remotewindows::protocol::Envelope& e) const {
  using namespace remotewindows::protocol;
  const KindInfo* info = nullptr;
  if (!findKind(e.kind, &info))
    return {GateResult::DENIED_UNKNOWN_KIND, "unknown_kind", e.kind};

  bool pairingKind = e.kind == "hello" || e.kind == "pairing_challenge" ||
                     e.kind == "pairing_result" || e.kind == "heartbeat";
  if (pairingKind)
    return {GateResult::ALLOWED, "", ""};

  if (!paired_)
    return {GateResult::DENIED_NOT_PAIRED, "not_paired",
            "Pair with the Windows Host before sending " + e.kind};

  if (e.kind == "system_power_request" && !power_authorized_)
    return {GateResult::DENIED_NOT_AUTHORIZED, "not_authorized",
            "Power control is not enabled for this client"};

  return {GateResult::ALLOWED, "", ""};
}

}  // namespace remotewindows::core
