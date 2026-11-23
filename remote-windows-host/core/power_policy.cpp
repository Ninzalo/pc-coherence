
#include "core/power_policy.hpp"

#include "core/pairing.hpp"

namespace remotewindows::core {

PowerController::PowerController(PowerExecutor executor)
    : executor_(std::move(executor)) {}

PowerOutcome PowerController::request(const PowerRequest& req,
                                      std::string& error_code,
                                      std::string& safe_message) {
  ++request_count_;
  if (req.action != PowerAction::SLEEP && req.action != PowerAction::SHUTDOWN) {
    error_code = "invalid_enum";
    safe_message = "Unknown power action";
    return PowerOutcome::DENIED;
  }
  if (!req.paired) {
    error_code = "not_paired";
    safe_message = "Device is not paired";
    return PowerOutcome::DENIED;
  }
  if (!req.authorized) {
    error_code = "not_authorized";
    safe_message = "Client is not authorized for power control";
    return PowerOutcome::DENIED;
  }
  if (!executor_.execute) {
    error_code = "no_executor";
    safe_message = "Power executor not configured";
    return PowerOutcome::FAILED;
  }
  if (executor_.execute(req.action, error_code)) {
    safe_message = req.action == PowerAction::SLEEP
                       ? "Windows is entering sleep"
                       : "Shutdown requested";
    return PowerOutcome::ACCEPTED;
  }
  if (safe_message.empty())
    safe_message = "Power action failed: " + error_code;
  return PowerOutcome::FAILED;
}

uint32_t PowerController::requestCount() const { return request_count_; }

}  // namespace remotewindows::core
