
#ifndef RWH_CORE_PROTOCOL_TYPES_HPP
#define RWH_CORE_PROTOCOL_TYPES_HPP

// Typed mirror of protocol/remote_windows.proto (schema v0.1.0).
// The .proto is canonical; this header must stay field-for-field in sync.
// Only the CONTRACT role changes the schema.

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace remotewindows::protocol {

enum class Capability : uint32_t {
  UNSPECIFIED = 0, APP_CATALOG = 1, LAUNCH = 2, MULTI_WINDOW = 3,
  INPUT = 4, RESIZE = 5, POWER = 6, AUXILIARY_WINDOWS = 7,
};

enum class LaunchState : uint32_t {
  UNSPECIFIED = 0, ACCEPTED = 1, STARTING = 2, STARTED = 3,
  WINDOW_DETECTED = 4, FAILED = 5,
};

enum class AppRunState : uint32_t {
  UNSPECIFIED = 0, RUNNING = 1, CLOSED = 2, UNKNOWN = 3,
};

enum class AppType : uint32_t {
  UNSPECIFIED = 0, WIN32_SHORTCUT = 1, PACKAGED = 2, USER_ADDED = 3,
};

enum class WindowRole : uint32_t {
  UNSPECIFIED = 0, MAIN = 1, DOCUMENT = 2, DIALOG = 3, FILE_PICKER = 4,
  POPUP = 5, TOOL = 6, UNKNOWN = 7,
};

enum class WindowState : uint32_t {
  UNSPECIFIED = 0, NORMAL = 1, MINIMIZED = 2, MAXIMIZED = 3, HIDDEN = 4,
};

enum class WindowRemovalReason : uint32_t {
  UNSPECIFIED = 0, CLOSED = 1, UNAVAILABLE = 2, SOURCE_INVALIDATED = 3,
  SHARE_STOPPED = 4,
};

enum class MouseKind : uint32_t {
  UNSPECIFIED = 0, MOVE = 1, DOWN = 2, UP = 3, DOUBLE_CLICK = 4, WHEEL = 5,
};

enum class MouseButton : uint32_t {
  UNSPECIFIED = 0, LEFT = 1, RIGHT = 2, MIDDLE = 3,
};

enum class WindowActionKind : uint32_t {
  UNSPECIFIED = 0, ACTIVATE = 1, MINIMIZE = 2, RESTORE = 3,
  STOP_STREAMING = 4, REQUEST_CLOSE = 5,
};

enum class WindowActionResultState : uint32_t {
  UNSPECIFIED = 0, OK = 1, DENIED = 2, FAILED = 3,
};

enum class FrameRateReason : uint32_t {
  UNSPECIFIED = 0, FOCUS_CHANGED = 1, DISPLAY_CHANGED = 2, MINIMIZED = 3,
  RESTORED = 4, USER_CAP = 5, RESUMED = 6,
};

enum class StreamState : uint32_t {
  UNSPECIFIED = 0, IDLE = 1, RUNNING = 2, DEGRADED = 3, STOPPED = 4, ERROR = 5,
};

enum class PowerAction : uint32_t {
  UNSPECIFIED = 0, SLEEP = 1, SHUTDOWN = 2,
};

enum class PowerResultState : uint32_t {
  UNSPECIFIED = 0, ACCEPTED = 1, DENIED = 2, FAILED = 3,
};

// Enum <-> lower_snake_case wire names. from_* returns the UNSPECIFIED
// value and reports ok=false on unknown names (validation then decides).
const char* capabilityName(Capability v);
bool capabilityFromName(const std::string& s, Capability& out);
const char* launchStateName(LaunchState v);
bool launchStateFromName(const std::string& s, LaunchState& out);
const char* appRunStateName(AppRunState v);
bool appRunStateFromName(const std::string& s, AppRunState& out);
const char* appTypeName(AppType v);
bool appTypeFromName(const std::string& s, AppType& out);
const char* windowRoleName(WindowRole v);
bool windowRoleFromName(const std::string& s, WindowRole& out);
const char* windowStateName(WindowState v);
bool windowStateFromName(const std::string& s, WindowState& out);
const char* windowRemovalReasonName(WindowRemovalReason v);
bool windowRemovalReasonFromName(const std::string& s, WindowRemovalReason& out);
const char* mouseKindName(MouseKind v);
bool mouseKindFromName(const std::string& s, MouseKind& out);
const char* mouseButtonName(MouseButton v);
bool mouseButtonFromName(const std::string& s, MouseButton& out);
const char* windowActionKindName(WindowActionKind v);
bool windowActionKindFromName(const std::string& s, WindowActionKind& out);
const char* windowActionResultStateName(WindowActionResultState v);
bool windowActionResultStateFromName(const std::string& s, WindowActionResultState& out);
const char* frameRateReasonName(FrameRateReason v);
bool frameRateReasonFromName(const std::string& s, FrameRateReason& out);
const char* streamStateName(StreamState v);
bool streamStateFromName(const std::string& s, StreamState& out);
const char* powerActionName(PowerAction v);
bool powerActionFromName(const std::string& s, PowerAction& out);
const char* powerResultStateName(PowerResultState v);
bool powerResultStateFromName(const std::string& s, PowerResultState& out);

struct Hello {
  std::string product_name;   // "windows-host" | "macos-client"
  std::string app_version;
  std::vector<Capability> capabilities;
};

struct PairingChallenge {
  std::optional<std::string> one_time_code;  // 6 digits
  std::optional<std::string> host_name;      // 1..64
  std::optional<std::string> challenge_nonce; // 32 lowercase hex
};

struct PairingResult {
  bool accepted = false;
  std::string error_code;
  std::string safe_message;
};

struct AppCatalogRequest {
  bool force_rescan = false;
};

struct AppEntry {
  std::string app_id;
  std::string display_name;
  AppType type = AppType::UNSPECIFIED;
  std::string icon_ref;
  bool available = false;
};

struct AppCatalog {
  uint32_t catalog_revision = 0;
  std::vector<AppEntry> entries;
};

struct AppCatalogChanged {
  uint32_t catalog_revision = 0;
};

struct LaunchApp {
  std::string app_id;
};

struct LaunchResult {
  std::string app_id;
  LaunchState state = LaunchState::UNSPECIFIED;
  std::string error_code;
  std::string safe_message;
  std::optional<std::string> application_session_id;
};

struct AppStatus {
  std::string app_id;
  AppRunState state = AppRunState::UNSPECIFIED;
  std::string application_session_id;
};

struct WindowAnnounce {
  std::string window_id;
  std::string application_session_id;
  std::optional<std::string> parent_window_id;
  std::optional<std::string> owner_window_id;
  WindowRole role = WindowRole::UNSPECIFIED;
  bool modal = false;
  bool visible = false;
  int32_t z_order = 0;
  std::string title;
  int32_t x = 0, y = 0;
  uint32_t width_px = 0, height_px = 0;
  uint32_t dpi_scale_percent = 100;
  WindowState state = WindowState::UNSPECIFIED;
};

struct WindowUpdate {
  std::string window_id;
  std::optional<std::string> title;
  std::optional<int32_t> x, y;
  std::optional<uint32_t> width_px, height_px;
  std::optional<WindowState> state;
  std::optional<bool> modal, visible, active;
  std::optional<uint32_t> dpi_scale_percent;
};

struct WindowRemove {
  std::string window_id;
  WindowRemovalReason reason = WindowRemovalReason::UNSPECIFIED;
};

struct MouseInput {
  MouseKind kind = MouseKind::UNSPECIFIED;
  MouseButton button = MouseButton::UNSPECIFIED;
  int32_t x = 0, y = 0;
  int32_t wheel_delta = 0;
};

// WIN_MOD_* bit constants for KeyInput::modifier_bits (see .proto).
inline constexpr uint32_t kWinModShift = 0x1;
inline constexpr uint32_t kWinModControl = 0x2;
inline constexpr uint32_t kWinModAlt = 0x4;
inline constexpr uint32_t kWinModWin = 0x8;

struct KeyInput {
  uint32_t windows_vk = 0;
  uint32_t modifier_bits = 0;
  bool key_down = false;
  bool is_repeat = false;
};

struct InputEvent {
  std::string window_id;
  std::variant<MouseInput, KeyInput> input;
};

struct ResizeRequest {
  std::string window_id;
  uint32_t client_width_px = 0;
  uint32_t client_height_px = 0;
};

struct ResizeAck {
  std::string window_id;
  bool ok = false;
  uint32_t actual_width_px = 0;
  uint32_t actual_height_px = 0;
  std::string error_code;
};

struct WindowAction {
  std::string window_id;
  WindowActionKind kind = WindowActionKind::UNSPECIFIED;
};

struct WindowActionResult {
  std::string window_id;
  WindowActionKind kind = WindowActionKind::UNSPECIFIED;
  WindowActionResultState state = WindowActionResultState::UNSPECIFIED;
  std::string error_code;
  std::string safe_message;
};

struct FrameRateRequest {
  std::string window_id;
  uint32_t target_fps = 0;
  FrameRateReason reason = FrameRateReason::UNSPECIFIED;
};

struct StreamStatus {
  std::string window_id;
  StreamState state = StreamState::UNSPECIFIED;
  uint32_t width_px = 0, height_px = 0;
  double actual_fps = 0.0;
  std::string error_code;
};

struct SystemPowerRequest {
  PowerAction action = PowerAction::UNSPECIFIED;
};

struct SystemPowerResult {
  PowerAction action = PowerAction::UNSPECIFIED;
  PowerResultState state = PowerResultState::UNSPECIFIED;
  std::string error_code;
  std::string safe_message;
};

struct Heartbeat {
  int64_t sender_time_ms = 0;
};

struct PerformanceStats {
  std::string window_id;
  double fps = 0.0;
  uint32_t bitrate_kbps = 0;
  uint32_t dropped_frames = 0;
  uint64_t capture_to_encode_ms = 0;
  uint64_t encode_ms = 0;
};

using Payload =
    std::variant<std::monostate, Hello, PairingChallenge, PairingResult,
                 AppCatalogRequest, AppCatalog, AppCatalogChanged, LaunchApp,
                 LaunchResult, AppStatus, WindowAnnounce, WindowUpdate,
                 WindowRemove, InputEvent, ResizeRequest, ResizeAck,
                 WindowAction, WindowActionResult, FrameRateRequest,
                 StreamStatus, SystemPowerRequest, SystemPowerResult,
                 Heartbeat, PerformanceStats>;

inline constexpr size_t kPayloadIndexHello = 1;
inline constexpr size_t kPayloadIndexPairingChallenge = 2;
inline constexpr size_t kPayloadIndexPairingResult = 3;
inline constexpr size_t kPayloadIndexAppCatalogRequest = 4;
inline constexpr size_t kPayloadIndexAppCatalog = 5;
inline constexpr size_t kPayloadIndexAppCatalogChanged = 6;
inline constexpr size_t kPayloadIndexLaunchApp = 7;
inline constexpr size_t kPayloadIndexLaunchResult = 8;
inline constexpr size_t kPayloadIndexAppStatus = 9;
inline constexpr size_t kPayloadIndexWindowAnnounce = 10;
inline constexpr size_t kPayloadIndexWindowUpdate = 11;
inline constexpr size_t kPayloadIndexWindowRemove = 12;
inline constexpr size_t kPayloadIndexInputEvent = 13;
inline constexpr size_t kPayloadIndexResizeRequest = 14;
inline constexpr size_t kPayloadIndexResizeAck = 15;
inline constexpr size_t kPayloadIndexWindowAction = 16;
inline constexpr size_t kPayloadIndexWindowActionResult = 17;
inline constexpr size_t kPayloadIndexFrameRateRequest = 18;
inline constexpr size_t kPayloadIndexStreamStatus = 19;
inline constexpr size_t kPayloadIndexSystemPowerRequest = 20;
inline constexpr size_t kPayloadIndexSystemPowerResult = 21;
inline constexpr size_t kPayloadIndexHeartbeat = 22;
inline constexpr size_t kPayloadIndexPerformanceStats = 23;

struct Envelope {
  uint32_t schema_major = 1;
  uint32_t schema_minor = 0;
  std::string kind;
  std::optional<uint32_t> request_id;
  std::optional<uint64_t> sequence;
  Payload payload = std::monostate{};
};

// Kind metadata. category: 0 = plain, 1 = request (request_id required),
// 2 = result (request_id must be echoed), 3 = sequence-required kind.
struct KindInfo {
  const char* name;
  size_t payload_index;
  uint8_t flags;
};

enum KindFlag : uint8_t {
  K_REQUEST = 0x1,    // request_id required
  K_RESULT = 0x2,     // request_id echo required
  K_SEQUENCE = 0x4,   // sequence required
  K_INPUT = 0x8,      // input event (never reordered/dropped)
};

// All 23 kinds, index-aligned with the variant.
const KindInfo& kindInfo(const std::string& kind);
bool findKind(const std::string& kind, const KindInfo** out);

// Fixed protocol version for v0.1.0.
inline constexpr uint32_t kProtocolMajor = 1;
inline constexpr uint32_t kProtocolMinor = 0;

}  // namespace remotewindows::protocol

#endif
