import Foundation
import CryptoKit

/// PROTOCOL.md §9.3 — exact key-derivation formulas for the media
/// channel (lowercase-hex SHA-256, as in the Host's `core/sha256.cpp`).
/// Mirrors the Host's `media/stream_keys.cpp` (RW-H-007); the golden
/// vector `Protocol/Contract/test-vectors/media/key-derivation.json`
/// MUST be reproduced exactly by both implementations.
public enum MediaKeys {
    /// `session_key` = `hex(sha256("rwh-pairing-v1|" + code + "|" + host_name))`
    /// — 64 lowercase-hex chars. This is exactly the Host's
    /// `core::PairingManager::deriveSessionKey`; both peers derive it
    /// from the one-time code + host_name at pairing time.
    public static func sessionKey(code: String, hostName: String) -> String {
        let message = "rwh-pairing-v1|\(code)|\(hostName)"
        return MediaHex.hexString(Array(SHA256.hash(data: Data(message.utf8))))
    }

    /// The 32 raw bytes of the session key (HMAC input for the §9.2
    /// handshake).
    public static func rawSessionKey(code: String, hostName: String) -> Data {
        // `sessionKey` always emits 64 valid lowercase-hex chars.
        return MediaHex.data(hexString: sessionKey(code: code, hostName: hostName))!
    }

    /// Decode a 64-char lowercase-hex session key to its 32 raw bytes.
    /// Throws `MediaKeysError.invalidSessionKeyHex` for any other form
    /// (wrong length, non-hex, or uppercase — the format is
    /// lowercase-hex, mirroring the Host's `validSessionKeyHex`).
    public static func rawSessionKey(fromHex hex: String) throws -> Data {
        try validateSessionKeyHex(hex)
        return MediaHex.data(hexString: hex)!
    }

    /// `media_key(window_id)` =
    /// `hex(sha256("rwh-media-v1|" + session_key_hex + "|" + window_id))`
    /// — 32 raw bytes; the AES-256 key for that window's stream.
    public static func mediaKey(sessionKeyHex: String, windowID: String) throws -> Data {
        try validateSessionKeyHex(sessionKeyHex)
        return digest(prefix: "rwh-media-v1|",
                      sessionKeyHex: sessionKeyHex, windowID: windowID)
    }

    /// `nonce_prefix(window_id)` = the first 8 bytes of
    /// `sha256("rwh-media-nonce-v1|" + session_key_hex + "|" + window_id))`.
    public static func noncePrefix(sessionKeyHex: String, windowID: String) throws -> Data {
        try validateSessionKeyHex(sessionKeyHex)
        let digest = digest(prefix: "rwh-media-nonce-v1|",
                            sessionKeyHex: sessionKeyHex, windowID: windowID)
        // Array-space slice (no `Data.subdata` — see MediaAead.open note).
        let bytes = [UInt8](digest)
        return Data(bytes[0..<MediaFraming.noncePrefixBytes])
    }

    // MARK: Internals

    enum MediaKeysError: Error, Equatable {
        /// Not a 64-char lowercase-hex session key.
        case invalidSessionKeyHex
    }

    private static func validateSessionKeyHex(_ hex: String) throws {
        guard hex.count == 64 else { throw MediaKeysError.invalidSessionKeyHex }
        for scalar in hex.unicodeScalars {
            if ("0"..."9").contains(scalar) || ("a"..."f").contains(scalar) { continue }
            // Uppercase rejected: the format is lowercase-hex (Host rule).
            throw MediaKeysError.invalidSessionKeyHex
        }
    }

    private static func digest(prefix: String, sessionKeyHex: String,
                               windowID: String) -> Data {
        let message = prefix + sessionKeyHex + "|" + windowID
        return Data(SHA256.hash(data: Data(message.utf8)))
    }
}

// MARK: - Hex helpers (lowercase, shared by the media layer)

public enum MediaHex {
    private static let nibble: [UInt8: UInt8] = {
        var table: [UInt8: UInt8] = [:]
        for (i, c) in "0123456789abcdef".unicodeScalars.enumerated() {
            table[UInt8(c.value)] = UInt8(i)
        }
        return table
    }()

    private static let hexDigits: [Character] = Array("0123456789abcdef")

    /// 2n lowercase-hex chars for a byte sequence (e.g. a digest).
    public static func hexString(_ bytes: [UInt8]) -> String {
        var out = ""
        for byte in bytes {
            out.append(hexDigits[Int(byte >> 4)])
            out.append(hexDigits[Int(byte & 0x0F)])
        }
        return out
    }

    /// Decode a lowercase-hex string to raw bytes. Returns `nil` for any
    /// odd-length or non-hex input (callers validate first when the
    /// input is a session key).
    public static func data(hexString: String) -> Data? {
        let scalars = Array(hexString.unicodeScalars)
        guard scalars.count % 2 == 0, !scalars.isEmpty else { return nil }
        var bytes = Data(capacity: scalars.count / 2)
        var i = 0
        while i < scalars.count {
            guard let hi = nibble[UInt8(scalars[i].value)],
                  let lo = nibble[UInt8(scalars[i + 1].value)] else { return nil }
            bytes.append(hi << 4 | lo)
            i += 2
        }
        return bytes
    }
}
