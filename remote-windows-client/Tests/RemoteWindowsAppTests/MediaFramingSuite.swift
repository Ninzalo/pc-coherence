import Foundation
import CryptoKit
@testable import RemoteWindowsCore

/// §9.4 binary framing (RW-C-003 test 1): the exact LE byte layout is
/// pinned with literals; parse rejects every §9.6 malformed-header
/// condition; the incremental reader reassembles split/multi-packet
/// streams; `streamID` matches the first two sha256 bytes
/// (little-endian) computed in-test.
enum MediaFramingSuite {
    static let cases: [Harness.Case] = [
        .case("serializedHeaderExactBytes") {
            // §9.4 field list: u8 magic | u8 version | u16 LE stream_id
            // | u8 flags | u32 LE frame_seq | u32 LE payload_len.
            let header = MediaHeader(streamId: 0x1234, flags: 0x01,
                                     frameSeq: 0x0000000A, payloadLen: 0x00010020)
            harness.expectEqual(
                header.serialize(),
                Data([0x52, 0x01, 0x34, 0x12, 0x01,
                      0x0A, 0x00, 0x00, 0x00,
                      0x20, 0x00, 0x01, 0x00]))
            harness.expectEqual(header.isKeyframe, true)
        },
        .case("parseRoundTrip") {
            let original = MediaHeader(streamId: 0xABCD, flags: 0x00,
                                       frameSeq: 0xDEADBEEF, payloadLen: 0x0000FFFF)
            let parsed = MediaHeader.parse(original.serialize())
            harness.expectEqual(parsed, .success(original))
            if case .success(let header) = parsed {
                harness.expectEqual(header.isKeyframe, false)
            }
        },
        .case("parseRejectsShortBuffer") {
            for n in 0..<MediaFraming.headerBytes {
                let data = Data(repeating: 0x52, count: n)
                harness.expectEqual(MediaHeader.parse(data),
                                    .failure(.shortBuffer(length: n)))
            }
        },
        .case("parseRejectsBadMagic") {
            var data = Data(repeating: 0, count: MediaFraming.headerBytes)
            data[0] = 0x53
            harness.expectEqual(MediaHeader.parse(data),
                                .failure(.badMagic(found: 0x53)))
        },
        .case("parseRejectsBadVersion") {
            var data = Data(repeating: 0, count: MediaFraming.headerBytes)
            data[0] = MediaFraming.magic
            data[1] = 2
            harness.expectEqual(MediaHeader.parse(data),
                                .failure(.badVersion(found: 2)))
        },
        .case("payloadLen16MiBBoundary") {
            let atLimit = MediaHeader(streamId: 1, frameSeq: 1,
                                      payloadLen: UInt32(MediaFraming.maxPayloadLen))
            harness.expectEqual(
                MediaHeader.parse(atLimit.serialize()), .success(atLimit),
                "16 MiB payload_len must be accepted")
            let over = MediaHeader(streamId: 1, frameSeq: 1,
                                   payloadLen: UInt32(MediaFraming.maxPayloadLen) + 1)
            harness.expectEqual(
                MediaHeader.parse(over.serialize()),
                .failure(.oversizedPayload(
                    payloadLen: UInt32(MediaFraming.maxPayloadLen) + 1)))
        },
        .case("streamIDIsFirstTwoSha256BytesLE") {
            let windowID = "11111111-2222-3333-4444-555555555555"
            let digest = Array(SHA256.hash(data: Data(windowID.utf8)))
            let expected = UInt16(digest[0]) | (UInt16(digest[1]) << 8)
            harness.expectEqual(MediaFraming.streamID(forWindowID: windowID),
                                expected)
        },
        .case("nonceIsPrefixPlusCounterLE") {
            let prefix = Data(repeating: 0xAB, count: MediaFraming.noncePrefixBytes)
            let nonce = mediaNonce(prefix: prefix, counter: 0x01020304)
            harness.expectEqual(nonce.count, MediaFraming.nonceBytes)
            harness.expectEqual(nonce, prefix + Data([0x04, 0x03, 0x02, 0x01]))
        },
        .case("frameReaderReassemblesSplitPackets") {
            let reader = MediaFrameReader()
            let h1 = MediaHeader(streamId: 1, flags: MediaFraming.flagKeyframe,
                                 frameSeq: 0, payloadLen: 4)
            let p1 = Data([0x11, 0x22, 0x33, 0x44])
            let h2 = MediaHeader(streamId: 2, frameSeq: 7, payloadLen: 2)
            let p2 = Data([0xAA, 0xBB])
            // Packet 1 complete; packet 2's header + 1 payload byte
            // still in the reader (13 + 1 = 14 buffered bytes).
            let chunk1 = h1.serialize() + p1 + h2.serialize() + p2.subdata(in: 0..<1)
            let out1 = reader.feed(chunk1)
            harness.expectEqual(out1.count, 1)
            harness.expectEqual(out1[0].header, h1)
            harness.expectEqual(out1[0].ciphertext, p1)
            harness.expectEqual(reader.bufferedByteCount,
                                MediaFraming.headerBytes + 1)
            // The last byte completes packet 2.
            let out2 = reader.feed(p2.subdata(in: 1..<2))
            harness.expectEqual(out2.count, 1)
            harness.expectEqual(out2[0].header, h2)
            harness.expectEqual(out2[0].ciphertext, p2)
            harness.expectEqual(reader.bufferedByteCount, 0)
        },
        .case("frameReaderRecordsBadMagicMidStream") {
            let reader = MediaFrameReader()
            let header = MediaHeader(streamId: 9, frameSeq: 1, payloadLen: 1)
            var packet = Data([0xFF])
            packet.append(header.serialize().subdata(in: 1..<MediaFraming.headerBytes))
            packet.append(Data([0x00]))
            // Not thrown: the typed error is recorded in `malformed`
            // (the channel closes with it, §9.6).
            let out = reader.feed(packet)
            harness.expectEqual(out.count, 0)
            harness.expectEqual(reader.malformed,
                                MediaFramingError.badMagic(found: 0xFF))
            // Terminal: later feeds return nothing until reset().
            harness.expectEqual(reader.feed(Data([0x01])).count, 0)
            reader.reset()
            harness.expectEqual(reader.malformed, nil)
        },
        .case("frameReaderRecordsOversizedPayload") {
            let reader = MediaFrameReader()
            let header = MediaHeader(streamId: 9, frameSeq: 1,
                                     payloadLen: UInt32(MediaFraming.maxPayloadLen) + 1)
            // Not thrown: recorded in `malformed`.
            let out = reader.feed(header.serialize())
            harness.expectEqual(out.count, 0)
            harness.expectEqual(reader.malformed, .oversizedPayload(
                payloadLen: UInt32(MediaFraming.maxPayloadLen) + 1))
        },
    ]
}
