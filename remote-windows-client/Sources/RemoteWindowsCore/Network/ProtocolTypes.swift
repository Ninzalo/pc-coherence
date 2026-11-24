import Foundation

// Protocol types for Remote Windows control protocol v0.1.0.
//
// Field semantics mirror the canonical schema
// (`Protocol/Contract/remote_windows.proto`, Host-owned) and the Host's
// portable core (`remote-windows-host/core/protocol_types.hpp`). Wire
// format: length-prefixed JSON (Host ADR-0002); enum values are
// lower_snake_case strings; absent fields are omitted.

// MARK: - Kinds

/// Envelope `kind` strings (lower_snake_case of the payload type).
public enum RWKind: String, CaseIterable, Equatable, Sendable {
    case hello
    case pairingChallenge = "pairing_challenge"
    case pairingResult = "pairing_result"
    case appCatalogRequest = "app_catalog_request"
    case appCatalog = "app_catalog"
    case appCatalogChanged = "app_catalog_changed"
    case launchApp = "launch_app"
    case launchResult = "launch_result"
    case appStatus = "app_status"
    case windowAnnounce = "window_announce"
    case windowUpdate = "window_update"
    case windowRemove = "window_remove"
    case inputEvent = "input_event"
    case resizeRequest = "resize_request"
    case resizeAck = "resize_ack"
    case windowAction = "window_action"
    case windowActionResult = "window_action_result"
    case frameRateRequest = "frame_rate_request"
    case streamStatus = "stream_status"
    case systemPowerRequest = "system_power_request"
    case systemPowerResult = "system_power_result"
    case heartbeat
    case performanceStats = "performance_stats"

    /// Flags per PROTOCOL.md §3: request_id required / request_id echo
    /// required / sequence required.
    public var requiresRequestID: Bool {
        switch self {
        case .appCatalogRequest, .launchApp, .resizeRequest, .windowAction,
             .frameRateRequest, .systemPowerRequest,
             .appCatalog, .launchResult, .resizeAck, .windowActionResult,
             .systemPowerResult:
            return true
        default:
            return false
        }
    }

    public var requiresSequence: Bool {
        switch self {
        case .inputEvent, .resizeRequest, .windowAction, .systemPowerRequest:
            return true
        default:
            return false
        }
    }
}

// MARK: - Enums (wire names are lower_snake_case of the value name)

public enum RWCapability: String, CaseIterable, Equatable, Sendable {
    case appCatalog = "app_catalog"
    case launch = "launch"
    case multiWindow = "multi_window"
    case input = "input"
    case resize = "resize"
    case power = "power"
    case auxiliaryWindows = "auxiliary_windows"
    /// Decoded only for rejection: the wire never carries `unspecified`.
    case unspecified = "unspecified"
}

public enum RWLaunchState: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case accepted = "accepted"
    case starting = "starting"
    case started = "started"
    case windowDetected = "window_detected"
    case failed = "failed"
}

public enum RWAppRunState: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case running = "running"
    case closed = "closed"
    case unknown = "unknown"
}

public enum RWAppType: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case win32Shortcut = "win32_shortcut"
    case packaged = "packaged"
    case userAdded = "user_added"
}

public enum RWWindowRole: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case main = "main"
    case document = "document"
    case dialog = "dialog"
    case filePicker = "file_picker"
    case popup = "popup"
    case tool = "tool"
    case unknown = "unknown"
}

public enum RWWindowState: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case normal = "normal"
    case minimized = "minimized"
    case maximized = "maximized"
    case hidden = "hidden"
}

public enum RWWindowRemovalReason: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case closed = "closed"
    case unavailable = "unavailable"
    case sourceInvalidated = "source_invalidated"
    case shareStopped = "share_stopped"
}

public enum RWMouseKind: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case move = "move"
    case down = "down"
    case up = "up"
    case doubleClick = "double_click"
    case wheel = "wheel"
}

public enum RWMouseButton: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case left = "left"
    case right = "right"
    case middle = "middle"
}

public enum RWWindowActionKind: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case activate = "activate"
    case minimize = "minimize"
    case restore = "restore"
    case stopStreaming = "stop_streaming"
    case requestClose = "request_close"
}

public enum RWWindowActionResultState: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case ok = "ok"
    case denied = "denied"
    case failed = "failed"
}

public enum RWFrameRateReason: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case focusChanged = "focus_changed"
    case displayChanged = "display_changed"
    case minimized = "minimized"
    case restored = "restored"
    case userCap = "user_cap"
    case resumed = "resumed"
}

public enum RWStreamState: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case idle = "idle"
    case running = "running"
    case degraded = "degraded"
    case stopped = "stopped"
    case error = "error"
}

/// Only `sleep` and `shutdown` are legal on the wire (FR-11/12, PROTOCOL §9).
public enum RWPowerAction: String, CaseIterable, Equatable, Sendable {
    case sleep = "sleep"
    case shutdown = "shutdown"
}

public enum RWPowerResultState: String, CaseIterable, Equatable, Sendable {
    case unspecified = "unspecified"
    case accepted = "accepted"
    case denied = "denied"
    case failed = "failed"
}

// MARK: - Payloads

public struct Hello: Equatable {
    public var productName: String
    public var appVersion: String
    public var capabilities: [RWCapability]
    public init(productName: String, appVersion: String, capabilities: [RWCapability] = []) {
        self.productName = productName
        self.appVersion = appVersion
        self.capabilities = capabilities
    }
}

public struct PairingChallenge: Equatable {
    public var oneTimeCode: String?
    public var hostName: String?
    public var challengeNonce: String?
    public init(oneTimeCode: String? = nil, hostName: String? = nil,
                challengeNonce: String? = nil) {
        self.oneTimeCode = oneTimeCode
        self.hostName = hostName
        self.challengeNonce = challengeNonce
    }
}

public struct PairingResult: Equatable {
    public var accepted: Bool
    public var errorCode: String
    public var safeMessage: String
    public init(accepted: Bool, errorCode: String = "", safeMessage: String = "") {
        self.accepted = accepted
        self.errorCode = errorCode
        self.safeMessage = safeMessage
    }
}

public struct AppCatalogRequest: Equatable {
    public var forceRescan: Bool
    public init(forceRescan: Bool = false) { self.forceRescan = forceRescan }
}

public struct AppEntry: Equatable {
    public var appID: String
    public var displayName: String
    public var type: RWAppType
    public var iconRef: String
    public var available: Bool
    public init(appID: String, displayName: String, type: RWAppType,
                iconRef: String = "", available: Bool = true) {
        self.appID = appID
        self.displayName = displayName
        self.type = type
        self.iconRef = iconRef
        self.available = available
    }
}

public struct AppCatalog: Equatable {
    public var catalogRevision: UInt32
    public var entries: [AppEntry]
    public init(catalogRevision: UInt32, entries: [AppEntry] = []) {
        self.catalogRevision = catalogRevision
        self.entries = entries
    }
}

public struct AppCatalogChanged: Equatable {
    public var catalogRevision: UInt32
    public init(catalogRevision: UInt32) { self.catalogRevision = catalogRevision }
}

public struct LaunchApp: Equatable {
    public var appID: String
    public init(appID: String) { self.appID = appID }
}

public struct LaunchResult: Equatable {
    public var appID: String
    public var state: RWLaunchState
    public var errorCode: String
    public var safeMessage: String
    public var applicationSessionID: String?
    public init(appID: String, state: RWLaunchState, errorCode: String = "",
                safeMessage: String = "", applicationSessionID: String? = nil) {
        self.appID = appID
        self.state = state
        self.errorCode = errorCode
        self.safeMessage = safeMessage
        self.applicationSessionID = applicationSessionID
    }
}

public struct AppStatus: Equatable {
    public var appID: String
    public var state: RWAppRunState
    public var applicationSessionID: String
    public init(appID: String, state: RWAppRunState,
                applicationSessionID: String = "") {
        self.appID = appID
        self.state = state
        self.applicationSessionID = applicationSessionID
    }
}

public struct WindowAnnounce: Equatable {
    public var windowID: String
    public var applicationSessionID: String
    public var parentWindowID: String?
    public var ownerWindowID: String?
    public var role: RWWindowRole
    public var modal: Bool
    public var visible: Bool
    public var zOrder: Int32
    public var title: String
    public var x: Int32
    public var y: Int32
    public var widthPx: UInt32
    public var heightPx: UInt32
    public var dpiScalePercent: UInt32
    public var state: RWWindowState
    public init(windowID: String, applicationSessionID: String,
                parentWindowID: String? = nil, ownerWindowID: String? = nil,
                role: RWWindowRole, modal: Bool = false, visible: Bool = true,
                zOrder: Int32 = 0, title: String = "", x: Int32 = 0, y: Int32 = 0,
                widthPx: UInt32, heightPx: UInt32, dpiScalePercent: UInt32 = 100,
                state: RWWindowState = .normal) {
        self.windowID = windowID
        self.applicationSessionID = applicationSessionID
        self.parentWindowID = parentWindowID
        self.ownerWindowID = ownerWindowID
        self.role = role
        self.modal = modal
        self.visible = visible
        self.zOrder = zOrder
        self.title = title
        self.x = x
        self.y = y
        self.widthPx = widthPx
        self.heightPx = heightPx
        self.dpiScalePercent = dpiScalePercent
        self.state = state
    }
}

public struct WindowUpdate: Equatable {
    public var windowID: String
    public var title: String?
    public var x: Int32?
    public var y: Int32?
    public var widthPx: UInt32?
    public var heightPx: UInt32?
    public var state: RWWindowState?
    public var modal: Bool?
    public var visible: Bool?
    public var active: Bool?
    public var dpiScalePercent: UInt32?
    public init(windowID: String) { self.windowID = windowID }
}

public struct WindowRemove: Equatable {
    public var windowID: String
    public var reason: RWWindowRemovalReason
    public init(windowID: String, reason: RWWindowRemovalReason) {
        self.windowID = windowID
        self.reason = reason
    }
}

public struct MouseInput: Equatable {
    public var kind: RWMouseKind
    public var button: RWMouseButton
    public var x: Int32
    public var y: Int32
    public var wheelDelta: Int32
    public init(kind: RWMouseKind, button: RWMouseButton = .unspecified,
                x: Int32 = 0, y: Int32 = 0, wheelDelta: Int32 = 0) {
        self.kind = kind
        self.button = button
        self.x = x
        self.y = y
        self.wheelDelta = wheelDelta
    }
}

public struct KeyInput: Equatable {
    /// Windows virtual-key code, 1...0xFF.
    public var windowsVk: UInt32
    /// Bitmask: 0x1 shift, 0x2 control, 0x4 alt, 0x8 win.
    public var modifierBits: UInt32
    public var keyDown: Bool
    public var isRepeat: Bool
    public init(windowsVk: UInt32, modifierBits: UInt32 = 0,
                keyDown: Bool, isRepeat: Bool = false) {
        self.windowsVk = windowsVk
        self.modifierBits = modifierBits
        self.keyDown = keyDown
        self.isRepeat = isRepeat
    }
}

public enum RWInput: Equatable {
    case mouse(MouseInput)
    case key(KeyInput)
}

public struct InputEvent: Equatable {
    public var windowID: String
    public var input: RWInput
    public init(windowID: String, input: RWInput) {
        self.windowID = windowID
        self.input = input
    }
}

public struct ResizeRequest: Equatable {
    public var windowID: String
    public var clientWidthPx: UInt32
    public var clientHeightPx: UInt32
    public init(windowID: String, clientWidthPx: UInt32, clientHeightPx: UInt32) {
        self.windowID = windowID
        self.clientWidthPx = clientWidthPx
        self.clientHeightPx = clientHeightPx
    }
}

public struct ResizeAck: Equatable {
    public var windowID: String
    public var ok: Bool
    public var actualWidthPx: UInt32
    public var actualHeightPx: UInt32
    public var errorCode: String
    public init(windowID: String, ok: Bool, actualWidthPx: UInt32 = 0,
                actualHeightPx: UInt32 = 0, errorCode: String = "") {
        self.windowID = windowID
        self.ok = ok
        self.actualWidthPx = actualWidthPx
        self.actualHeightPx = actualHeightPx
        self.errorCode = errorCode
    }
}

public struct WindowAction: Equatable {
    public var windowID: String
    public var kind: RWWindowActionKind
    public init(windowID: String, kind: RWWindowActionKind) {
        self.windowID = windowID
        self.kind = kind
    }
}

public struct WindowActionResult: Equatable {
    public var windowID: String
    public var kind: RWWindowActionKind
    public var state: RWWindowActionResultState
    public var errorCode: String
    public var safeMessage: String
    public init(windowID: String, kind: RWWindowActionKind,
                state: RWWindowActionResultState, errorCode: String = "",
                safeMessage: String = "") {
        self.windowID = windowID
        self.kind = kind
        self.state = state
        self.errorCode = errorCode
        self.safeMessage = safeMessage
    }
}

public struct FrameRateRequest: Equatable {
    public var windowID: String
    /// 0 = paused; range 0...240.
    public var targetFps: UInt32
    public var reason: RWFrameRateReason
    public init(windowID: String, targetFps: UInt32, reason: RWFrameRateReason) {
        self.windowID = windowID
        self.targetFps = targetFps
        self.reason = reason
    }
}

public struct StreamStatus: Equatable {
    public var windowID: String
    public var state: RWStreamState
    public var widthPx: UInt32
    public var heightPx: UInt32
    public var actualFps: Double
    public var errorCode: String
    public init(windowID: String, state: RWStreamState, widthPx: UInt32 = 0,
                heightPx: UInt32 = 0, actualFps: Double = 0, errorCode: String = "") {
        self.windowID = windowID
        self.state = state
        self.widthPx = widthPx
        self.heightPx = heightPx
        self.actualFps = actualFps
        self.errorCode = errorCode
    }
}

public struct SystemPowerRequest: Equatable {
    public var action: RWPowerAction
    public init(action: RWPowerAction) { self.action = action }
}

public struct SystemPowerResult: Equatable {
    public var action: RWPowerAction
    public var state: RWPowerResultState
    public var errorCode: String
    public var safeMessage: String
    public init(action: RWPowerAction, state: RWPowerResultState,
                errorCode: String = "", safeMessage: String = "") {
        self.action = action
        self.state = state
        self.errorCode = errorCode
        self.safeMessage = safeMessage
    }
}

public struct Heartbeat: Equatable {
    public var senderTimeMs: Int64
    public init(senderTimeMs: Int64) { self.senderTimeMs = senderTimeMs }
}

public struct PerformanceStats: Equatable {
    public var windowID: String
    public var fps: Double
    public var bitrateKbps: UInt32
    public var droppedFrames: UInt32
    public var captureToEncodeMs: UInt64
    public var encodeMs: UInt64
    public init(windowID: String, fps: Double, bitrateKbps: UInt32 = 0,
                droppedFrames: UInt32 = 0, captureToEncodeMs: UInt64 = 0,
                encodeMs: UInt64 = 0) {
        self.windowID = windowID
        self.fps = fps
        self.bitrateKbps = bitrateKbps
        self.droppedFrames = droppedFrames
        self.captureToEncodeMs = captureToEncodeMs
        self.encodeMs = encodeMs
    }
}

// MARK: - Envelope

public enum RWPayload: Equatable {
    case hello(Hello)
    case pairingChallenge(PairingChallenge)
    case pairingResult(PairingResult)
    case appCatalogRequest(AppCatalogRequest)
    case appCatalog(AppCatalog)
    case appCatalogChanged(AppCatalogChanged)
    case launchApp(LaunchApp)
    case launchResult(LaunchResult)
    case appStatus(AppStatus)
    case windowAnnounce(WindowAnnounce)
    case windowUpdate(WindowUpdate)
    case windowRemove(WindowRemove)
    case inputEvent(InputEvent)
    case resizeRequest(ResizeRequest)
    case resizeAck(ResizeAck)
    case windowAction(WindowAction)
    case windowActionResult(WindowActionResult)
    case frameRateRequest(FrameRateRequest)
    case streamStatus(StreamStatus)
    case systemPowerRequest(SystemPowerRequest)
    case systemPowerResult(SystemPowerResult)
    case heartbeat(Heartbeat)
    case performanceStats(PerformanceStats)
}

/// The wire envelope (proto `Envelope`).
public struct Envelope: Equatable {
    /// 1 for all v0.x.
    public var schemaMajor: UInt32
    public var schemaMinor: UInt32
    public var kind: RWKind
    public var requestID: UInt32?
    public var sequence: UInt64?
    public var payload: RWPayload

    public init(schemaMajor: UInt32 = 1, schemaMinor: UInt32 = 0,
                kind: RWKind, requestID: UInt32? = nil, sequence: UInt64? = nil,
                payload: RWPayload) {
        self.schemaMajor = schemaMajor
        self.schemaMinor = schemaMinor
        self.kind = kind
        self.requestID = requestID
        self.sequence = sequence
        self.payload = payload
    }
}
