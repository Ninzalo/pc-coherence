import Foundation
import CryptoKit
import Network
@testable import RemoteWindowsCore

/// In-test loopback media server: an `NWListener` on 127.0.0.1 with an
/// EPHEMERAL port (the media-side mirror of SignalingClientSuite's
/// `FakeTransport`). The Windows Host's media server does not exist
/// yet (a later host task), so every server behavior here is a
/// scripted §9.2/§9.4 stub — never a fake 'demo stream' in the UI.
final class FakeMediaServer {
    enum Mode {
        /// §9.2: verify the client's MAC with `sessionKey`, reply 0x01.
        case accept(sessionKey: Data)
        /// Always reply 0x00 (wrong-key case).
        case reject
        /// Send only N of the 16 nonce bytes, then close (malformed
        /// handshake step).
        case truncatedNonce(bytes: Int)
    }

    private(set) var port: UInt16 = 0
    private(set) var receivedMAC: Data?
    /// The 16-byte host nonce the server sends (deterministic bytes).
    let nonce = Data((0..<16).map { UInt8($0) })

    /// Fired (on the server queue) once the handshake completed with an
    /// accept — the stream suite injects its frames here.
    var onChannelOpen: ((NWConnection) -> Void)?

    private var listener: NWListener?
    private let mode: Mode
    private let serverQueue = DispatchQueue(label: "test.media.fakeserver")
    private let readySemaphore = DispatchSemaphore(value: 0)
    private var startFailure: Error?

    init(mode: Mode) { self.mode = mode }

    /// Blocks until the listener reaches `.ready` (ephemeral port
    /// known) or fails. The 27.2 Network overlay: `NWListener`'s init
    /// throws, `start(queue:)` takes no completion, and the bound port
    /// is read from `listener.port` once the state is `.ready`.
    func start() throws {
        let listener = try NWListener(using: .tcp, on: .any)
        self.listener = listener
        listener.newConnectionHandler = { [weak self] conn in
            self?.handle(conn)
        }
        listener.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            switch state {
            case .ready:
                self.port = self.listener?.port?.rawValue ?? 0
                self.readySemaphore.signal()
            case .failed(let error):
                self.startFailure = error
                self.readySemaphore.signal()
            case .setup, .waiting, .cancelled:
                break
            @unknown default:
                break
            }
        }
        listener.start(queue: serverQueue)
        readySemaphore.wait()
        if let startFailure { throw startFailure }
    }

    func stop() { listener?.cancel() }

    private func handle(_ conn: NWConnection) {
        conn.start(queue: serverQueue)
        conn.stateUpdateHandler = { [weak self] state in
            guard let self, case .ready = state else { return }
            self.runHandshake(conn)
        }
    }

    private func runHandshake(_ conn: NWConnection) {
        if case .truncatedNonce(let n) = mode {
            conn.send(content: nonce.subdata(in: 0..<n),
                      completion: .contentProcessed { _ in conn.cancel() })
            return
        }
        conn.send(content: nonce, completion: .contentProcessed { [weak self] _ in
            guard let self else { return }
            conn.receive(minimumIncompleteLength: 32, maximumLength: 32) { [weak self] data, _, _, _ in
                guard let self else { return }
                self.receivedMAC = data
                var result: UInt8 = 0x00
                if case .accept(let sessionKey) = self.mode {
                    let expected = MediaChannel.sessionKeyMAC(
                        forHostNonce: self.nonce, sessionKey: sessionKey)
                    result = (data == expected) ? 0x01 : 0x00
                }
                conn.send(content: Data([result]),
                          completion: .contentProcessed { [weak self] _ in
                    if result == 0x01, case .accept = self?.mode {
                        self?.onChannelOpen?(conn)
                    }
                })
            }
        })
    }
}

/// Spin the caller's RunLoop until `condition` holds or `timeout`
/// elapses. Network callbacks run on their own queues, so the suites
/// only poll.
func waitUntil(timeout: TimeInterval = 10,
               _ label: String, _ condition: () -> Bool) -> Bool {
    let deadline = Date().addingTimeInterval(timeout)
    while !condition() {
        if Date() >= deadline {
            print("  [waitUntil] timed out: \(label)")
            return false
        }
        RunLoop.current.run(until: Date().addingTimeInterval(0.02))
    }
    return true
}
