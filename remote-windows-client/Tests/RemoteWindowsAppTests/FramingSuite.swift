import Foundation
@testable import RemoteWindowsCore

enum FramingSuite {
    static let cases: [Harness.Case] = [
        .case("encodeProducesBigEndianLengthPrefix") {
            let payload = Data("{}".utf8)
            let frame = try Framing.encode(payload)
            harness.expectEqual(Array(frame.prefix(4)), [0, 0, 0, 2])
            harness.expectEqual(Array(frame.suffix(2)), [0x7b, 0x7d])
        },
        .case("encodeRejectsOversizedFrame") {
            let big = Data(repeating: 0x61, count: Framing.maxFrameSize + 1)
            var threw = false
            do { _ = try Framing.encode(big) } catch { threw = true }
            harness.expect(threw, "oversized frame must throw")
        },
        .case("parseLengthRejectsDeclaredOversize") {
            // Declares 8 MiB + 1
            let header = [UInt8(0xff), UInt8(0xff), UInt8(0xff), UInt8(0x01)]
            var threw = false
            do { _ = try Framing.parseLength(header[...]) } catch { threw = true }
            harness.expect(threw, "declared oversize must throw")
        },
        .case("frameReaderReassemblesAcrossPartialFeeds") {
            let payload = try Data(#"{"schema_major":1,"schema_minor":0,"kind":"heartbeat","payload":{"sender_time_ms":1}}"#.utf8)
            let frame = try Framing.encode(payload)
            let reader = FrameReader()

            // Feed in awkward chunk sizes (1 byte, 3 bytes, rest).
            let bytes = Array(frame)
            var frames = try reader.feed(Data(bytes.prefix(1)))
            frames += try reader.feed(Data(bytes[1..<4]))
            harness.expectEqual(frames.count, 0, "no complete frame yet")
            frames += try reader.feed(Data(bytes[4...]))
            harness.expectEqual(frames.count, 1)
            harness.expectEqual(frames[0], payload)
        },
        .case("frameReaderHandlesMultipleFramesInOneFeed") {
            let a = try Framing.encode(Data("aa".utf8))
            let b = try Framing.encode(Data("bb".utf8))
            let reader = FrameReader()
            let frames = try reader.feed(a + b)
            harness.expectEqual(frames.count, 2)
            harness.expectEqual(frames[0], Data("aa".utf8))
            harness.expectEqual(frames[1], Data("bb".utf8))
        },
        .case("frameReaderRejectsOversizedDeclaredLength") {
            let reader = FrameReader()
            // Header declares 9 MiB; the feed must throw, not hang.
            let header = [UInt8(0xff), UInt8(0xff), UInt8(0xff), UInt8(0x01)]
            var threw = false
            do { _ = try reader.feed(Data(header)) } catch { threw = true }
            harness.expect(threw, "oversized declared length must throw")
        },
    ]
}
