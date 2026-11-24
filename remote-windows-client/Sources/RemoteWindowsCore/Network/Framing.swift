import Foundation

/// Wire framing for v0.x (PROTOCOL.md §1):
/// frame = 4-byte big-endian length + UTF-8 JSON payload, length ≤ 8 MiB.
/// Mirrors the Host's `network/framing.cpp`.
public enum Framing {
    public static let maxFrameSize = 8 * 1024 * 1024

    /// Encode one payload as a wire frame.
    public static func encode(_ payload: Data) throws -> Data {
        guard payload.count <= maxFrameSize else {
            throw CodecError("malformed_frame",
                             "frame size \(payload.count) exceeds \(maxFrameSize)")
        }
        var out = Data(capacity: payload.count + 4)
        let len = UInt32(payload.count)
        out.append(UInt8((len >> 24) & 0xff))
        out.append(UInt8((len >> 16) & 0xff))
        out.append(UInt8((len >> 8) & 0xff))
        out.append(UInt8(len & 0xff))
        out.append(payload)
        return out
    }

    /// Parse the 4-byte header; returns the payload length.
    public static func parseLength(_ bytes: ArraySlice<UInt8>) throws -> Int {
        guard bytes.count >= 4 else {
            throw CodecError("malformed_frame", "incomplete header")
        }
        let b = Array(bytes.prefix(4))
        let len = Int(b[0]) << 24 | Int(b[1]) << 16 | Int(b[2]) << 8 | Int(b[3])
        guard len <= maxFrameSize else {
            throw CodecError("malformed_frame", "declared frame size \(len) exceeds \(maxFrameSize)")
        }
        return len
    }
}

/// Incremental reassembly of frames from a byte stream (TCP reads may
/// deliver partial headers/payloads). Feed bytes; collect frames.
public final class FrameReader {
    private var buffer: [UInt8] = []

    /// Feed more bytes; returns complete payloads in order.
    public func feed(_ data: Data) throws -> [Data] {
        buffer.append(contentsOf: data)
        var frames: [Data] = []
        while buffer.count >= 4 {
            let len = try parseAvailableLength()
            guard buffer.count >= 4 + len else { break }
            let payloadStart = buffer.index(buffer.startIndex, offsetBy: 4)
            let payloadEnd = buffer.index(payloadStart, offsetBy: len)
            frames.append(Data(buffer[payloadStart..<payloadEnd]))
            buffer.removeSubrange(buffer.startIndex..<payloadEnd)
        }
        return frames
    }

    public func reset() { buffer.removeAll(keepingCapacity: true) }

    public var bufferedByteCount: Int { buffer.count }

    private func parseAvailableLength() throws -> Int {
        let head = buffer.prefix(4)
        let len = try Framing.parseLength(head)
        return len
    }
}
