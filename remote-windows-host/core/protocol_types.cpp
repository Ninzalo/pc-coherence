
#include "core/protocol_types.hpp"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace remotewindows::protocol {

// ------------------------------------------------------------------ enums
#define RWH_ENUM_NAMES(ENUM, FN, FROMFN, ...)                                     \
  namespace {                                                                     \
  static const std::vector<const char*> FN##_table = {__VA_ARGS__};               \
  }                                                                               \
  const char* FN(ENUM v) {                                                        \
    return FN##_table[size_t(v)];                                                 \
  }                                                                               \
  bool FROMFN(const std::string& s, ENUM& out) {                                  \
    for (size_t i = 0; i < FN##_table.size(); ++i)                                \
      if (s == FN##_table[i]) {                                                   \
        out = ENUM(i);                                                            \
        return true;                                                              \
      }                                                                           \
    return false;                                                                 \
  }

RWH_ENUM_NAMES(Capability, capabilityName, capabilityFromName,
               "unspecified", "app_catalog", "launch", "multi_window",
               "input", "resize", "power", "auxiliary_windows")
RWH_ENUM_NAMES(LaunchState, launchStateName, launchStateFromName,
               "unspecified", "accepted", "starting", "started",
               "window_detected", "failed")
RWH_ENUM_NAMES(AppRunState, appRunStateName, appRunStateFromName,
               "unspecified", "running", "closed", "unknown")
RWH_ENUM_NAMES(AppType, appTypeName, appTypeFromName, "unspecified",
               "win32_shortcut", "packaged", "user_added")
RWH_ENUM_NAMES(WindowRole, windowRoleName, windowRoleFromName,
               "unspecified", "main", "document", "dialog", "file_picker",
               "popup", "tool", "unknown")
RWH_ENUM_NAMES(WindowState, windowStateName, windowStateFromName,
               "unspecified", "normal", "minimized", "maximized", "hidden")
RWH_ENUM_NAMES(WindowRemovalReason, windowRemovalReasonName,
               windowRemovalReasonFromName, "unspecified", "closed",
               "unavailable", "source_invalidated", "share_stopped")
RWH_ENUM_NAMES(MouseKind, mouseKindName, mouseKindFromName, "unspecified",
               "move", "down", "up", "double_click", "wheel")
RWH_ENUM_NAMES(MouseButton, mouseButtonName, mouseButtonFromName,
               "unspecified", "left", "right", "middle")
RWH_ENUM_NAMES(WindowActionKind, windowActionKindName,
               windowActionKindFromName, "unspecified", "activate",
               "minimize", "restore", "stop_streaming", "request_close")
RWH_ENUM_NAMES(WindowActionResultState, windowActionResultStateName,
               windowActionResultStateFromName, "unspecified", "ok", "denied",
               "failed")
RWH_ENUM_NAMES(FrameRateReason, frameRateReasonName, frameRateReasonFromName,
               "unspecified", "focus_changed", "display_changed", "minimized",
               "restored", "user_cap", "resumed")
RWH_ENUM_NAMES(StreamState, streamStateName, streamStateFromName,
               "unspecified", "idle", "running", "degraded", "stopped",
               "error")
RWH_ENUM_NAMES(PowerAction, powerActionName, powerActionFromName,
               "unspecified", "sleep", "shutdown")
RWH_ENUM_NAMES(PowerResultState, powerResultStateName,
               powerResultStateFromName, "unspecified", "accepted", "denied",
               "failed")

// ------------------------------------------------------------------- kinds
namespace {
const std::array<KindInfo, 24>& kindTable() {
  static const std::array<KindInfo, 24> t = {
      {{"", 0, 0},
       {"hello", kPayloadIndexHello, 0},
       {"pairing_challenge", kPayloadIndexPairingChallenge, 0},
       {"pairing_result", kPayloadIndexPairingResult, 0},
       {"app_catalog_request", kPayloadIndexAppCatalogRequest, K_REQUEST},
       {"app_catalog", kPayloadIndexAppCatalog, K_RESULT},
       {"app_catalog_changed", kPayloadIndexAppCatalogChanged, 0},
       {"launch_app", kPayloadIndexLaunchApp, K_REQUEST},
       {"launch_result", kPayloadIndexLaunchResult, K_RESULT},
       {"app_status", kPayloadIndexAppStatus, 0},
       {"window_announce", kPayloadIndexWindowAnnounce, 0},
       {"window_update", kPayloadIndexWindowUpdate, 0},
       {"window_remove", kPayloadIndexWindowRemove, 0},
       {"input_event", kPayloadIndexInputEvent, K_SEQUENCE | K_INPUT},
       {"resize_request", kPayloadIndexResizeRequest,
        K_REQUEST | K_SEQUENCE},
       {"resize_ack", kPayloadIndexResizeAck, K_RESULT},
       {"window_action", kPayloadIndexWindowAction, K_REQUEST | K_SEQUENCE},
       {"window_action_result", kPayloadIndexWindowActionResult, K_RESULT},
       // FPS adjustments are fire-and-forget control commands (feedback
       // arrives via stream_status/performance_stats), so no request_id.
       {"frame_rate_request", kPayloadIndexFrameRateRequest, 0},
       {"stream_status", kPayloadIndexStreamStatus, 0},
       {"system_power_request", kPayloadIndexSystemPowerRequest,
        K_REQUEST | K_SEQUENCE},
       {"system_power_result", kPayloadIndexSystemPowerResult, K_RESULT},
       {"heartbeat", kPayloadIndexHeartbeat, 0},
       {"performance_stats", kPayloadIndexPerformanceStats, 0}}};
  return t;
}

const std::unordered_map<std::string, size_t>& kindIndex() {
  static const std::unordered_map<std::string, size_t> m = [] {
    std::unordered_map<std::string, size_t> mm;
    const auto& t = kindTable();
    for (size_t i = 1; i < t.size(); ++i) mm[t[i].name] = i;
    return mm;
  }();
  return m;
}
}  // namespace

const KindInfo& kindInfo(const std::string& kind) {
  const auto& m = kindIndex();
  auto it = m.find(kind);
  if (it == m.end()) {
    static const KindInfo unknown = {kind.c_str(), 0, 0};
    return unknown;
  }
  return kindTable()[it->second];
}

bool findKind(const std::string& kind, const KindInfo** out) {
  const auto& m = kindIndex();
  auto it = m.find(kind);
  if (it == m.end()) return false;
  *out = &kindTable()[it->second];
  return true;
}

}  // namespace remotewindows::protocol
