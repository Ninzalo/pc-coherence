import Foundation
import CryptoKit
import Network

/// PROTOCOL.md §9.2 — the media-channel handshake + receive path.
///
/// Handshake (Client = initiator, Host = responder), over TCP 47988:
/// 1. Client connects.
/// 2. Host sends 16 random bytes (`host_nonce`).
/// 3. Client replies with 32 bytes =
///    `HMAC-SHA256(session_key_bytes, "rwh-media-v1|" || host_nonce)`,
///    where `session_key_bytes` are the 32 RAW bytes of the paired
///    session key (§9.3) and `||` is byte concatenation.
/// 4. Host answers `0x01` (accept) or `0x00` (reject → close).
///
/// The handshake proves knowledge of a valid session key without
/// transmitting it. The session key, nonces, and all frame payloads are
/// NEVER logged (content policy): only machine IDs (stream_id,
/// frame_seq), durations, and error counts may be surfaced.
///
/// After the handshake the channel is a byte stream of §9.4 packets:
/// the receive loop parses headers, decrypts each payload with the
/// stream's cipher (`MediaStreamState`), and delivers `MediaFrame`s.
/// Any §9.6 condition (malformed magic/version, oversized payload, bad
/// GCM tag, unregistered stream, reset) closes the media channel with a
/// typed reason. The signaling session (47987) is a separate connection
/// and is NOT affected.
public final class MediaChannel {
    /// The Host listens on TCP 47988 (PROTOCOL §9.2).
    public static let defaultPort = 47988

    public enum State: Equatable {
        case idle
        case handshaking
        case open
        case closed
    }

    /// Typed close reasons (§9.6). `nil` in `onClosed` = clean
    /// self-initiated close.
    public enum ClosedReason: Error, Equatable {
        case connectFailed(String)
        case handshakeRejected
        case malformedHandshake(String)
        case connectionClosed(String)
        case malformedHeader(String)
        case unknownStream(streamId: UInt16)
        case authenticationFailed(streamId: UInt16)
    }

    /// One decrypted video frame, routed to its window.
    public struct MediaFrame: Equatable {
        public let windowID: String
        public let streamID: UInt16
        public let frameSeq: UInt32
        public let isKeyframe: Bool
        public let payload: Data   // decrypted H.264 NAL bytes — never logged

        public init(windowID: String, streamID: UInt16, frameSeq: UInt32,
                    isKeyframe: Bool, payload: Data) {
            self.windowID = windowID
            self.streamID = streamID
            self.frameSeq = frameSeq
            self.isKeyframe = isKeyframe
            self.payload = payload
        }
    }

    public private(set) var state: State = .idle
    /// Fired for every decrypted frame, on the channel's queue (hop to
    /// the main queue for UI work).
    public var onFrame: ((MediaFrame) -> Void)?
    /// Fired at most once per connection: `nil` = clean self-initiated
    /// close, a `ClosedReason` = a §9.6 failure. Delivered on the
    /// channel's queue.
    public var onClosed: ((ClosedReason?) -> Void)?

    private let host: String
    private let port: Int
    private let rawSessionKey: Data   // 32 raw bytes — never logged
    private let streamState: MediaStreamState
    private let queue: DispatchQueue
    private var connection: NWConnection?
    private var frameReader = MediaFrameReader()
    private var closedReported = false
    private var closedByUs = false

    public init(host: String, port: Int = MediaChannel.defaultPort,
                sessionKey: Data, state: MediaStreamState,
                queue: DispatchQueue? = nil) {
        self.host = host
        self.port = port
        self.rawSessionKey = sessionKey
        self.streamState = state
        self.queue = queue ?? DispatchQueue(label: "remotewindows.media.channel")
    }

    /// Connect and run the §9.2 handshake. One channel = one
    /// connection; reconnect = a fresh `MediaChannel` (re-handshake per
    /// §9.6 is the owner's job — the app restarts the channel on
    /// signaling re-pairing).
    public func connect() {
        guard state == .idle else { return }
        queue.async { [weak self] in
            guard let self, self.state == .idle else { return }
            self.state = .handshaking
            self.startConnection()
        }
    }

    /// Tear down: cancels the connection; `onClosed(nil)`.
    public func close() {
        queue.async { [weak self] in
            guard let self, self.state != .closed else { return }
            self.closedByUs = true
            self.state = .closed
            self.connection?.cancel()
            self.connection = nil
            self.reportClosed(nil)
        }
    }

    // MARK: Handshake (all on `queue`)

    private func startConnection() {
        guard let portValue = NWEndpoint.Port(rawValue: UInt16(port)) else {
            state = .closed
            reportClosed(.connectFailed("port out of range"))
            return
        }
        let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(host),
                                           port: portValue)
        let connection = NWConnection(to: endpoint, using: .tcp)
        self.connection = connection
        connection.stateUpdateHandler = { [weak self, weak connection] state in
            guard let self, let connection, self.connection === connection else { return }
            switch state {
            case .ready:
                self.readHostNonce(connection)
            case .failed(let error):
                self.failConnection(.connectFailed(error.localizedDescription))
            case .cancelled:
                break // handled by close()/failConnection
            case .waiting, .setup, .preparing:
                break
            @unknown default:
                break
            }
        }
        connection.start(queue: queue)
    }

    private func readHostNonce(_ connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 16, maximumLength: 16) {
            [weak self] data, _, isComplete, error in
            guard let self, self.connection === connection else { return }
            if let error {
                self.failConnection(.connectFailed(error.localizedDescription))
                return
            }
            guard let data, data.count == 16 else {
                // Truncated host nonce (peer closed early): malformed
                // handshake step, not a clean close (§9.2).
                self.failConnection(.malformedHandshake("truncated host nonce"))
                return
            }
            self.sendHandshakeMAC(connection, hostNonce: data)
        }
    }

    /// §9.2 step 3: HMAC-SHA256 over `session_key_bytes` (the 32 RAW
    /// session-key bytes) of `"rwh-media-v1|" || host_nonce`.
    /// `sessionKeyMAC(forHostNonce:)` is exposed so the Host's exact
    /// input format is testable (in-test fake servers compute the same
    /// value — the cross-language handshake proof).
    public static func sessionKeyMAC(forHostNonce hostNonce: Data,
                                     sessionKey: Data) -> Data {
        let message = Data("rwh-media-v1|".utf8) + hostNonce
        let mac = HMAC<SHA256>.authenticationCode(
            for: message, using: SymmetricKey(data: sessionKey))
        return Data(mac)
    }

    private func sendHandshakeMAC(_ connection: NWConnection, hostNonce: Data) {
        let mac = Self.sessionKeyMAC(forHostNonce: hostNonce, sessionKey: rawSessionKey)
        connection.send(content: mac, completion: .contentProcessed { [weak self] error in
            guard let self, self.connection === connection else { return }
            if let error {
                self.failConnection(.connectFailed(error.localizedDescription))
                return
            }
            self.readHandshakeResult(connection)
        })
    }

    private func readHandshakeResult(_ connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 1) {
            [weak self] data, _, isComplete, error in
            guard let self, self.connection === connection else { return }
            if let error {
                self.failConnection(.connectionClosed(error.localizedDescription))
                return
            }
            guard let data, !data.isEmpty else {
                // Host closed before/without the result byte.
                self.failConnection(.malformedHandshake("missing result byte"))
                return
            }
            switch data.first {
            case 0x01:
                self.state = .open
                self.receiveLoop(connection)
            case 0x00:
                self.failConnection(.handshakeRejected)
            case let byte?:
                self.failConnection(.malformedHandshake("unexpected result byte \(byte)"))
            case nil:
                self.failConnection(isComplete ? .malformedHandshake("missing result byte")
                                               : .connectionClosed("closed during handshake"))
            }
        }
    }

    // MARK: Frame receive (all on `queue`)

    private func receiveLoop(_ connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1,
                           maximumLength: 65536) {
            [weak self] data, _, isComplete, error in
            guard let self, self.connection === connection else { return }
            if let data, !data.isEmpty {
                self.handleData(data)
            }
            if self.state == .closed { return }
            if let error {
                self.failConnection(.connectionClosed(error.localizedDescription))
            } else if isComplete {
                self.failConnection(.connectionClosed("peer closed the media channel"))
            } else {
                self.receiveLoop(connection)
            }
        }
    }

    private func handleData(_ data: Data) {
        let packets = frameReader.feed(data)
        for packet in packets {
            do {
                try process(packet)
            } catch let unknown as MediaUnknownStream {
                failConnection(.unknownStream(streamId: unknown.streamID))
                return
            } catch let aead as MediaAead.MediaAeadError {
                // Bad GCM tag: the packet is discarded and the
                // connection reset (§9.6) — never a crash (FR-13).
                _ = aead
                failConnection(.authenticationFailed(
                    streamId: packet.header.streamId))
                return
            } catch {
                // Defensive: process() only throws the two typed
                // errors above; treat anything else as a connection
                // failure, never a crash (FR-13).
                failConnection(.connectionClosed(
                    "frame processing failure (stream \(packet.header.streamId))"))
                return
            }
            if state == .closed { return }
        }
        // A terminal malformed header seen in this feed (or earlier):
        // every valid packet before it has already been delivered
        // above; now reset the connection with the typed reason (§9.6).
        if let malformed = frameReader.malformed {
            failConnection(.malformedHeader(describe(malformed)))
        }
    }

    private func process(_ packet: (header: MediaHeader, ciphertext: Data)) throws {
        do {
            let result = try streamState.processFrame(
                streamID: packet.header.streamId,
                frameSeq: packet.header.frameSeq,
                isKeyframe: packet.header.isKeyframe,
                ciphertext: packet.ciphertext)
            let frame = MediaFrame(windowID: result.windowID,
                                   streamID: packet.header.streamId,
                                   frameSeq: packet.header.frameSeq,
                                   isKeyframe: result.isKeyframe,
                                   payload: result.plaintext)
            onFrame?(frame)
        } catch let unknown as MediaUnknownStream {
            failConnection(.unknownStream(streamId: unknown.streamID))
        } catch let aead as MediaAead.MediaAeadError {
            // Bad GCM tag: the packet is discarded and the connection
            // reset (§9.6) — never a crash (FR-13).
            _ = aead
            failConnection(.authenticationFailed(streamId: packet.header.streamId))
        }
    }

    private func describe(_ error: MediaFramingError) -> String {
        switch error {
        case .shortBuffer(let n): return "short header buffer (\(n) bytes)"
        case .badMagic(let b): return "bad magic 0x\(String(format: "%02X", b))"
        case .badVersion(let v): return "bad version \(v)"
        case .oversizedPayload(let n): return "payload_len \(n) exceeds 16 MiB"
        }
    }

    private func failConnection(_ reason: ClosedReason) {
        guard state != .closed else { return }
        state = .closed
        connection?.cancel()
        connection = nil
        reportClosed(reason)
    }

    private func reportClosed(_ reason: ClosedReason?) {
        guard !closedReported else { return }
        closedReported = true
        onClosed?(reason)
    }
}
