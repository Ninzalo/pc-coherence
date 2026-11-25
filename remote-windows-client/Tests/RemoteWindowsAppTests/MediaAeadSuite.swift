import Foundation
import CryptoKit
@testable import RemoteWindowsCore

/// §9.4 AES-256-GCM (RW-C-003 test 3): EVERY vector in
/// `aes-256-gcm.json` is reproduced byte-exactly in both directions
/// (seal → ciphertext+tag; open → plaintext) — the cross-language
/// compatibility proof against the Host's CNG implementation.
/// Tamper detection and the per-stream counter are covered as well.
enum MediaAeadSuite {
    static let cases: [Harness.Case] = [
        .case("goldenAES256GCMVectorsReproducedExactly") {
            guard let url = MediaTestVectors.file("aes-256-gcm.json") else {
                try harness.fail("aes-256-gcm.json missing from the pinned snapshot")
            }
            let obj = try JSONSerialization.jsonObject(
                with: Data(contentsOf: url)) as! [String: Any]
            let vectors = obj["vectors"] as! [[String: Any]]
            harness.expect(!vectors.isEmpty, "vector file has no vectors")
            for vector in vectors {
                let name = vector["name"] as! String
                let key = MediaTestVectors.hex(vector["key"] as! String)
                let nonce = MediaTestVectors.hex(vector["nonce"] as! String)
                let plaintext = MediaTestVectors.hex(vector["plaintext"] as! String)
                let expectedCT = MediaTestVectors.hex(vector["ciphertext"] as! String)
                let expectedTag = MediaTestVectors.hex(vector["tag"] as! String)
                harness.expectEqual(key.count, 32, "\(name): key length")
                harness.expectEqual(nonce.count, 12, "\(name): nonce length")

                // Seal: ciphertext AND tag must match the vector byte-exactly.
                let sealed = try MediaAead.seal(plaintext: plaintext,
                                                keyData: key,
                                                nonceData: nonce)
                harness.expectEqual(sealed.ciphertext, expectedCT,
                                    "\(name): seal ciphertext")
                harness.expectEqual(sealed.tag, expectedTag,
                                    "\(name): seal tag")

                // Open the vector's wire payload (ciphertext || tag):
                // plaintext must match byte-exactly.
                let opened = try MediaAead.open(payload: expectedCT + expectedTag,
                                                keyData: key,
                                                nonceData: nonce)
                harness.expectEqual(opened, plaintext, "\(name): open plaintext")
            }
        },
        .case("tamperedCiphertextRejected") {
            guard let url = MediaTestVectors.file("aes-256-gcm.json") else {
                try harness.fail("vector file missing")
            }
            let obj = try JSONSerialization.jsonObject(
                with: Data(contentsOf: url)) as! [String: Any]
            let vector = (obj["vectors"] as! [[String: Any]])
                .first { ($0["name"] as! String) == "vec_c_1byte" }!
            let key = MediaTestVectors.hex(vector["key"] as! String)
            let nonce = MediaTestVectors.hex(vector["nonce"] as! String)
            var ct = MediaTestVectors.hex(vector["ciphertext"] as! String)
            let tag = MediaTestVectors.hex(vector["tag"] as! String)
            ct[0] ^= 0x01   // flip one ciphertext byte
            do {
                _ = try MediaAead.open(payload: ct + tag, keyData: key, nonceData: nonce)
                try harness.fail("expected a tamper failure")
            } catch let error as MediaAead.MediaAeadError {
                harness.expectEqual(error, .tampered)
            }
        },
        .case("tamperedTagRejected") {
            guard let url = MediaTestVectors.file("aes-256-gcm.json") else {
                try harness.fail("vector file missing")
            }
            let obj = try JSONSerialization.jsonObject(
                with: Data(contentsOf: url)) as! [String: Any]
            let vector = (obj["vectors"] as! [[String: Any]])
                .first { ($0["name"] as! String) == "vec_c_1byte" }!
            let key = MediaTestVectors.hex(vector["key"] as! String)
            let nonce = MediaTestVectors.hex(vector["nonce"] as! String)
            let ct = MediaTestVectors.hex(vector["ciphertext"] as! String)
            var tag = MediaTestVectors.hex(vector["tag"] as! String)
            tag[15] ^= 0x80   // flip the final tag byte
            do {
                _ = try MediaAead.open(payload: ct + tag, keyData: key, nonceData: nonce)
                try harness.fail("expected a tamper failure")
            } catch let error as MediaAead.MediaAeadError {
                harness.expectEqual(error, .tampered)
            }
        },
        .case("shortInputsRejected") {
            do {
                _ = try MediaAead.seal(plaintext: Data([0x01]),
                                       keyData: Data(repeating: 0, count: 16),
                                       nonceData: Data(repeating: 0, count: 12))
                try harness.fail("expected a typed failure")
            } catch let error as MediaAead.MediaAeadError {
                harness.expectEqual(error, .shortInput(length: 16))
            }
            do {
                _ = try MediaAead.open(payload: Data(repeating: 0, count: 10),
                                       keyData: Data(repeating: 0, count: 32),
                                       nonceData: Data(repeating: 0, count: 12))
                try harness.fail("expected a typed failure")
            } catch let error as MediaAead.MediaAeadError {
                harness.expectEqual(error, .shortInput(length: 10))
            }
        },
        .case("streamCipherCounterAndFrameSeqOpen") {
            let key = MediaTestVectors.hex(
                "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
            let prefix = Data(repeating: 0x5A, count: MediaFraming.noncePrefixBytes)
            let cipher = MediaStreamCipher(keyData: key, noncePrefix: prefix)
            let plain = Data([0x10, 0x20, 0x30])
            let first = try cipher.seal(plain)
            let second = try cipher.seal(plain)
            // Same plaintext, advancing per-stream counter → different
            // wire payloads (nonce = prefix || counter, §9.3).
            harness.expect(first != second,
                           "counter must advance per frame")
            harness.expectEqual(first.count, plain.count + MediaFraming.gcmTagBytes)
            harness.expectEqual(cipher.nextFrameCounter, 2)
            // The receive side opens with the header's frame_seq.
            let opened0 = try cipher.open(first, frameSeq: 0)
            harness.expectEqual(opened0, plain)
            let opened1 = try cipher.open(second, frameSeq: 1)
            harness.expectEqual(opened1, plain)
            // A wrong frame_seq is a GCM authentication failure.
            do {
                _ = try cipher.open(first, frameSeq: 1)
                try harness.fail("expected a tamper failure")
            } catch let error as MediaAead.MediaAeadError {
                harness.expectEqual(error, .tampered)
            }
        },
    ]
}
