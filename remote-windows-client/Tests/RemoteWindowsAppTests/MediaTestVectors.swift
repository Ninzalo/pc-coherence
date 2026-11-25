import Foundation
@testable import RemoteWindowsCore

/// Shared lookup for the pinned golden vectors in
/// `Protocol/Contract/test-vectors/media/` (the read-only snapshot).
enum MediaTestVectors {
    /// Walk up from this source file to the `test-vectors` directory
    /// (same convention as ProtocolVectorSuite).
    static func vectorsDir() -> URL? {
        var url = URL(fileURLWithPath: #filePath)
        for _ in 0..<8 {
            url.deleteLastPathComponent()
            let candidate = url
                .appendingPathComponent("remote-windows-client/Protocol/Contract/test-vectors")
            var isDir: ObjCBool = false
            if FileManager.default.fileExists(atPath: candidate.path, isDirectory: &isDir),
               isDir.boolValue {
                return candidate
            }
        }
        return nil
    }

    static func file(_ name: String) -> URL? {
        vectorsDir()?.appendingPathComponent("media").appendingPathComponent(name)
    }

    static func hex(_ s: String) -> Data {
        MediaHex.data(hexString: s) ?? Data()
    }
}
