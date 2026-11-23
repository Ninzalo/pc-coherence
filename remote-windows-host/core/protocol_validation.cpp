
#include "core/protocol_validation.hpp"

#include <cctype>
#include <cstdint>

namespace remotewindows::protocol {

int32_t utf8Length(const std::string& s) {
  int32_t count = 0;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char c = s[i];
    int extra;
    if (c < 0x80) extra = 0;
    else if ((c & 0xe0) == 0xc0) extra = 1;
    else if ((c & 0xf0) == 0xe0) extra = 2;
    else if ((c & 0xf8) == 0xf0) extra = 3;
    else return -1;
    if (i + (size_t)extra >= s.size() + 1 && (size_t)extra + 1 > s.size() - i)
      return -1;
    for (int k = 1; k <= extra; ++k) {
      if (i + (size_t)k >= s.size() || (s[i + (size_t)k] & 0xc0) != 0x80)
        return -1;
    }
    i += 1 + (size_t)extra;
    ++count;
  }
  return count;
}

bool isUuid(const std::string& s) {
  if (s.size() != 36) return false;
  for (int i = 0; i < 36; ++i) {
    char c = s[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') return false;
    } else if (!std::isxdigit((unsigned char)c)) {
      return false;
    }
  }
  return true;
}

bool isAppId(const std::string& s) {
  if (s.empty() || s.size() > 128) return false;
  if (!std::isalnum((unsigned char)s[0])) return false;
  for (char c : s)
    if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') return false;
  return true;
}

bool isSixDigitCode(const std::string& s) {
  if (s.size() != 6) return false;
  for (char c : s)
    if (!std::isdigit((unsigned char)c)) return false;
  return true;
}

bool isHex32(const std::string& s) {
  if (s.size() != 32) return false;
  for (char c : s)
    if (!std::isxdigit((unsigned char)c)) return false;
  return true;
}

bool validAppName(const std::string& s) {
  if (s.empty() || utf8Length(s) > 64) return false;
  for (unsigned char c : s)
    if (c < 0x20 || c == 0x7f) return false;
  return true;
}

namespace {

inline ValidationResult fail(const char* code, const std::string& detail = "") {
  return ValidationResult{false, code, detail};
}

constexpr int32_t kMaxDim = 38400;
constexpr uint32_t kMaxDpi = 400;
constexpr uint32_t kMaxFps = 240;
constexpr int32_t kMaxTitle = 512;
constexpr int32_t kMaxSafeMsg = 256;
constexpr uint32_t kMaxCaps = 16;

bool safeMsgOk(const std::string& s) { return utf8Length(s) <= kMaxSafeMsg; }

}  // namespace

ValidationResult validate(const Envelope& e) {
  if (e.schema_major != kProtocolMajor)
    return fail("schema_major_must_be_1",
                "schema_major must equal " + std::to_string(kProtocolMajor));

  const KindInfo* info = nullptr;
  if (!findKind(e.kind, &info)) return fail("unknown_kind", e.kind);

  // request_id / sequence policy (PROTOCOL §4)
  if ((info->flags & (K_REQUEST | K_RESULT)) && !e.request_id)
    return fail("missing_request_id", e.kind);
  if ((info->flags & K_SEQUENCE) && !e.sequence)
    return fail("missing_sequence", e.kind);

  auto ok = ValidationResult{true, {}, {}};

  switch (info->payload_index) {
    case kPayloadIndexHello: {
      const auto& m = std::get<Hello>(e.payload);
      if (m.product_name != "windows-host" && m.product_name != "macos-client")
        return fail("invalid_product_name", m.product_name);
      if (m.app_version.empty() || m.app_version.size() > 64)
        return fail("invalid_app_version", m.app_version);
      if (m.capabilities.size() > kMaxCaps)
        return fail("list_too_long", "capabilities");
      for (auto c : m.capabilities)
        if (c == Capability::UNSPECIFIED)
          return fail("invalid_enum", "capability unspecified");
      break;
    }
    case kPayloadIndexPairingChallenge: {
      const auto& m = std::get<PairingChallenge>(e.payload);
      if (!m.one_time_code && !m.host_name && !m.challenge_nonce)
        return fail("empty_payload", "pairing_challenge");
      if (m.one_time_code && !isSixDigitCode(*m.one_time_code))
        return fail("invalid_pairing_code", *m.one_time_code);
      if (m.host_name && !validAppName(*m.host_name))
        return fail("invalid_app_name", *m.host_name);
      if (m.challenge_nonce && !isHex32(*m.challenge_nonce))
        return fail("invalid_nonce", *m.challenge_nonce);
      break;
    }
    case kPayloadIndexPairingResult: {
      const auto& m = std::get<PairingResult>(e.payload);
      if (!m.error_code.empty() && m.error_code.size() > 64)
        return fail("invalid_error_code", m.error_code);
      if (!m.safe_message.empty() && !safeMsgOk(m.safe_message))
        return fail("invalid_safe_message", m.safe_message);
      break;
    }
    case kPayloadIndexLaunchApp: {
      const auto& m = std::get<LaunchApp>(e.payload);
      if (!isAppId(m.app_id)) return fail("invalid_app_id", m.app_id);
      break;
    }
    case kPayloadIndexLaunchResult: {
      const auto& m = std::get<LaunchResult>(e.payload);
      if (!isAppId(m.app_id)) return fail("invalid_app_id", m.app_id);
      if (m.state == LaunchState::UNSPECIFIED)
        return fail("invalid_enum", "launch state");
      if (m.application_session_id &&
          (m.application_session_id->empty() ||
           m.application_session_id->size() > 64))
        return fail("invalid_session_id", *m.application_session_id);
      if (!m.error_code.empty() && m.error_code.size() > 64)
        return fail("invalid_error_code", m.error_code);
      if (!m.safe_message.empty() && !safeMsgOk(m.safe_message))
        return fail("invalid_safe_message", m.safe_message);
      break;
    }
    case kPayloadIndexAppStatus: {
      const auto& m = std::get<AppStatus>(e.payload);
      if (!isAppId(m.app_id)) return fail("invalid_app_id", m.app_id);
      if (m.state == AppRunState::UNSPECIFIED)
        return fail("invalid_enum", "app state");
      break;
    }
    case kPayloadIndexAppCatalog: {
      const auto& m = std::get<AppCatalog>(e.payload);
      for (const auto& entry : m.entries)
        if (!isAppId(entry.app_id) || !isAppId(entry.app_id))
          return fail("invalid_app_id", entry.app_id);
      break;
    }
    case kPayloadIndexAppCatalogChanged:
      break;
    case kPayloadIndexWindowAnnounce: {
      const auto& m = std::get<WindowAnnounce>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (m.application_session_id.empty() ||
          m.application_session_id.size() > 64)
        return fail("invalid_session_id", m.application_session_id);
      if (m.role == WindowRole::UNSPECIFIED)
        return fail("invalid_enum", "window role");
      if (m.state == WindowState::UNSPECIFIED)
        return fail("invalid_enum", "window state");
      if (m.width_px == 0 || m.height_px == 0 || m.width_px > (uint32_t)kMaxDim ||
          m.height_px > (uint32_t)kMaxDim)
        return fail("invalid_dimensions",
                    std::to_string(m.width_px) + "x" + std::to_string(m.height_px));
      if (m.dpi_scale_percent < 25 || m.dpi_scale_percent > kMaxDpi)
        return fail("invalid_dpi", std::to_string(m.dpi_scale_percent));
      if (utf8Length(m.title) > kMaxTitle)
        return fail("title_too_long", m.title);
      if (m.parent_window_id && !isUuid(*m.parent_window_id))
        return fail("invalid_uuid", *m.parent_window_id);
      if (m.owner_window_id && !isUuid(*m.owner_window_id))
        return fail("invalid_uuid", *m.owner_window_id);
      break;
    }
    case kPayloadIndexWindowUpdate: {
      const auto& m = std::get<WindowUpdate>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      bool hasField = m.title || m.x || m.y || m.width_px || m.height_px ||
                      m.state || m.modal || m.visible || m.active ||
                      m.dpi_scale_percent;
      if (!hasField)
        return fail("empty_payload", "window_update needs window_id plus one change");
      if ((m.width_px && (*m.width_px == 0 || *m.width_px > (uint32_t)kMaxDim)) ||
          (m.height_px && (*m.height_px == 0 || *m.height_px > (uint32_t)kMaxDim)))
        return fail("invalid_dimensions", e.kind);
      if (m.dpi_scale_percent &&
          (*m.dpi_scale_percent < 25 || *m.dpi_scale_percent > kMaxDpi))
        return fail("invalid_dpi", std::to_string(*m.dpi_scale_percent));
      if (m.title && utf8Length(*m.title) > kMaxTitle)
        return fail("title_too_long", *m.title);
      break;
    }
    case kPayloadIndexWindowRemove: {
      const auto& m = std::get<WindowRemove>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (m.reason == WindowRemovalReason::UNSPECIFIED)
        return fail("invalid_enum", "removal reason");
      break;
    }
    case kPayloadIndexInputEvent: {
      const auto& m = std::get<InputEvent>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (std::holds_alternative<MouseInput>(m.input)) {
        const auto& mi = std::get<MouseInput>(m.input);
        if (mi.kind == MouseKind::UNSPECIFIED)
          return fail("invalid_enum", "mouse kind");
      } else {
        const auto& ki = std::get<KeyInput>(m.input);
        if (ki.windows_vk < 1 || ki.windows_vk > 255)
          return fail("vk_out_of_range", std::to_string(ki.windows_vk));
        if (ki.modifier_bits > 0xF)
          return fail("invalid_modifiers", std::to_string(ki.modifier_bits));
      }
      break;
    }
    case kPayloadIndexResizeRequest: {
      const auto& m = std::get<ResizeRequest>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (m.client_width_px == 0 || m.client_height_px == 0 ||
          m.client_width_px > (uint32_t)kMaxDim ||
          m.client_height_px > (uint32_t)kMaxDim)
        return fail("invalid_dimensions", e.kind);
      break;
    }
    case kPayloadIndexResizeAck: {
      const auto& m = std::get<ResizeAck>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      break;
    }
    case kPayloadIndexWindowAction: {
      const auto& m = std::get<WindowAction>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (m.kind == WindowActionKind::UNSPECIFIED)
        return fail("invalid_enum", "action kind");
      break;
    }
    case kPayloadIndexWindowActionResult: {
      const auto& m = std::get<WindowActionResult>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (m.state == WindowActionResultState::UNSPECIFIED)
        return fail("invalid_enum", "action result state");
      break;
    }
    case kPayloadIndexFrameRateRequest: {
      const auto& m = std::get<FrameRateRequest>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      if (m.target_fps > kMaxFps)
        return fail("fps_out_of_range", std::to_string(m.target_fps));
      if (m.reason == FrameRateReason::UNSPECIFIED)
        return fail("invalid_enum", "fps reason");
      break;
    }
    case kPayloadIndexStreamStatus: {
      const auto& m = std::get<StreamStatus>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      break;
    }
    case kPayloadIndexSystemPowerRequest: {
      const auto& m = std::get<SystemPowerRequest>(e.payload);
      // powerActionFromName already rejects anything but sleep/shutdown;
      // unspecified is structurally impossible (wire name required).
      if (m.action != PowerAction::SLEEP && m.action != PowerAction::SHUTDOWN)
        return fail("invalid_enum", "power action");
      break;
    }
    case kPayloadIndexSystemPowerResult: {
      const auto& m = std::get<SystemPowerResult>(e.payload);
      if (m.action != PowerAction::SLEEP && m.action != PowerAction::SHUTDOWN)
        return fail("invalid_enum", "power action");
      if (m.state == PowerResultState::UNSPECIFIED)
        return fail("invalid_enum", "power result state");
      break;
    }
    case kPayloadIndexHeartbeat:
      break;
    case kPayloadIndexPerformanceStats: {
      const auto& m = std::get<PerformanceStats>(e.payload);
      if (!isUuid(m.window_id)) return fail("invalid_uuid", m.window_id);
      break;
    }
    default:
      break;
  }
  return ok;
}

}  // namespace remotewindows::protocol
