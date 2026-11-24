import Foundation

/// Semantic validation for decoded envelopes — a 1:1 port of the Host's
/// `remote-windows-host/core/protocol_validation.cpp` (same rules, bounds,
/// and error codes; PROTOCOL.md §4-6). Structural problems are already
/// reported by `ProtocolCodec` with ParseError codes.
public enum ProtocolValidation {
    public struct Result: Equatable {
        public var ok: Bool
        public var errorCode: String
        public var detail: String
        public init(ok: Bool, errorCode: String = "", detail: String = "") {
            self.ok = ok
            self.errorCode = errorCode
            self.detail = detail
        }
    }

    // MARK: Field helpers (shared with the Host implementation)

    public static func isUuid(_ s: String) -> Bool {
        let bytes = Array(s.utf8)
        guard bytes.count == 36 else { return false }
        for (i, b) in bytes.enumerated() {
            if i == 8 || i == 13 || i == 18 || i == 23 {
                if b != UInt8(ascii: "-") { return false }
            } else if !isHexDigit(b) {
                return false
            }
        }
        return true
    }

    public static func isAppId(_ s: String) -> Bool {
        guard !s.isEmpty, s.count <= 128 else { return false }
        let bytes = Array(s.utf8)
        guard isAlnum(bytes[0]) else { return false }
        for b in bytes where !isAlnum(b) && b != UInt8(ascii: "-") && b != UInt8(ascii: "_") {
            return false
        }
        return true
    }

    public static func isSixDigitCode(_ s: String) -> Bool {
        let bytes = Array(s.utf8)
        guard bytes.count == 6 else { return false }
        return bytes.allSatisfy { isDigit($0) }
    }

    public static func isHex32(_ s: String) -> Bool {
        let bytes = Array(s.utf8)
        guard bytes.count == 32 else { return false }
        return bytes.allSatisfy { isHexDigit($0) }
    }

    public static func validAppName(_ s: String) -> Bool {
        guard !s.isEmpty, utf8Length(s) <= 64 else { return false }
        return Array(s.utf8).allSatisfy { $0 >= 0x20 && $0 != 0x7f }
    }

    /// UTF-8 code-point count; -1 if invalid UTF-8.
    public static func utf8Length(_ s: String) -> Int32 {
        var count = 0
        var i = 0
        let bytes = Array(s.utf8)
        while i < bytes.count {
            let c = bytes[i]
            let extra: Int
            if c < 0x80 { extra = 0 }
            else if (c & 0xe0) == 0xc0 { extra = 1 }
            else if (c & 0xf0) == 0xe0 { extra = 2 }
            else if (c & 0xf8) == 0xf0 { extra = 3 }
            else { return -1 }
            if i + extra >= bytes.count { return -1 }
            if extra > 0 {
                for k in 1...extra where (bytes[i + k] & 0xc0) != 0x80 { return -1 }
            }
            i += 1 + extra
            count += 1
        }
        return Int32(count)
    }

    /// Decode-time guards (mirroring the Host codec):
    /// UUID format and title length are rejected before routing.
    public static func requireUuid(_ s: String, field: String) throws {
        guard isUuid(s) else { throw CodecError("bad_uuid", "not a canonical UUID: \(field)") }
    }

    public static func requireTitle(_ s: String, field: String,
                                    maxCodePoints: Int32 = 512) throws {
        let n = utf8Length(s)
        guard n >= 0, n <= maxCodePoints else {
            throw CodecError("title_too_long", "title exceeds \(maxCodePoints) code points: \(field)")
        }
    }

    // MARK: Envelope validation

    public static func validate(_ e: Envelope) -> Result {
        func fail(_ code: String, _ detail: String = "") -> Result {
            Result(ok: false, errorCode: code, detail: detail)
        }

        guard e.schemaMajor == 1 else {
            return fail("schema_major_must_be_1",
                        "schema_major must equal 1 (got \(e.schemaMajor))")
        }
        guard RWKind(rawValue: e.kind.rawValue) != nil else {
            return fail("unknown_kind", e.kind.rawValue)
        }
        if e.kind.requiresRequestID && e.requestID == nil {
            return fail("missing_request_id", e.kind.rawValue)
        }
        if e.kind.requiresSequence && e.sequence == nil {
            return fail("missing_sequence", e.kind.rawValue)
        }

        let maxDim: Int64 = 38_400
        let maxDpi: UInt32 = 400
        let maxFps: UInt32 = 240
        let maxTitle: Int32 = 512
        let maxSafeMsg: Int32 = 256
        let maxCaps = 16

        func safeMsgOk(_ s: String) -> Bool { utf8Length(s) <= maxSafeMsg }

        switch e.payload {
        case .hello(let m):
            guard m.productName == "windows-host" || m.productName == "macos-client" else {
                return fail("invalid_product_name", m.productName)
            }
            guard !m.appVersion.isEmpty, m.appVersion.utf8.count <= 64 else {
                return fail("invalid_app_version", m.appVersion)
            }
            guard m.capabilities.count <= maxCaps else {
                return fail("list_too_long", "capabilities")
            }
            for c in m.capabilities where c == .unspecified {
                return fail("invalid_enum", "capability unspecified")
            }
        case .pairingChallenge(let m):
            guard m.oneTimeCode != nil || m.hostName != nil || m.challengeNonce != nil else {
                return fail("empty_payload", "pairing_challenge")
            }
            if let v = m.oneTimeCode, !isSixDigitCode(v) {
                return fail("invalid_pairing_code", v)
            }
            if let v = m.hostName, !validAppName(v) {
                return fail("invalid_app_name", v)
            }
            if let v = m.challengeNonce, !isHex32(v) {
                return fail("invalid_nonce", v)
            }
        case .pairingResult(let m):
            if !m.errorCode.isEmpty, m.errorCode.utf8.count > 64 {
                return fail("invalid_error_code", m.errorCode)
            }
            if !m.safeMessage.isEmpty, !safeMsgOk(m.safeMessage) {
                return fail("invalid_safe_message", m.safeMessage)
            }
        case .launchApp(let m):
            guard isAppId(m.appID) else { return fail("invalid_app_id", m.appID) }
        case .launchResult(let m):
            guard isAppId(m.appID) else { return fail("invalid_app_id", m.appID) }
            guard m.state != .unspecified else {
                return fail("invalid_enum", "launch state")
            }
            if let s = m.applicationSessionID, s.isEmpty || s.utf8.count > 64 {
                return fail("invalid_session_id", s)
            }
            if !m.errorCode.isEmpty, m.errorCode.utf8.count > 64 {
                return fail("invalid_error_code", m.errorCode)
            }
            if !m.safeMessage.isEmpty, !safeMsgOk(m.safeMessage) {
                return fail("invalid_safe_message", m.safeMessage)
            }
        case .appStatus(let m):
            guard isAppId(m.appID) else { return fail("invalid_app_id", m.appID) }
            guard m.state != .unspecified else {
                return fail("invalid_enum", "app state")
            }
        case .appCatalogRequest:
            break
        case .appCatalog(let m):
            for entry in m.entries where !isAppId(entry.appID) {
                return fail("invalid_app_id", entry.appID)
            }
        case .appCatalogChanged:
            break
        case .windowAnnounce(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            guard !m.applicationSessionID.isEmpty,
                  m.applicationSessionID.utf8.count <= 64 else {
                return fail("invalid_session_id", m.applicationSessionID)
            }
            guard m.role != .unspecified else {
                return fail("invalid_enum", "window role")
            }
            guard m.state != .unspecified else {
                return fail("invalid_enum", "window state")
            }
            guard m.widthPx != 0, m.heightPx != 0,
                  Int64(m.widthPx) <= maxDim, Int64(m.heightPx) <= maxDim else {
                return fail("invalid_dimensions",
                            "\(m.widthPx)x\(m.heightPx)")
            }
            guard m.dpiScalePercent >= 25, m.dpiScalePercent <= maxDpi else {
                return fail("invalid_dpi", String(m.dpiScalePercent))
            }
            guard utf8Length(m.title) <= maxTitle else {
                return fail("title_too_long", m.title)
            }
            if let v = m.parentWindowID, !isUuid(v) {
                return fail("invalid_uuid", v)
            }
            if let v = m.ownerWindowID, !isUuid(v) {
                return fail("invalid_uuid", v)
            }
        case .windowUpdate(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            let hasField = m.title != nil || m.x != nil || m.y != nil ||
                m.widthPx != nil || m.heightPx != nil || m.state != nil ||
                m.modal != nil || m.visible != nil || m.active != nil ||
                m.dpiScalePercent != nil
            guard hasField else {
                return fail("empty_payload", "window_update needs window_id plus one change")
            }
            if let w = m.widthPx, w == 0 || Int64(w) > maxDim {
                return fail("invalid_dimensions", e.kind.rawValue)
            }
            if let h = m.heightPx, h == 0 || Int64(h) > maxDim {
                return fail("invalid_dimensions", e.kind.rawValue)
            }
            if let d = m.dpiScalePercent, d < 25 || d > maxDpi {
                return fail("invalid_dpi", String(d))
            }
            if let t = m.title, utf8Length(t) > maxTitle {
                return fail("title_too_long", t)
            }
        case .windowRemove(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            guard m.reason != .unspecified else {
                return fail("invalid_enum", "removal reason")
            }
        case .inputEvent(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            switch m.input {
            case .mouse(let mi):
                guard mi.kind != .unspecified else {
                    return fail("invalid_enum", "mouse kind")
                }
            case .key(let ki):
                guard ki.windowsVk >= 1, ki.windowsVk <= 255 else {
                    return fail("vk_out_of_range", String(ki.windowsVk))
                }
                guard ki.modifierBits <= 0xF else {
                    return fail("invalid_modifiers", String(ki.modifierBits))
                }
            }
        case .resizeRequest(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            guard m.clientWidthPx != 0, m.clientHeightPx != 0,
                  Int64(m.clientWidthPx) <= maxDim, Int64(m.clientHeightPx) <= maxDim else {
                return fail("invalid_dimensions", e.kind.rawValue)
            }
        case .resizeAck(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
        case .windowAction(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            guard m.kind != .unspecified else {
                return fail("invalid_enum", "action kind")
            }
        case .windowActionResult(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            guard m.state != .unspecified else {
                return fail("invalid_enum", "action result state")
            }
        case .frameRateRequest(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
            guard m.targetFps <= maxFps else {
                return fail("fps_out_of_range", String(m.targetFps))
            }
            guard m.reason != .unspecified else {
                return fail("invalid_enum", "fps reason")
            }
        case .streamStatus(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
        case .systemPowerRequest(let m):
            guard m.action == .sleep || m.action == .shutdown else {
                return fail("invalid_enum", "power action")
            }
        case .systemPowerResult(let m):
            guard m.action == .sleep || m.action == .shutdown else {
                return fail("invalid_enum", "power action")
            }
            guard m.state != .unspecified else {
                return fail("invalid_enum", "power result state")
            }
        case .heartbeat:
            break
        case .performanceStats(let m):
            guard isUuid(m.windowID) else { return fail("invalid_uuid", m.windowID) }
        }
        return Result(ok: true)
    }

    // MARK: Byte helpers

    private static func isAlnum(_ b: UInt8) -> Bool {
        (b >= 0x30 && b <= 0x39) || (b >= 0x41 && b <= 0x5a) || (b >= 0x61 && b <= 0x7a)
    }
    private static func isDigit(_ b: UInt8) -> Bool { b >= 0x30 && b <= 0x39 }
    private static func isHexDigit(_ b: UInt8) -> Bool {
        isDigit(b) || (b >= 0x41 && b <= 0x46) || (b >= 0x61 && b <= 0x66)
    }
}
