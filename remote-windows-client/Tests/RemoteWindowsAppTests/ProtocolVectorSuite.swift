import Foundation
@testable import RemoteWindowsCore

/// The 22 canonical test vectors (shared golden set with the Host) are the
/// cross-language compatibility contract. Every `valid` vector must decode
/// AND validate clean; every `invalid` vector must be rejected with the
/// exact `violation` code — at the codec or the validation stage.
enum ProtocolVectorSuite {
    static func vectorsDir() -> URL? {
        // Walk up from this source file to the repository checkout root.
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<8 {
            url.deleteLastPathComponent()
            let candidate = url.appendingPathComponent("remote-windows-client/Protocol/Contract/test-vectors")
            var isDir: ObjCBool = false
            if FileManager.default.fileExists(atPath: candidate.path, isDirectory: &isDir),
               isDir.boolValue {
                return candidate
            }
        }
        return nil
    }

    static let cases: [Harness.Case] = [
        .case("snapshotPresent") {
            guard Self.vectorsDir() != nil else {
                try harness.fail("Protocol/Contract/test-vectors missing — run tools/sync_protocol_from_host.sh")
            }
        },
        .case("allCanonicalVectors") {
            guard let dir = Self.vectorsDir() else {
                try harness.fail("snapshot not synced")
            }
            let files = try FileManager.default.contentsOfDirectory(
                atPath: dir.path).filter { $0.hasSuffix(".json") }.sorted()
            harness.expect(files.count >= 22,
                           "expected the full canonical vector set (22), got \(files.count)")
            var checked = 0
            for name in files {
                let path = dir.appendingPathComponent(name).path
                let data = try Data(contentsOf: URL(fileURLWithPath: path))
                let obj = try JSONSerialization.jsonObject(with: data) as! [String: Any]
                guard let envelopeObj = obj["envelope"] as? [String: Any] else {
                    try harness.fail("\(name): vector has no envelope")
                }
                let envelopeData = try JSONSerialization.data(withJSONObject: envelopeObj)
                let expected = obj["expected"] as! String
                let violation = obj["violation"] as? String

                do {
                    let envelope = try ProtocolCodec.decodeEnvelope(envelopeData)
                    let result = ProtocolValidation.validate(envelope)
                    if expected == "valid" {
                        harness.expect(result.ok,
                            "\(name): expected valid but validation failed: \(result.errorCode) \(result.detail)")
                    } else {
                        harness.expectEqual(result.errorCode, violation ?? "",
                            "\(name): expected violation \(violation ?? "?"), got \(result.errorCode)")
                    }
                } catch let e as CodecError {
                    harness.expectEqual(expected, "invalid",
                        "\(name): valid vector failed to decode: \(e.code) \(e.message)")
                    harness.expectEqual(e.code, violation ?? "",
                        "\(name): expected violation \(violation ?? "?"), got \(e.code)")
                }
                checked += 1
            }
            harness.expectEqual(checked, files.count)
        },
    ]
}
