import Foundation
import CryptoKit
import Network
@testable import RemoteWindowsCore

/// Full loopback receive path (RW-C-003 test 5): handshake OK, then the
/// fake server sends framed + AES-256-GCM payloads on TWO stream_ids
/// (one starting with the keyframe flag, correct per-stream counters,
/// one deliberate frame_seq gap) → the client decrypts, validates,
/// routes by stream_id, and delivers in order; then a malformed frame
/// (bad magic) → the media channel closes with the typed reason.
///
/// Scope note: the signaling session (TCP 47987) is NOT instantiated in
/// this loopback (the host's servers are later tasks); the signaling
/// connection is architecturally independent — a separate
/// `NWConnection` — so the media close cannot affect it.
enum MediaStreamSuite {
    static let code = "RWH-STREAMTEST"
    static let hostName = "LOOPBACK"
    static let sessionKeyHex = MediaKeys.sessionKey(code: code, hostName: hostName)
    static let windowA = "aaaa1111-2222-3333-4444-55555555aaaa"
    static let windowB = "bbbb1111-2222-3333-4444-55555555bbbb"

    static let cases: [Harness.Case] = [
        .case("loopbackDecryptRouteGapAndMalformedClose") {
            let sessionKey = MediaKeys.rawSessionKey(code: code, hostName: hostName)
            let server = FakeMediaServer(mode: .accept(sessionKey: sessionKey))
            try server.start()
            defer { server.stop() }

            // Client-side stream registration (control-channel stand-in).
            let state = MediaStreamState(sessionKeyHex: sessionKeyHex)
            let entryA = try state.register(windowID: windowA)
            let entryB = try state.register(windowID: windowB)

            // Server side: the same §9.3 derivations (both peers derive
            // from the one session key) + per-stream ciphers.
            let cipherA = MediaStreamCipher(
                keyData: try MediaKeys.mediaKey(sessionKeyHex: sessionKeyHex,
                                                windowID: windowA),
                noncePrefix: try MediaKeys.noncePrefix(sessionKeyHex: sessionKeyHex,
                                                       windowID: windowA))
            let cipherB = MediaStreamCipher(
                keyData: try MediaKeys.mediaKey(sessionKeyHex: sessionKeyHex,
                                                windowID: windowB),
                noncePrefix: try MediaKeys.noncePrefix(sessionKeyHex: sessionKeyHex,
                                                       windowID: windowB))

            // Frame plan with per-stream counters. A: 0 (keyframe), 1,
            // 3 (gap: seq 2 never arrives), 4. B: 0 (keyframe), 1.
            let plan: [(windowID: String, cipher: MediaStreamCipher,
                        seq: UInt32, keyframe: Bool)] = [
                (windowA, cipherA, 0, true),
                (windowA, cipherA, 1, false),
                (windowA, cipherA, 3, false),
                (windowA, cipherA, 4, false),
                (windowB, cipherB, 0, true),
                (windowB, cipherB, 1, false),
            ]
            var expectedPayloads: [Data] = []
            var stream = Data()
            for (i, frame) in plan.enumerated() {
                let plaintext = Data((0..<32).map { UInt8(($0 &* 7) &+ i) })
                expectedPayloads.append(plaintext)
                // §9.4: the sender encrypts with packet_counter ==
                // frame_seq (the receiver reconstructs the nonce counter
                // from the header). The explicit-seal form is required
                // for the gap frames: the auto-increment counter would
                // diverge from the header seq (2 vs 3) and the client
                // would correctly reject the tag.
                let ct = try frame.cipher.seal(plaintext, frameSeq: frame.seq)
                let header = MediaHeader(
                    streamId: MediaFraming.streamID(forWindowID: frame.windowID),
                    flags: frame.keyframe ? MediaFraming.flagKeyframe : 0,
                    frameSeq: frame.seq,
                    payloadLen: UInt32(ct.count))
                stream.append(header.serialize())
                stream.append(ct)
            }
            // §9.6 malformed packet: bad magic → the client MUST close.
            var malformed = Data([0xFF])
            malformed.append(Data(repeating: 0, count: MediaFraming.headerBytes - 1))
            malformed.append(Data([0x00, 0x00]))

            server.onChannelOpen = { conn in
                // Send the valid frames in two writes (the split lands
                // inside the second packet), then the malformed one.
                let split = MediaFraming.headerBytes + 40
                conn.send(content: stream.subdata(in: 0..<split),
                          completion: .contentProcessed { _ in
                    conn.send(content: stream.subdata(in: split..<stream.count),
                              completion: .contentProcessed { _ in
                        conn.send(content: malformed,
                                  completion: .contentProcessed { _ in })
                    })
                })
            }

            var received: [MediaChannel.MediaFrame] = []
            let lock = NSLock()
            var closedFired = false
            var closeReason: MediaChannel.ClosedReason?
            let channel = MediaChannel(host: "127.0.0.1",
                                       port: Int(server.port),
                                       sessionKey: sessionKey, state: state)
            channel.onFrame = { frame in
                lock.lock()
                received.append(frame)
                lock.unlock()
            }
            channel.onClosed = { reason in
                closedFired = true
                closeReason = reason
            }
            channel.connect()

            harness.expect(waitUntil(timeout: 15, "6 frames + typed close") {
                lock.lock()
                let n = received.count
                lock.unlock()
                return closedFired && n >= 6
            }, "expected all frames delivered, then the malformed close")

            lock.lock()
            let frames = received
            lock.unlock()
            harness.expectEqual(frames.count, 6, "delivered frame count")

            // Routed by stream_id, in per-stream order.
            let aSeqs = frames.filter { $0.windowID == windowA }.map(\.frameSeq)
            let bSeqs = frames.filter { $0.windowID == windowB }.map(\.frameSeq)
            harness.expectEqual(aSeqs, [0, 1, 3, 4], "window A frame_seq order")
            harness.expectEqual(bSeqs, [0, 1], "window B frame_seq order")

            // Per-frame assertions are guarded by the exact count
            // checked above: the expectEqual already records the
            // mismatch, and indexing a short array would trap the whole
            // test process (FR-13: malformed input never crashes).
            if frames.count == 6 {
                harness.expectEqual(frames[0].isKeyframe, true)
                harness.expectEqual(frames[1].isKeyframe, false)
                harness.expectEqual(frames[4].isKeyframe, true)

                // Decryption proof: every payload byte-exact.
                for (i, frame) in frames.enumerated() {
                    harness.expectEqual(frame.payload, expectedPayloads[i],
                                        "frame \(i) decrypted payload")
                }
            }

            // §9.4 gap handling: A skipped seq 2 — counted, stream kept.
            harness.expectEqual(entryA.gapCount, 1, "window A gap count")
            harness.expectEqual(entryA.frameCount, 4, "window A frame count")
            harness.expectEqual(entryB.gapCount, 0)
            harness.expectEqual(entryB.frameCount, 2)

            // The bad magic closed the channel with the typed reason.
            switch closeReason {
            case .malformedHeader:
                harness.expect(true, "typed malformedHeader close")
            default:
                try harness.fail("expected malformedHeader close, got \(String(describing: closeReason))")
            }
            harness.expectEqual(channel.state, MediaChannel.State.closed)
        },
        .case("unknownStreamClosesTyped") {
            // A packet for a stream_id with no registered window is a
            // §9.6 violation → typed close (no GCM involved).
            let sessionKey = MediaKeys.rawSessionKey(code: code, hostName: hostName)
            let server = FakeMediaServer(mode: .accept(sessionKey: sessionKey))
            try server.start()
            defer { server.stop() }

            let state = MediaStreamState(sessionKeyHex: sessionKeyHex)
            _ = try state.register(windowID: windowA)   // only A registered

            let foreignID = UInt16(0xBEEF)   // not streamID(windowA/B)
            var packet = MediaHeader(streamId: foreignID, frameSeq: 0,
                                     payloadLen: 3).serialize()
            packet.append(Data([0x01, 0x02, 0x03]))

            server.onChannelOpen = { conn in
                conn.send(content: packet, completion: .contentProcessed { _ in })
            }

            var closedFired = false
            var closeReason: MediaChannel.ClosedReason?
            let channel = MediaChannel(host: "127.0.0.1",
                                       port: Int(server.port),
                                       sessionKey: sessionKey, state: state)
            var framesSeen = 0
            channel.onFrame = { _ in framesSeen += 1 }
            channel.onClosed = { reason in
                closedFired = true
                closeReason = reason
            }
            channel.connect()

            harness.expect(waitUntil("close on unknown stream") { closedFired },
                           "unknown stream_id must close the channel")
            switch closeReason {
            case .unknownStream(let id):
                harness.expectEqual(id, foreignID)
            default:
                try harness.fail("expected unknownStream close, got \(String(describing: closeReason))")
            }
            harness.expectEqual(framesSeen, 0, "no frame may be delivered")
        },
    ]
}
