import Foundation
import CryptoKit
@testable import RemoteWindowsCore

/// §9.3 key derivation (RW-C-003 test 2): the golden values in
/// `key-derivation.json` are reproduced EXACTLY — the cross-language
/// compatibility proof against the Host's SHA-256 implementation.
/// Malformed session-key hex forms fail with the typed error.
enum MediaKeysSuite {
    static let cases: [Harness.Case] = [
        .case("goldenKeyDerivationReproducedExactly") {
            guard let url = MediaTestVectors.file("key-derivation.json") else {
                try harness.fail("key-derivation.json missing from the pinned snapshot")
            }
            let obj = try JSONSerialization.jsonObject(
                with: Data(contentsOf: url)) as! [String: Any]
            let inputs = obj["inputs"] as! [String: Any]
            let values = obj["values"] as! [String: Any]
            let code = inputs["code"] as! String
            let hostName = inputs["host_name"] as! String
            let windowID = inputs["window_id"] as! String

            let sessionKeyHex = MediaKeys.sessionKey(code: code, hostName: hostName)
            harness.expectEqual(sessionKeyHex, values["session_key"] as! String,
                                "session_key")
            harness.expectEqual(sessionKeyHex.count, 64)

            let mediaKey = try MediaKeys.mediaKey(
                sessionKeyHex: sessionKeyHex, windowID: windowID)
            harness.expectEqual(mediaKey.count, 32)
            harness.expectEqual(MediaHex.hexString(Array(mediaKey)),
                                values["media_key"] as! String, "media_key")

            let noncePrefix = try MediaKeys.noncePrefix(
                sessionKeyHex: sessionKeyHex, windowID: windowID)
            harness.expectEqual(noncePrefix.count, MediaFraming.noncePrefixBytes)
            harness.expectEqual(MediaHex.hexString(Array(noncePrefix)),
                                values["nonce_prefix"] as! String, "nonce_prefix")
        },
        .case("rawSessionKeyHexRoundTrip") {
            let code = "RWH-DEADBEEF"
            let hostName = "TESTHOST"
            let hex = MediaKeys.sessionKey(code: code, hostName: hostName)
            let raw = try MediaKeys.rawSessionKey(fromHex: hex)
            harness.expectEqual(raw.count, 32)
            harness.expectEqual(MediaHex.hexString(Array(raw)), hex)
        },
        .case("malformedSessionKeyHexRejected") {
            // Wrong length.
            do {
                _ = try MediaKeys.rawSessionKey(fromHex: "deadbeef")
                try harness.fail("expected invalidSessionKeyHex")
            } catch let error as MediaKeys.MediaKeysError {
                harness.expectEqual(error, .invalidSessionKeyHex)
            }
            // Non-hex char.
            do {
                _ = try MediaKeys.rawSessionKey(
                    fromHex: "g" + String(repeating: "a", count: 63))
                try harness.fail("expected invalidSessionKeyHex")
            } catch let error as MediaKeys.MediaKeysError {
                harness.expectEqual(error, .invalidSessionKeyHex)
            }
            // Uppercase (the wire format is lowercase-hex, Host rule).
            do {
                _ = try MediaKeys.rawSessionKey(fromHex: String(repeating: "A", count: 64))
                try harness.fail("expected invalidSessionKeyHex")
            } catch let error as MediaKeys.MediaKeysError {
                harness.expectEqual(error, .invalidSessionKeyHex)
            }
            // The media-key path validates the stored key too.
            do {
                _ = try MediaKeys.mediaKey(sessionKeyHex: String(repeating: "0", count: 63),
                                           windowID: "w")
                try harness.fail("expected invalidSessionKeyHex")
            } catch let error as MediaKeys.MediaKeysError {
                harness.expectEqual(error, .invalidSessionKeyHex)
            }
        },
        .case("hexHelpers") {
            let bytes: [UInt8] = [0x00, 0x5A, 0xAB, 0xFF]
            let hex = MediaHex.hexString(bytes)
            harness.expectEqual(hex, "005aabff")
            harness.expectEqual(MediaHex.data(hexString: hex), Data(bytes))
            harness.expect(MediaHex.data(hexString: "abc") == nil,
                           "odd-length hex must not decode")
            harness.expect(MediaHex.data(hexString: "0g") == nil,
                           "non-hex must not decode")
        },
    ]
}
