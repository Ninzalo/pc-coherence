import Foundation

/// JSON codec for protocol v0.1.0 (wire = `Envelope` as UTF-8 JSON,
/// Host ADR-0002). Mirrors the Host codec
/// (`remote-windows-host/core/protocol_json.cpp`):
/// - message field names are exactly the proto field names;
/// - enum values are lower_snake_case strings;
/// - absent fields are omitted; unknown fields are ignored;
/// - decode-time enforcement: UUID format (`bad_uuid`) and `title`
///   length (`title_too_long`), before any routing.
///
/// Error codes: `malformed_json`, `unknown_kind`, `field_type`,
/// `int_overflow`, `invalid_enum`, `bad_uuid`, `title_too_long`,
/// `bad_oneof` (structural) — matching the Host.

public struct CodecError: Error, Equatable {
    public let code: String
    public let message: String
    public init(_ code: String, _ message: String) {
        self.code = code
        self.message = message
    }
}

// MARK: - Decode helpers (strict, error-code bearing)

private final class J {
    let obj: [String: Any]
    init(_ o: [String: Any]) { self.obj = o }

    func string(_ k: String) throws -> String {
        guard let v = obj[k] as? String else {
            throw CodecError("field_type", "required string field missing or wrong type: \(k)")
        }
        return v
    }

    func optString(_ k: String) throws -> String? {
        guard let v = obj[k] else { return nil }
        guard let s = v as? String else {
            throw CodecError("field_type", "optional field wrong type: \(k)")
        }
        return s
    }

    /// Required UUID string (decode-time `bad_uuid`, mirroring Host).
    func uuid(_ k: String) throws -> String {
        let v = try string(k)
        try ProtocolValidation.requireUuid(v, field: k)
        return v
    }

    /// Optional UUID string.
    func optUuid(_ k: String) throws -> String? {
        guard let v = try optString(k) else { return nil }
        try ProtocolValidation.requireUuid(v, field: k)
        return v
    }

    /// Required title string (decode-time length bound, mirroring Host).
    func title(_ k: String) throws -> String {
        let v = try string(k)
        try ProtocolValidation.requireTitle(v, field: k)
        return v
    }

    func optTitle(_ k: String) throws -> String? {
        guard let v = try optString(k) else { return nil }
        try ProtocolValidation.requireTitle(v, field: k)
        return v
    }

    func bool(_ k: String) throws -> Bool {
        guard let v = obj[k] as? Bool else {
            throw CodecError("field_type", "required bool field missing or wrong type: \(k)")
        }
        return v
    }

    func optBool(_ k: String) throws -> Bool? {
        guard let v = obj[k] else { return nil }
        guard let b = v as? Bool else {
            throw CodecError("field_type", "optional field wrong type: \(k)")
        }
        return b
    }

    func int(_ k: String, in range: ClosedRange<Int64>) throws -> Int64 {
        guard let n = J.integer(obj[k]) else {
            throw CodecError("field_type", "required integer field missing or wrong type: \(k)")
        }
        guard range.contains(n) else {
            throw CodecError("int_overflow", "integer out of range: \(k)")
        }
        return n
    }

    func optInt(_ k: String, in range: ClosedRange<Int64>) throws -> Int64? {
        guard let raw = obj[k] else { return nil }
        guard let n = J.integer(raw) else {
            throw CodecError("field_type", "optional integer field wrong type: \(k)")
        }
        guard range.contains(n) else {
            throw CodecError("int_overflow", "integer out of range: \(k)")
        }
        return n
    }

    func double(_ k: String) throws -> Double {
        guard let v = obj[k] as? NSNumber else {
            throw CodecError("field_type", "required number field missing or wrong type: \(k)")
        }
        // Reject JSON integers for number fields? Host accepts any number.
        return v.doubleValue
    }

    func optDouble(_ k: String) throws -> Double? {
        guard let v = obj[k] as? NSNumber else { return nil }
        return v.doubleValue
    }

    func enumValue<T: RawRepresentable>(_ k: String, type: T.Type) throws -> T
    where T.RawValue == String {
        guard let s = obj[k] as? String else {
            throw CodecError("field_type", "required enum field missing or wrong type: \(k)")
        }
        guard let e = T(rawValue: s) else {
            throw CodecError("invalid_enum", "unknown enum value for field: \(k)")
        }
        return e
    }

    func optEnum<T: RawRepresentable>(_ k: String, type: T.Type) throws -> T?
    where T.RawValue == String {
        guard let s = obj[k] as? String else { return nil }
        guard let e = T(rawValue: s) else {
            throw CodecError("invalid_enum", "unknown enum value for field: \(k)")
        }
        return e
    }

    /// A JSON number with an integral value (JSONSerialization represents
    /// integers as NSNumber; a fractional value is the wrong type for an
    /// integer field).
    static func integer(_ v: Any?) -> Int64? {
        guard let n = v as? NSNumber else { return nil }
        let d = n.doubleValue
        guard d == d.rounded(), d >= Double(Int64.min), d <= Double(Int64.max) else {
            return nil
        }
        return Int64(truncating: n)
    }
}

// MARK: - Payload decode

public enum ProtocolCodec {
    static let i32: ClosedRange<Int64> = Int64(Int32.min)...Int64(Int32.max)
    static let u32: ClosedRange<Int64> = 0...Int64(UInt32.max)
    static let i64: ClosedRange<Int64> = Int64.min...Int64.max

    // MARK: Envelope

    public static func decodeEnvelope(_ data: Data) throws -> Envelope {
        let opts: JSONSerialization.ReadingOptions = [.fragmentsAllowed]
        guard let obj = try? JSONSerialization.jsonObject(with: data, options: opts),
              let top = obj as? [String: Any] else {
            throw CodecError("malformed_json", "envelope must be a JSON object")
        }
        let j = J(top)
        let major = try j.int("schema_major", in: u32)
        let minor = try j.int("schema_minor", in: u32)
        let kindString = try j.string("kind")
        guard let kind = RWKind(rawValue: kindString) else {
            throw CodecError("unknown_kind", "unknown envelope kind: \(kindString)")
        }
        let requestID = try j.optInt("request_id", in: u32).map { UInt32(truncatingIfNeeded: $0) }
        let sequence = try j.optInt("sequence", in: Int64.min...Int64.max)
            .flatMap { $0 >= 0 ? $0 : nil }
            .map { UInt64(truncatingIfNeeded: $0) }
        guard let payloadObj = top["payload"] as? [String: Any] else {
            throw CodecError("field_type", "payload must be a JSON object")
        }
        let payload = try decodePayload(kind: kind, obj: payloadObj)
        return Envelope(schemaMajor: UInt32(truncatingIfNeeded: major), schemaMinor: UInt32(truncatingIfNeeded: minor),
                        kind: kind, requestID: requestID, sequence: sequence,
                        payload: payload)
    }

    private static func decodePayload(kind: RWKind, obj: [String: Any]) throws -> RWPayload {
        let j = J(obj)
        switch kind {
        case .hello:
            var caps: [RWCapability] = []
            if let arr = obj["capabilities"] as? [Any] {
                for item in arr {
                    guard let s = item as? String else {
                        throw CodecError("field_type", "capability entry")
                    }
                    guard let c = RWCapability(rawValue: s) else {
                        throw CodecError("invalid_enum", "unknown capability")
                    }
                    caps.append(c)
                }
            }
            return .hello(Hello(productName: try j.string("product_name"),
                                appVersion: try j.string("app_version"),
                                capabilities: caps))
        case .pairingChallenge:
            return .pairingChallenge(PairingChallenge(
                oneTimeCode: try j.optString("one_time_code"),
                hostName: try j.optString("host_name"),
                challengeNonce: try j.optString("challenge_nonce")))
        case .pairingResult:
            return .pairingResult(PairingResult(
                accepted: try j.bool("accepted"),
                errorCode: try j.optString("error_code") ?? "",
                safeMessage: try j.optString("safe_message") ?? ""))
        case .appCatalogRequest:
            return .appCatalogRequest(AppCatalogRequest(forceRescan: try j.bool("force_rescan")))
        case .appCatalog:
            var entries: [AppEntry] = []
            if let arr = obj["entries"] as? [Any] {
                for item in arr {
                    guard let eo = item as? [String: Any] else {
                        throw CodecError("field_type", "entries entry must be an object")
                    }
                    let ej = J(eo)
                    entries.append(AppEntry(
                        appID: try ej.string("app_id"),
                        displayName: try ej.string("display_name"),
                        type: try ej.enumValue("type", type: RWAppType.self),
                        iconRef: try ej.optString("icon_ref") ?? "",
                        available: try ej.bool("available")))
                }
            }
            return .appCatalog(AppCatalog(
                catalogRevision: UInt32(truncatingIfNeeded: try j.int("catalog_revision", in: u32)),
                entries: entries))
        case .appCatalogChanged:
            return .appCatalogChanged(AppCatalogChanged(
                catalogRevision: UInt32(truncatingIfNeeded: try j.int("catalog_revision", in: u32))))
        case .launchApp:
            return .launchApp(LaunchApp(appID: try j.string("app_id")))
        case .launchResult:
            return .launchResult(LaunchResult(
                appID: try j.string("app_id"),
                state: try j.enumValue("state", type: RWLaunchState.self),
                errorCode: try j.optString("error_code") ?? "",
                safeMessage: try j.optString("safe_message") ?? "",
                applicationSessionID: try j.optString("application_session_id")))
        case .appStatus:
            return .appStatus(AppStatus(
                appID: try j.string("app_id"),
                state: try j.enumValue("state", type: RWAppRunState.self),
                applicationSessionID: try j.optString("application_session_id") ?? ""))
        case .windowAnnounce:
            return .windowAnnounce(WindowAnnounce(
                windowID: try j.uuid("window_id"),
                applicationSessionID: try j.string("application_session_id"),
                parentWindowID: try j.optUuid("parent_window_id"),
                ownerWindowID: try j.optUuid("owner_window_id"),
                role: try j.enumValue("role", type: RWWindowRole.self),
                modal: try j.optBool("modal") ?? false,
                visible: try j.bool("visible"),
                zOrder: Int32(truncatingIfNeeded: try j.optInt("z_order", in: i32) ?? 0),
                title: try j.title("title"),
                x: Int32(truncatingIfNeeded: try j.optInt("x", in: i32) ?? 0),
                y: Int32(truncatingIfNeeded: try j.optInt("y", in: i32) ?? 0),
                widthPx: UInt32(truncatingIfNeeded: try j.int("width_px", in: u32)),
                heightPx: UInt32(truncatingIfNeeded: try j.int("height_px", in: u32)),
                dpiScalePercent: UInt32(truncatingIfNeeded: try j.optInt("dpi_scale_percent", in: u32) ?? 100),
                state: try j.optEnum("state", type: RWWindowState.self) ?? .normal))
        case .windowUpdate:
            var u = WindowUpdate(windowID: try j.uuid("window_id"))
            u.title = try j.optTitle("title")
            u.x = try j.optInt("x", in: i32).map { Int32(truncatingIfNeeded: $0) }
            u.y = try j.optInt("y", in: i32).map { Int32(truncatingIfNeeded: $0) }
            u.widthPx = try j.optInt("width_px", in: u32).map { UInt32(truncatingIfNeeded: $0) }
            u.heightPx = try j.optInt("height_px", in: u32).map { UInt32(truncatingIfNeeded: $0) }
            u.state = try j.optEnum("state", type: RWWindowState.self)
            u.modal = try j.optBool("modal")
            u.visible = try j.optBool("visible")
            u.active = try j.optBool("active")
            u.dpiScalePercent = try j.optInt("dpi_scale_percent", in: u32).map { UInt32(truncatingIfNeeded: $0) }
            return .windowUpdate(u)
        case .windowRemove:
            return .windowRemove(WindowRemove(
                windowID: try j.uuid("window_id"),
                reason: try j.enumValue("reason", type: RWWindowRemovalReason.self)))
        case .inputEvent:
            let windowID = try j.uuid("window_id")
            let hasMouse = (obj["mouse"] as? [String: Any]) != nil
            let hasKey = (obj["key"] as? [String: Any]) != nil
            if hasMouse == hasKey {
                throw CodecError("bad_oneof", "input_event must carry exactly one of mouse/key")
            }
            if hasMouse, let mo = obj["mouse"] as? [String: Any] {
                let mj = J(mo)
                return .inputEvent(InputEvent(windowID: windowID, input: .mouse(MouseInput(
                    kind: try mj.enumValue("kind", type: RWMouseKind.self),
                    button: try mj.optEnum("button", type: RWMouseButton.self) ?? .unspecified,
                    x: Int32(truncatingIfNeeded: try mj.optInt("x", in: i32) ?? 0),
                    y: Int32(truncatingIfNeeded: try mj.optInt("y", in: i32) ?? 0),
                    wheelDelta: Int32(truncatingIfNeeded: try mj.optInt("wheel_delta", in: i32) ?? 0)))))
            }
            if hasKey, let ko = obj["key"] as? [String: Any] {
                let kj = J(ko)
                return .inputEvent(InputEvent(windowID: windowID, input: .key(KeyInput(
                    windowsVk: UInt32(truncatingIfNeeded: try kj.int("windows_vk", in: u32)),
                    modifierBits: UInt32(truncatingIfNeeded: try kj.optInt("modifier_bits", in: u32) ?? 0),
                    keyDown: try kj.bool("key_down"),
                    isRepeat: try kj.optBool("is_repeat") ?? false))))
            }
            throw CodecError("bad_oneof", "input_event must carry exactly one of mouse/key")
        case .resizeRequest:
            return .resizeRequest(ResizeRequest(
                windowID: try j.uuid("window_id"),
                clientWidthPx: UInt32(truncatingIfNeeded: try j.int("client_width_px", in: u32)),
                clientHeightPx: UInt32(truncatingIfNeeded: try j.int("client_height_px", in: u32))))
        case .resizeAck:
            return .resizeAck(ResizeAck(
                windowID: try j.uuid("window_id"),
                ok: try j.bool("ok"),
                actualWidthPx: UInt32(truncatingIfNeeded: try j.optInt("actual_width_px", in: u32) ?? 0),
                actualHeightPx: UInt32(truncatingIfNeeded: try j.optInt("actual_height_px", in: u32) ?? 0),
                errorCode: try j.optString("error_code") ?? ""))
        case .windowAction:
            return .windowAction(WindowAction(
                windowID: try j.uuid("window_id"),
                kind: try j.enumValue("kind", type: RWWindowActionKind.self)))
        case .windowActionResult:
            return .windowActionResult(WindowActionResult(
                windowID: try j.uuid("window_id"),
                kind: try j.enumValue("kind", type: RWWindowActionKind.self),
                state: try j.enumValue("state", type: RWWindowActionResultState.self),
                errorCode: try j.optString("error_code") ?? "",
                safeMessage: try j.optString("safe_message") ?? ""))
        case .frameRateRequest:
            return .frameRateRequest(FrameRateRequest(
                windowID: try j.uuid("window_id"),
                targetFps: UInt32(truncatingIfNeeded: try j.int("target_fps", in: u32)),
                reason: try j.enumValue("reason", type: RWFrameRateReason.self)))
        case .streamStatus:
            return .streamStatus(StreamStatus(
                windowID: try j.uuid("window_id"),
                state: try j.enumValue("state", type: RWStreamState.self),
                widthPx: UInt32(truncatingIfNeeded: try j.optInt("width_px", in: u32) ?? 0),
                heightPx: UInt32(truncatingIfNeeded: try j.optInt("height_px", in: u32) ?? 0),
                actualFps: try j.optDouble("actual_fps") ?? 0,
                errorCode: try j.optString("error_code") ?? ""))
        case .systemPowerRequest:
            // `action` must be sleep|shutdown: the raw-value set excludes
            // everything else, so an unknown string is `invalid_enum`.
            guard let s = obj["action"] as? String else {
                throw CodecError("field_type", "required enum field missing or wrong type: action")
            }
            guard let a = RWPowerAction(rawValue: s) else {
                throw CodecError("invalid_enum", "unknown enum value for field: action")
            }
            return .systemPowerRequest(SystemPowerRequest(action: a))
        case .systemPowerResult:
            guard let s = obj["action"] as? String, let a = RWPowerAction(rawValue: s) else {
                throw CodecError("invalid_enum", "unknown enum value for field: action")
            }
            return .systemPowerResult(SystemPowerResult(
                action: a,
                state: try j.enumValue("state", type: RWPowerResultState.self),
                errorCode: try j.optString("error_code") ?? "",
                safeMessage: try j.optString("safe_message") ?? ""))
        case .heartbeat:
            return .heartbeat(Heartbeat(senderTimeMs: try j.int("sender_time_ms", in: i64)))
        case .performanceStats:
            return .performanceStats(PerformanceStats(
                windowID: try j.uuid("window_id"),
                fps: try j.optDouble("fps") ?? 0,
                bitrateKbps: UInt32(truncatingIfNeeded: try j.optInt("bitrate_kbps", in: u32) ?? 0),
                droppedFrames: UInt32(truncatingIfNeeded: try j.optInt("dropped_frames", in: u32) ?? 0),
                captureToEncodeMs: UInt64(truncatingIfNeeded: try j.optInt("capture_to_encode_ms", in: 0...Int64.max) ?? 0),
                encodeMs: UInt64(truncatingIfNeeded: try j.optInt("encode_ms", in: 0...Int64.max) ?? 0)))
        }
    }

    // MARK: Encode

    public static func encode(_ e: Envelope) throws -> Data {
        var obj: [String: Any] = [
            "schema_major": e.schemaMajor,
            "schema_minor": e.schemaMinor,
            "kind": e.kind.rawValue,
            "payload": try encodePayload(e.payload),
        ]
        if let r = e.requestID { obj["request_id"] = r }
        if let s = e.sequence { obj["sequence"] = s }
        let opts: JSONSerialization.WritingOptions = [.withoutEscapingSlashes]
        return try JSONSerialization.data(withJSONObject: obj, options: opts)
    }

    private static func encodePayload(_ p: RWPayload) throws -> [String: Any] {
        switch p {
        case .hello(let m):
            var o: [String: Any] = ["product_name": m.productName,
                                    "app_version": m.appVersion]
            if !m.capabilities.isEmpty {
                o["capabilities"] = m.capabilities.map { $0.rawValue }
            }
            return o
        case .pairingChallenge(let m):
            var o: [String: Any] = [:]
            if let v = m.oneTimeCode { o["one_time_code"] = v }
            if let v = m.hostName { o["host_name"] = v }
            if let v = m.challengeNonce { o["challenge_nonce"] = v }
            return o
        case .pairingResult(let m):
            var o: [String: Any] = ["accepted": m.accepted]
            if !m.errorCode.isEmpty { o["error_code"] = m.errorCode }
            if !m.safeMessage.isEmpty { o["safe_message"] = m.safeMessage }
            return o
        case .appCatalogRequest(let m):
            return ["force_rescan": m.forceRescan]
        case .appCatalog(let m):
            var o: [String: Any] = ["catalog_revision": m.catalogRevision]
            o["entries"] = m.entries.map { e -> [String: Any] in
                var eo: [String: Any] = [
                    "app_id": e.appID,
                    "display_name": e.displayName,
                    "type": e.type.rawValue,
                ]
                if !e.iconRef.isEmpty { eo["icon_ref"] = e.iconRef }
                eo["available"] = e.available
                return eo
            }
            return o
        case .appCatalogChanged(let m):
            return ["catalog_revision": m.catalogRevision]
        case .launchApp(let m):
            return ["app_id": m.appID]
        case .launchResult(let m):
            var o: [String: Any] = ["app_id": m.appID, "state": m.state.rawValue]
            if !m.errorCode.isEmpty { o["error_code"] = m.errorCode }
            if !m.safeMessage.isEmpty { o["safe_message"] = m.safeMessage }
            if let v = m.applicationSessionID { o["application_session_id"] = v }
            return o
        case .appStatus(let m):
            var o: [String: Any] = ["app_id": m.appID, "state": m.state.rawValue]
            if !m.applicationSessionID.isEmpty { o["application_session_id"] = m.applicationSessionID }
            return o
        case .windowAnnounce(let m):
            var o: [String: Any] = [
                "window_id": m.windowID,
                "application_session_id": m.applicationSessionID,
                "role": m.role.rawValue,
                "modal": m.modal,
                "visible": m.visible,
                "z_order": m.zOrder,
                "title": m.title,
                "x": m.x,
                "y": m.y,
                "width_px": m.widthPx,
                "height_px": m.heightPx,
                "dpi_scale_percent": m.dpiScalePercent,
                "state": m.state.rawValue,
            ]
            if let v = m.parentWindowID { o["parent_window_id"] = v }
            if let v = m.ownerWindowID { o["owner_window_id"] = v }
            return o
        case .windowUpdate(let m):
            var o: [String: Any] = ["window_id": m.windowID]
            if let v = m.title { o["title"] = v }
            if let v = m.x { o["x"] = v }
            if let v = m.y { o["y"] = v }
            if let v = m.widthPx { o["width_px"] = v }
            if let v = m.heightPx { o["height_px"] = v }
            if let v = m.state { o["state"] = v.rawValue }
            if let v = m.modal { o["modal"] = v }
            if let v = m.visible { o["visible"] = v }
            if let v = m.active { o["active"] = v }
            if let v = m.dpiScalePercent { o["dpi_scale_percent"] = v }
            return o
        case .windowRemove(let m):
            return ["window_id": m.windowID, "reason": m.reason.rawValue]
        case .inputEvent(let m):
            var o: [String: Any] = ["window_id": m.windowID]
            switch m.input {
            case .mouse(let mi):
                o["mouse"] = [
                    "kind": mi.kind.rawValue,
                    "button": mi.button.rawValue,
                    "x": mi.x,
                    "y": mi.y,
                    "wheel_delta": mi.wheelDelta,
                ]
            case .key(let ki):
                o["key"] = [
                    "windows_vk": ki.windowsVk,
                    "modifier_bits": ki.modifierBits,
                    "key_down": ki.keyDown,
                    "is_repeat": ki.isRepeat,
                ]
            }
            return o
        case .resizeRequest(let m):
            return ["window_id": m.windowID,
                    "client_width_px": m.clientWidthPx,
                    "client_height_px": m.clientHeightPx]
        case .resizeAck(let m):
            var o: [String: Any] = ["window_id": m.windowID, "ok": m.ok,
                                    "actual_width_px": m.actualWidthPx,
                                    "actual_height_px": m.actualHeightPx]
            if !m.errorCode.isEmpty { o["error_code"] = m.errorCode }
            return o
        case .windowAction(let m):
            return ["window_id": m.windowID, "kind": m.kind.rawValue]
        case .windowActionResult(let m):
            var o: [String: Any] = ["window_id": m.windowID, "kind": m.kind.rawValue,
                                    "state": m.state.rawValue]
            if !m.errorCode.isEmpty { o["error_code"] = m.errorCode }
            if !m.safeMessage.isEmpty { o["safe_message"] = m.safeMessage }
            return o
        case .frameRateRequest(let m):
            return ["window_id": m.windowID, "target_fps": m.targetFps,
                    "reason": m.reason.rawValue]
        case .streamStatus(let m):
            var o: [String: Any] = ["window_id": m.windowID, "state": m.state.rawValue,
                                    "width_px": m.widthPx, "height_px": m.heightPx,
                                    "actual_fps": m.actualFps]
            if !m.errorCode.isEmpty { o["error_code"] = m.errorCode }
            return o
        case .systemPowerRequest(let m):
            return ["action": m.action.rawValue]
        case .systemPowerResult(let m):
            var o: [String: Any] = ["action": m.action.rawValue, "state": m.state.rawValue]
            if !m.errorCode.isEmpty { o["error_code"] = m.errorCode }
            if !m.safeMessage.isEmpty { o["safe_message"] = m.safeMessage }
            return o
        case .heartbeat(let m):
            return ["sender_time_ms": m.senderTimeMs]
        case .performanceStats(let m):
            var o: [String: Any] = ["window_id": m.windowID, "fps": m.fps]
            if m.bitrateKbps != 0 { o["bitrate_kbps"] = m.bitrateKbps }
            if m.droppedFrames != 0 { o["dropped_frames"] = m.droppedFrames }
            if m.captureToEncodeMs != 0 { o["capture_to_encode_ms"] = m.captureToEncodeMs }
            if m.encodeMs != 0 { o["encode_ms"] = m.encodeMs }
            return o
        }
    }
}
