
#ifndef RWH_CORE_POWER_POLICY_HPP
#define RWH_CORE_POWER_POLICY_HPP

// FR-11/FR-12 / Stage 7: typed power commands only. The state machine is
// portable and mock-testable; the actual SetSuspendState /
// InitiateSystemShutdownExW calls live in win32/SystemPowerController
// and are injected through this interface.

#include <cstdint>
#include <functional>
#include <string>

namespace remotewindows::core {

enum class PowerAction { SLEEP = 1, SHUTDOWN = 2 };

enum class PowerOutcome {
  ACCEPTED = 1,   // action will proceed (sleep/shutdown are async)
  DENIED = 2,     // not paired / not authorized / unknown enum
  FAILED = 3,     // OS call failed; error_code explains
};

struct PowerRequest {
  PowerAction action = PowerAction::SLEEP;
  bool paired = false;
  bool authorized = false;
};

// Injectable OS layer so unit tests never touch the real machine.
struct PowerExecutor {
  // Returns true on API acceptance. error_code: e.g. "privilege_denied",
  // "api_failed".
  std::function<bool(PowerAction, std::string& error_code)> execute;
};

class PowerController {
 public:
  explicit PowerController(PowerExecutor executor);
  // Validates pairing/authorization and the enum, then dispatches.
  // NEVER retries: a timeout upstream must report unknown, not resend
  // (PROTOCOL §5.2).
  PowerOutcome request(const PowerRequest& req, std::string& error_code,
                       std::string& safe_message);
  uint32_t requestCount() const;
 private:
  PowerExecutor executor_;
  uint32_t request_count_ = 0;
};

}  // namespace remotewindows::core

#endif
