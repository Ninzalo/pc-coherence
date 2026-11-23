
#include "core/protocol_json.hpp"

#include <cctype>
#include <limits>

namespace remotewindows::protocol {

namespace j = nlohmann;

ParseError::ParseError(std::string code, std::string message)
    : std::runtime_error(message), code(std::move(code)) {}

namespace detail {

inline std::string reqStr(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_string())
    throw ParseError("field_type", std::string("required string field missing or wrong type: ") + k);
  return *it;
}

template <class T>
std::optional<T> optStr(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end()) return std::nullopt;
  if (!it->is_string())
    throw ParseError("field_type", std::string("optional field wrong type: ") + k);
  return *it;
}

// Window identifiers on the wire are canonical 8-4-4-4-12 hex UUIDs
// (protocol rule: every remote window_id is a UUID). Anything else is a
// contract violation rejected at decode time, not silently accepted.
inline bool isUuidFormat(const std::string& s) {
  if (s.size() != 36) return false;
  for (size_t i = 0; i < 36; ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (s[i] != '-') return false;
    } else if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
  }
  return true;
}

inline std::string reqUuid(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_string())
    throw ParseError("field_type", std::string("required string field missing or wrong type: ") + k);
  std::string s = *it;
  if (!isUuidFormat(s))
    throw ParseError("bad_uuid", std::string("not a canonical UUID: ") + k);
  return s;
}

inline std::optional<std::string> optUuid(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end()) return std::nullopt;
  if (!it->is_string())
    throw ParseError("field_type", std::string("optional field wrong type: ") + k);
  std::string s = *it;
  if (!isUuidFormat(s))
    throw ParseError("bad_uuid", std::string("not a canonical UUID: ") + k);
  return s;
}

// Window titles are bounded on the wire; oversized titles are a contract
// violation rather than a decode-time truncation case.
inline constexpr size_t kMaxWindowTitleLen = 512;

inline std::string reqTitle(const j::json& j, const char* k) {
  std::string s = reqStr(j, k);
  if (s.size() > kMaxWindowTitleLen)
    throw ParseError("title_too_long", std::string("title exceeds ") +
                                           std::to_string(kMaxWindowTitleLen) + " characters");
  return s;
}

inline std::optional<std::string> optTitle(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end()) return std::nullopt;
  if (!it->is_string())
    throw ParseError("field_type", std::string("optional field wrong type: ") + k);
  std::string s = *it;
  if (s.size() > kMaxWindowTitleLen)
    throw ParseError("title_too_long", std::string("title exceeds ") +
                                           std::to_string(kMaxWindowTitleLen) + " characters");
  return s;
}

inline bool reqBool(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_boolean())
    throw ParseError("field_type", std::string("required bool field missing or wrong type: ") + k);
  return *it;
}

template <class T>
std::optional<T> optBool(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end()) return std::nullopt;
  if (!it->is_boolean())
    throw ParseError("field_type", std::string("optional field wrong type: ") + k);
  return static_cast<T>(*it);
}

template <class T>
T reqInt(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_number_integer())
    throw ParseError("field_type", std::string("required integer field missing or wrong type: ") + k);
  if (it->is_number_unsigned() ? static_cast<unsigned long long>(*it) >
                                    std::numeric_limits<T>::max()
                               : *it < std::numeric_limits<T>::min())
    throw ParseError("int_overflow", std::string("integer out of range: ") + k);
  return static_cast<T>(*it);
}

template <class T>
std::optional<T> optInt(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end()) return std::nullopt;
  if (!it->is_number_integer())
    throw ParseError("field_type", std::string("optional integer field wrong type: ") + k);
  if (it->is_number_unsigned() ? static_cast<unsigned long long>(*it) >
                                    std::numeric_limits<T>::max()
                               : *it < std::numeric_limits<T>::min())
    throw ParseError("int_overflow", std::string("integer out of range: ") + k);
  return static_cast<T>(*it);
}

inline double reqDouble(const j::json& j, const char* k) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_number())
    throw ParseError("field_type", std::string("required number field missing or wrong type: ") + k);
  return it->get<double>();
}

// Enum fields: wire value is the lower_snake_case name string.
template <class E, class FromName>
E reqEnum(const j::json& j, const char* k, FromName fromName) {
  auto it = j.find(k);
  if (it == j.end() || !it->is_string())
    throw ParseError("field_type", std::string("required enum field missing or wrong type: ") + k);
  E out{};
  if (!fromName(it->get<std::string>(), out))
    throw ParseError("invalid_enum", std::string("unknown enum value for field: ") + k);
  return out;
}

}  // namespace detail

// ------------------------------------------------------- per-message json
using namespace detail;

inline void to_json(j::json& out, const Hello& m) {
  out["product_name"] = m.product_name;
  out["app_version"] = m.app_version;
  j::json caps = j::json::array();
  for (auto c : m.capabilities) caps.push_back(capabilityName(c));
  out["capabilities"] = std::move(caps);
}

inline void from_json(const j::json& in, Hello& m) {
  m.product_name = reqStr(in, "product_name");
  m.app_version = reqStr(in, "app_version");
  m.capabilities.clear();
  auto it = in.find("capabilities");
  if (it != in.end()) {
    if (!it->is_array())
      throw ParseError("field_type", "capabilities must be an array");
    for (const auto& e : *it) {
      if (!e.is_string()) throw ParseError("field_type", "capability entry");
      Capability c{};
      if (!capabilityFromName(e.get<std::string>(), c))
        throw ParseError("invalid_enum", "unknown capability");
      m.capabilities.push_back(c);
    }
  }
}

inline void to_json(j::json& out, const PairingChallenge& m) {
  if (m.one_time_code) out["one_time_code"] = *m.one_time_code;
  if (m.host_name) out["host_name"] = *m.host_name;
  if (m.challenge_nonce) out["challenge_nonce"] = *m.challenge_nonce;
}

inline void from_json(const j::json& in, PairingChallenge& m) {
  m.one_time_code = optStr<std::string>(in, "one_time_code");
  m.host_name = optStr<std::string>(in, "host_name");
  m.challenge_nonce = optStr<std::string>(in, "challenge_nonce");
}

inline void to_json(j::json& out, const PairingResult& m) {
  out["accepted"] = m.accepted;
  if (!m.error_code.empty()) out["error_code"] = m.error_code;
  if (!m.safe_message.empty()) out["safe_message"] = m.safe_message;
}

inline void from_json(const j::json& in, PairingResult& m) {
  m.accepted = reqBool(in, "accepted");
  m.error_code = optStr<std::string>(in, "error_code").value_or("");
  m.safe_message = optStr<std::string>(in, "safe_message").value_or("");
}

inline void to_json(j::json& out, const AppCatalogRequest& m) {
  out["force_rescan"] = m.force_rescan;
}

inline void from_json(const j::json& in, AppCatalogRequest& m) {
  m.force_rescan = reqBool(in, "force_rescan");
}

inline void to_json(j::json& out, const AppEntry& m) {
  out["app_id"] = m.app_id;
  out["display_name"] = m.display_name;
  out["type"] = appTypeName(m.type);
  if (!m.icon_ref.empty()) out["icon_ref"] = m.icon_ref;
  out["available"] = m.available;
}

inline void from_json(const j::json& in, AppEntry& m) {
  m.app_id = reqStr(in, "app_id");
  m.display_name = reqStr(in, "display_name");
  m.type = reqEnum<AppType>(in, "type", &appTypeFromName);
  m.icon_ref = optStr<std::string>(in, "icon_ref").value_or("");
  m.available = reqBool(in, "available");
}

inline void to_json(j::json& out, const AppCatalog& m) {
  out["catalog_revision"] = m.catalog_revision;
  j::json entries = j::json::array();
  for (const auto& e : m.entries) entries.push_back(e);
  out["entries"] = std::move(entries);
}

inline void from_json(const j::json& in, AppCatalog& m) {
  m.catalog_revision = reqInt<uint32_t>(in, "catalog_revision");
  auto it = in.find("entries");
  if (it == in.end() || !it->is_array())
    throw ParseError("field_type", "entries must be an array");
  m.entries.clear();
  for (const auto& e : *it) m.entries.push_back(e.get<AppEntry>());
}

inline void to_json(j::json& out, const AppCatalogChanged& m) {
  out["catalog_revision"] = m.catalog_revision;
}

inline void from_json(const j::json& in, AppCatalogChanged& m) {
  m.catalog_revision = reqInt<uint32_t>(in, "catalog_revision");
}

inline void to_json(j::json& out, const LaunchApp& m) {
  out["app_id"] = m.app_id;
}

inline void from_json(const j::json& in, LaunchApp& m) {
  m.app_id = reqStr(in, "app_id");
}

inline void to_json(j::json& out, const LaunchResult& m) {
  out["app_id"] = m.app_id;
  out["state"] = launchStateName(m.state);
  if (!m.error_code.empty()) out["error_code"] = m.error_code;
  if (!m.safe_message.empty()) out["safe_message"] = m.safe_message;
  if (m.application_session_id)
    out["application_session_id"] = *m.application_session_id;
}

inline void from_json(const j::json& in, LaunchResult& m) {
  m.app_id = reqStr(in, "app_id");
  m.state = reqEnum<LaunchState>(in, "state", &launchStateFromName);
  m.error_code = optStr<std::string>(in, "error_code").value_or("");
  m.safe_message = optStr<std::string>(in, "safe_message").value_or("");
  m.application_session_id = optStr<std::string>(in, "application_session_id");
}

inline void to_json(j::json& out, const AppStatus& m) {
  out["app_id"] = m.app_id;
  out["state"] = appRunStateName(m.state);
  if (!m.application_session_id.empty())
    out["application_session_id"] = m.application_session_id;
}

inline void from_json(const j::json& in, AppStatus& m) {
  m.app_id = reqStr(in, "app_id");
  m.state = reqEnum<AppRunState>(in, "state", &appRunStateFromName);
  m.application_session_id = optStr<std::string>(in, "application_session_id").value_or("");
}

inline void to_json(j::json& out, const WindowAnnounce& m) {
  out["window_id"] = m.window_id;
  out["application_session_id"] = m.application_session_id;
  if (m.parent_window_id) out["parent_window_id"] = *m.parent_window_id;
  if (m.owner_window_id) out["owner_window_id"] = *m.owner_window_id;
  out["role"] = windowRoleName(m.role);
  out["modal"] = m.modal;
  out["visible"] = m.visible;
  out["z_order"] = m.z_order;
  out["title"] = m.title;
  out["x"] = m.x;
  out["y"] = m.y;
  out["width_px"] = m.width_px;
  out["height_px"] = m.height_px;
  out["dpi_scale_percent"] = m.dpi_scale_percent;
  out["state"] = windowStateName(m.state);
}

inline void from_json(const j::json& in, WindowAnnounce& m) {
  m.window_id = reqUuid(in, "window_id");
  m.application_session_id = reqStr(in, "application_session_id");
  m.parent_window_id = optUuid(in, "parent_window_id");
  m.owner_window_id = optUuid(in, "owner_window_id");
  m.role = reqEnum<WindowRole>(in, "role", &windowRoleFromName);
  m.modal = reqBool(in, "modal");
  m.visible = reqBool(in, "visible");
  m.z_order = reqInt<int32_t>(in, "z_order");
  m.title = reqTitle(in, "title");
  m.x = reqInt<int32_t>(in, "x");
  m.y = reqInt<int32_t>(in, "y");
  m.width_px = reqInt<uint32_t>(in, "width_px");
  m.height_px = reqInt<uint32_t>(in, "height_px");
  m.dpi_scale_percent = reqInt<uint32_t>(in, "dpi_scale_percent");
  m.state = reqEnum<WindowState>(in, "state", &windowStateFromName);
}

inline void to_json(j::json& out, const WindowUpdate& m) {
  out["window_id"] = m.window_id;
  if (m.title) out["title"] = *m.title;
  if (m.x) out["x"] = *m.x;
  if (m.y) out["y"] = *m.y;
  if (m.width_px) out["width_px"] = *m.width_px;
  if (m.height_px) out["height_px"] = *m.height_px;
  if (m.state) out["state"] = windowStateName(*m.state);
  if (m.modal) out["modal"] = *m.modal;
  if (m.visible) out["visible"] = *m.visible;
  if (m.active) out["active"] = *m.active;
  if (m.dpi_scale_percent) out["dpi_scale_percent"] = *m.dpi_scale_percent;
}

inline void from_json(const j::json& in, WindowUpdate& m) {
  m.window_id = reqUuid(in, "window_id");
  m.title = optTitle(in, "title");
  m.x = optInt<int32_t>(in, "x");
  m.y = optInt<int32_t>(in, "y");
  m.width_px = optInt<uint32_t>(in, "width_px");
  m.height_px = optInt<uint32_t>(in, "height_px");
  auto sit = in.find("state");
  if (sit != in.end()) {
    WindowState s{};
    if (!sit->is_string() || !windowStateFromName(sit->get<std::string>(), s))
      throw ParseError("invalid_enum", "window_update.state");
    m.state = s;
  }
  m.modal = optBool<bool>(in, "modal");
  m.visible = optBool<bool>(in, "visible");
  m.active = optBool<bool>(in, "active");
  m.dpi_scale_percent = optInt<uint32_t>(in, "dpi_scale_percent");
}

inline void to_json(j::json& out, const WindowRemove& m) {
  out["window_id"] = m.window_id;
  out["reason"] = windowRemovalReasonName(m.reason);
}

inline void from_json(const j::json& in, WindowRemove& m) {
  m.window_id = reqUuid(in, "window_id");
  m.reason = reqEnum<WindowRemovalReason>(in, "reason", &windowRemovalReasonFromName);
}

inline void to_json(j::json& out, const MouseInput& m) {
  out["kind"] = mouseKindName(m.kind);
  if (m.button != MouseButton::UNSPECIFIED)
    out["button"] = mouseButtonName(m.button);
  out["x"] = m.x;
  out["y"] = m.y;
  out["wheel_delta"] = m.wheel_delta;
}

inline void from_json(const j::json& in, MouseInput& m) {
  m.kind = reqEnum<MouseKind>(in, "kind", &mouseKindFromName);
  auto bit = in.find("button");
  if (bit != in.end()) {
    if (!bit->is_string() || !mouseButtonFromName(bit->get<std::string>(), m.button))
      throw ParseError("invalid_enum", "mouse.button");
  }
  m.x = reqInt<int32_t>(in, "x");
  m.y = reqInt<int32_t>(in, "y");
  m.wheel_delta = optInt<int32_t>(in, "wheel_delta").value_or(0);
}

inline void to_json(j::json& out, const KeyInput& m) {
  out["windows_vk"] = m.windows_vk;
  out["modifier_bits"] = m.modifier_bits;
  out["key_down"] = m.key_down;
  out["is_repeat"] = m.is_repeat;
}

inline void from_json(const j::json& in, KeyInput& m) {
  m.windows_vk = reqInt<uint32_t>(in, "windows_vk");
  m.modifier_bits = reqInt<uint32_t>(in, "modifier_bits");
  m.key_down = reqBool(in, "key_down");
  // is_repeat is proto field 4 (proto3 bool): optional on the wire,
  // defaults to false when omitted.
  m.is_repeat = optBool<bool>(in, "is_repeat").value_or(false);
}

inline void to_json(j::json& out, const InputEvent& m) {
  out["window_id"] = m.window_id;
  std::visit(
      [&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, MouseInput>) out["mouse"] = v;
        if constexpr (std::is_same_v<T, KeyInput>) out["key"] = v;
      },
      m.input);
}

inline void from_json(const j::json& in, InputEvent& m) {
  m.window_id = reqUuid(in, "window_id");
  bool has_mouse = in.contains("mouse");
  bool has_key = in.contains("key");
  if (has_mouse == has_key)
    throw ParseError("bad_oneof", "input_event must carry exactly one of mouse/key");
  if (has_mouse) m.input = in.at("mouse").get<MouseInput>();
  else m.input = in.at("key").get<KeyInput>();
}

inline void to_json(j::json& out, const ResizeRequest& m) {
  out["window_id"] = m.window_id;
  out["client_width_px"] = m.client_width_px;
  out["client_height_px"] = m.client_height_px;
}

inline void from_json(const j::json& in, ResizeRequest& m) {
  m.window_id = reqUuid(in, "window_id");
  m.client_width_px = reqInt<uint32_t>(in, "client_width_px");
  m.client_height_px = reqInt<uint32_t>(in, "client_height_px");
}

inline void to_json(j::json& out, const ResizeAck& m) {
  out["window_id"] = m.window_id;
  out["ok"] = m.ok;
  out["actual_width_px"] = m.actual_width_px;
  out["actual_height_px"] = m.actual_height_px;
  if (!m.error_code.empty()) out["error_code"] = m.error_code;
}

inline void from_json(const j::json& in, ResizeAck& m) {
  m.window_id = reqUuid(in, "window_id");
  m.ok = reqBool(in, "ok");
  m.actual_width_px = reqInt<uint32_t>(in, "actual_width_px");
  m.actual_height_px = reqInt<uint32_t>(in, "actual_height_px");
  m.error_code = optStr<std::string>(in, "error_code").value_or("");
}

inline void to_json(j::json& out, const WindowAction& m) {
  out["window_id"] = m.window_id;
  out["kind"] = windowActionKindName(m.kind);
}

inline void from_json(const j::json& in, WindowAction& m) {
  m.window_id = reqUuid(in, "window_id");
  m.kind = reqEnum<WindowActionKind>(in, "kind", &windowActionKindFromName);
}

inline void to_json(j::json& out, const WindowActionResult& m) {
  out["window_id"] = m.window_id;
  out["kind"] = windowActionKindName(m.kind);
  out["state"] = windowActionResultStateName(m.state);
  if (!m.error_code.empty()) out["error_code"] = m.error_code;
  if (!m.safe_message.empty()) out["safe_message"] = m.safe_message;
}

inline void from_json(const j::json& in, WindowActionResult& m) {
  m.window_id = reqUuid(in, "window_id");
  m.kind = reqEnum<WindowActionKind>(in, "kind", &windowActionKindFromName);
  m.state = reqEnum<WindowActionResultState>(in, "state", &windowActionResultStateFromName);
  m.error_code = optStr<std::string>(in, "error_code").value_or("");
  m.safe_message = optStr<std::string>(in, "safe_message").value_or("");
}

inline void to_json(j::json& out, const FrameRateRequest& m) {
  out["window_id"] = m.window_id;
  out["target_fps"] = m.target_fps;
  out["reason"] = frameRateReasonName(m.reason);
}

inline void from_json(const j::json& in, FrameRateRequest& m) {
  m.window_id = reqUuid(in, "window_id");
  m.target_fps = reqInt<uint32_t>(in, "target_fps");
  m.reason = reqEnum<FrameRateReason>(in, "reason", &frameRateReasonFromName);
}

inline void to_json(j::json& out, const StreamStatus& m) {
  out["window_id"] = m.window_id;
  out["state"] = streamStateName(m.state);
  out["width_px"] = m.width_px;
  out["height_px"] = m.height_px;
  out["actual_fps"] = m.actual_fps;
  if (!m.error_code.empty()) out["error_code"] = m.error_code;
}

inline void from_json(const j::json& in, StreamStatus& m) {
  m.window_id = reqUuid(in, "window_id");
  m.state = reqEnum<StreamState>(in, "state", &streamStateFromName);
  m.width_px = reqInt<uint32_t>(in, "width_px");
  m.height_px = reqInt<uint32_t>(in, "height_px");
  m.actual_fps = reqDouble(in, "actual_fps");
  m.error_code = optStr<std::string>(in, "error_code").value_or("");
}

inline void to_json(j::json& out, const SystemPowerRequest& m) {
  out["action"] = powerActionName(m.action);
}

inline void from_json(const j::json& in, SystemPowerRequest& m) {
  m.action = reqEnum<PowerAction>(in, "action", &powerActionFromName);
}

inline void to_json(j::json& out, const SystemPowerResult& m) {
  out["action"] = powerActionName(m.action);
  out["state"] = powerResultStateName(m.state);
  if (!m.error_code.empty()) out["error_code"] = m.error_code;
  if (!m.safe_message.empty()) out["safe_message"] = m.safe_message;
}

inline void from_json(const j::json& in, SystemPowerResult& m) {
  m.action = reqEnum<PowerAction>(in, "action", &powerActionFromName);
  m.state = reqEnum<PowerResultState>(in, "state", &powerResultStateFromName);
  m.error_code = optStr<std::string>(in, "error_code").value_or("");
  m.safe_message = optStr<std::string>(in, "safe_message").value_or("");
}

inline void to_json(j::json& out, const Heartbeat& m) {
  out["sender_time_ms"] = m.sender_time_ms;
}

inline void from_json(const j::json& in, Heartbeat& m) {
  m.sender_time_ms = reqInt<int64_t>(in, "sender_time_ms");
}

inline void to_json(j::json& out, const PerformanceStats& m) {
  out["window_id"] = m.window_id;
  out["fps"] = m.fps;
  out["bitrate_kbps"] = m.bitrate_kbps;
  out["dropped_frames"] = m.dropped_frames;
  out["capture_to_encode_ms"] = m.capture_to_encode_ms;
  out["encode_ms"] = m.encode_ms;
}

inline void from_json(const j::json& in, PerformanceStats& m) {
  m.window_id = reqUuid(in, "window_id");
  m.fps = reqDouble(in, "fps");
  m.bitrate_kbps = reqInt<uint32_t>(in, "bitrate_kbps");
  m.dropped_frames = reqInt<uint32_t>(in, "dropped_frames");
  m.capture_to_encode_ms = reqInt<uint64_t>(in, "capture_to_encode_ms");
  m.encode_ms = reqInt<uint64_t>(in, "encode_ms");
}

// ------------------------------------------------------------- envelope
j::json toEnvelopeJson(const Envelope& e) {
  j::json out;
  out["schema_major"] = e.schema_major;
  out["schema_minor"] = e.schema_minor;
  out["kind"] = e.kind;
  if (e.request_id) out["request_id"] = *e.request_id;
  if (e.sequence) out["sequence"] = *e.sequence;
  j::json payload = j::json::object();
  std::visit(
      [&](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (!std::is_same_v<T, std::monostate>) payload = p;
      },
      e.payload);
  out["payload"] = std::move(payload);
  return out;
}

std::string encodeEnvelope(const Envelope& e) {
  return toEnvelopeJson(e).dump();
}

bool encodeEnvelopeSafe(const Envelope& e, std::string& out) {
  try {
    out = encodeEnvelope(e);
    return true;
  } catch (const ParseError&) {
    return false;
  }
}

Envelope fromEnvelopeJson(const j::json& root) {
  if (!root.is_object())
    throw ParseError("malformed_json", "envelope must be a JSON object");
  Envelope e;
  e.schema_major = reqInt<uint32_t>(root, "schema_major");
  e.schema_minor = reqInt<uint32_t>(root, "schema_minor");
  e.kind = reqStr(root, "kind");
  e.request_id = optInt<uint32_t>(root, "request_id");
  e.sequence = optInt<uint64_t>(root, "sequence");

  auto pit = root.find("payload");
  if (pit == root.end() || !pit->is_object())
    throw ParseError("field_type", "payload must be a JSON object");

  const KindInfo* info = nullptr;
  if (!findKind(e.kind, &info))
    throw ParseError("unknown_kind", "unknown envelope kind: " + e.kind);
  const j::json& payload = *pit;
  switch (info->payload_index) {
    case kPayloadIndexHello: e.payload = payload.get<Hello>(); break;
    case kPayloadIndexPairingChallenge: e.payload = payload.get<PairingChallenge>(); break;
    case kPayloadIndexPairingResult: e.payload = payload.get<PairingResult>(); break;
    case kPayloadIndexAppCatalogRequest: e.payload = payload.get<AppCatalogRequest>(); break;
    case kPayloadIndexAppCatalog: e.payload = payload.get<AppCatalog>(); break;
    case kPayloadIndexAppCatalogChanged: e.payload = payload.get<AppCatalogChanged>(); break;
    case kPayloadIndexLaunchApp: e.payload = payload.get<LaunchApp>(); break;
    case kPayloadIndexLaunchResult: e.payload = payload.get<LaunchResult>(); break;
    case kPayloadIndexAppStatus: e.payload = payload.get<AppStatus>(); break;
    case kPayloadIndexWindowAnnounce: e.payload = payload.get<WindowAnnounce>(); break;
    case kPayloadIndexWindowUpdate: e.payload = payload.get<WindowUpdate>(); break;
    case kPayloadIndexWindowRemove: e.payload = payload.get<WindowRemove>(); break;
    case kPayloadIndexInputEvent: e.payload = payload.get<InputEvent>(); break;
    case kPayloadIndexResizeRequest: e.payload = payload.get<ResizeRequest>(); break;
    case kPayloadIndexResizeAck: e.payload = payload.get<ResizeAck>(); break;
    case kPayloadIndexWindowAction: e.payload = payload.get<WindowAction>(); break;
    case kPayloadIndexWindowActionResult: e.payload = payload.get<WindowActionResult>(); break;
    case kPayloadIndexFrameRateRequest: e.payload = payload.get<FrameRateRequest>(); break;
    case kPayloadIndexStreamStatus: e.payload = payload.get<StreamStatus>(); break;
    case kPayloadIndexSystemPowerRequest: e.payload = payload.get<SystemPowerRequest>(); break;
    case kPayloadIndexSystemPowerResult: e.payload = payload.get<SystemPowerResult>(); break;
    case kPayloadIndexHeartbeat: e.payload = payload.get<Heartbeat>(); break;
    case kPayloadIndexPerformanceStats: e.payload = payload.get<PerformanceStats>(); break;
    default:
      throw ParseError("unknown_kind", "unmapped kind index: " + e.kind);
  }
  return e;
}

bool decodeEnvelope(const std::string& jsonText, Envelope& out,
                    std::string& err_code) {
  if (jsonText.size() > 8u * 1024u * 1024u) {
    err_code = "frame_too_large";
    return false;
  }
  try {
    j::json parsed = j::json::parse(jsonText, nullptr, false);
    if (parsed.is_discarded()) {
      err_code = "malformed_json";
      return false;
    }
    out = fromEnvelopeJson(parsed);
    return true;
  } catch (const ParseError& pe) {
    err_code = pe.code;
    return false;
  } catch (const j::json::exception&) {
    err_code = "malformed_json";
    return false;
  }
}

}  // namespace remotewindows::protocol
