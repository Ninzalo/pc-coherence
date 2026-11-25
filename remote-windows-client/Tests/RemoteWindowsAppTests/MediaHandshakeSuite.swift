import Foundation
import CryptoKit
import Network
@testable import RemoteWindowsCore

/// §9.2 handshake (RW-C-003 test 4), loopback against the in-test
/// `FakeMediaServer` (NWListener, ephemeral 127.0.0.1 port):
/// (a) correct session key → the client completes and the server sees
/// the EXACT 32-byte HMAC (the server recomputes it — proving the
/// input format `"rwh-media-v1|" || host_nonce` over the raw
/// session-key bytes); (b) wrong key → 0x00 → typed reject;
/// (c) truncated host nonce → typed malformed-handshake failure, no
/// hang.
enum MediaHandshakeSuite {
    static let code = "RWH-HANDSHAKE"
    static let hostName = "LOOPBACK"
    static let sessionKeyHex = MediaKeys.sessionKey(code: code, hostName: hostName)
    static let goodKey = MediaKeys.rawSessionKey(code: code, hostName: hostName)
    static let badKey = Data(repeating: 0x7F, count: 32)

    static let cases: [Harness.Case] = [
        .case("handshakeAcceptedWithCorrectKey") {
            let server = FakeMediaServer(mode: .accept(sessionKey: goodKey))
            try server.start()
            defer { server.stop() }

            let state = MediaStreamState(sessionKeyHex: sessionKeyHex)
            let channel = MediaChannel(host: "127.0.0.1",
                                       port: Int(server.port),
                                       sessionKey: goodKey, state: state)
            var closedFired = false
            var closeReason: MediaChannel.ClosedReason?
            channel.onClosed = { reason in
                closedFired = true
                closeReason = reason
            }
            channel.connect()

            harness.expect(waitUntil("channel open") { channel.state == .open },
                           "handshake did not reach .open (state: \(channel.state))")
            harness.expect(server.receivedMAC != nil,
                           "server did not receive the 32-byte MAC")
            // The server recomputed the MAC with the same key: a byte
            // match proves the exact §9.2 HMAC input format.
            let expectedMAC = MediaChannel.sessionKeyMAC(
                forHostNonce: server.nonce, sessionKey: goodKey)
            harness.expectEqual(expectedMAC.count, 32)
            harness.expectEqual(server.receivedMAC, expectedMAC,
                                "handshake MAC bytes (input format proof)")

            channel.close()
            harness.expect(waitUntil(timeout: 5, "clean close") { closedFired },
                           "onClosed not fired on a clean close")
            harness.expectEqual(closeReason, nil,
                                "clean close must report a nil reason")
            harness.expectEqual(channel.state, MediaChannel.State.closed)
        },
        .case("handshakeRejectedWithWrongKey") {
            let server = FakeMediaServer(mode: .reject)
            try server.start()
            defer { server.stop() }

            let state = MediaStreamState(sessionKeyHex: sessionKeyHex)
            let channel = MediaChannel(host: "127.0.0.1",
                                       port: Int(server.port),
                                       sessionKey: badKey, state: state)
            var closedFired = false
            var closeReason: MediaChannel.ClosedReason?
            channel.onClosed = { reason in
                closedFired = true
                closeReason = reason
            }
            channel.connect()

            harness.expect(waitUntil("close after rejection") { closedFired },
                           "no close after a 0x00 handshake result")
            harness.expectEqual(closeReason,
                                MediaChannel.ClosedReason.handshakeRejected)
            harness.expectEqual(channel.state, MediaChannel.State.closed)
            // The reject server must have seen a well-formed 32-byte MAC.
            harness.expectEqual(server.receivedMAC?.count, 32)
        },
        .case("truncatedHostNonceFailsTypedNoHang") {
            let server = FakeMediaServer(mode: .truncatedNonce(bytes: 8))
            try server.start()
            defer { server.stop() }

            let state = MediaStreamState(sessionKeyHex: sessionKeyHex)
            let channel = MediaChannel(host: "127.0.0.1",
                                       port: Int(server.port),
                                       sessionKey: goodKey, state: state)
            var closedFired = false
            var closeReason: MediaChannel.ClosedReason?
            channel.onClosed = { reason in
                closedFired = true
                closeReason = reason
            }
            channel.connect()

            harness.expect(waitUntil(timeout: 10, "close after truncated nonce") {
                closedFired
            }, "truncated nonce must not hang the handshake")
            if case .malformedHandshake = closeReason {
                harness.expect(true, "typed malformedHandshake reason")
            } else {
                try harness.fail("expected malformedHandshake, got \(String(describing: closeReason))")
            }
            harness.expectEqual(channel.state, MediaChannel.State.closed)
        },
    ]
}
