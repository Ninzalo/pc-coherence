import Foundation
@testable import RemoteWindowsCore

// MARK: - Fakes (in-memory transport, timer, clock)

/// In-memory `SignalingTransport`: records what the client sent and lets
/// tests fire ready/close and deliver framed envelopes. Reuses the Core
/// `Framing` + `ProtocolCodec` for both directions.
final class FakeTransport: SignalingTransport {
    var onData: ((Data) -> Void)?
    var onReady: (() -> Void)?
    var onClosed: ((Error?) -> Void)?

    private(set) var connectedEndpoints: [(host: String, port: Int)] = []
    private(set) var sentFrames: [Data] = []
    private(set) var closedCount = 0

    var sendCount: Int { sentFrames.count }

    func connect(host: String, port: Int) {
        connectedEndpoints.append((host: host, port: port))
    }

    func send(_ bytes: Data) {
        sentFrames.append(bytes)
    }

    func close() {
        closedCount += 1
    }

    // Test drivers
    func fireReady() { onReady?() }
    func fireClosed(_ error: Error? = nil) { onClosed?(error) }

    /// Deliver one framed envelope, as the Host would.
    func deliver(_ envelope: Envelope) throws {
        let json = try ProtocolCodec.encode(envelope)
        let frame = try Framing.encode(json)
        onData?(frame)
    }

    /// Deliver raw bytes (for malformed-frame tests).
    func deliverRaw(_ bytes: Data) { onData?(bytes) }

    /// Decode every frame the client sent (Core framing + codec reuse).
    var sentEnvelopes: [Envelope] {
        var out: [Envelope] = []
        for frame in sentFrames {
            guard let payloads = try? FrameReader().feed(frame),
                  let payload = payloads.first,
                  let envelope = try? ProtocolCodec.decodeEnvelope(payload) else {
                out.append(.malformed)
                continue
            }
            out.append(envelope)
        }
        return out
    }
}

/// Marker for a frame the client could not decode (test bookkeeping only).
extension Envelope {
    static var malformed: Envelope {
        Envelope(kind: .heartbeat, payload: .heartbeat(Heartbeat(senderTimeMs: -1)))
    }
}

final class FakeSignalingTimer: SignalingTimer {
    var onTick: (() -> Void)?
    private(set) var startedIntervalSeconds: Double?
    private(set) var isRunning = false
    private(set) var tickCount = 0

    func start(intervalSeconds: Double) {
        startedIntervalSeconds = intervalSeconds
        isRunning = true
    }

    func stop() {
        isRunning = false
    }

    /// Deterministic tick (a no-op once stopped).
    func tick() {
        guard isRunning else { return }
        tickCount += 1
        onTick?()
    }
}

final class FakeSignalingClock: SignalingClock {
    var now: Int64 = 1_700_000_000_000
    func nowMs() -> Int64 { now }
}

// MARK: - Session helper

struct FakeSession {
    let transport: FakeTransport
    let timer: FakeSignalingTimer
    let clock: FakeSignalingClock
    let client: SignalingClient

    static let testCode = "123456"
    static let testNonce = "0123456789abcdef0123456789abcdef"
    static let testHostName = "DESKTOP-PC"

    static func hostHello() -> Envelope {
        Envelope(kind: .hello, payload: .hello(Hello(
            productName: "windows-host",
            appVersion: "0.1.0",
            capabilities: [.appCatalog, .launch, .multiWindow, .input,
                           .resize, .power, .auxiliaryWindows])))
    }

    static func challenge() -> Envelope {
        Envelope(kind: .pairingChallenge, payload: .pairingChallenge(PairingChallenge(
            hostName: testHostName,
            challengeNonce: testNonce)))
    }

    static func pairingResult(accepted: Bool, errorCode: String = "",
                              safeMessage: String = "") -> Envelope {
        Envelope(kind: .pairingResult,
                 payload: .pairingResult(PairingResult(
                     accepted: accepted, errorCode: errorCode,
                     safeMessage: safeMessage)))
    }

    static func make(heartbeatIntervalMs: Int64 = 5000) -> FakeSession {
        let transport = FakeTransport()
        let timer = FakeSignalingTimer()
        let clock = FakeSignalingClock()
        let client = SignalingClient(
            transport: transport,
            clock: clock,
            timerFactory: { timer },
            config: .init(appVersion: "0.1.0",
                          heartbeatIntervalMs: heartbeatIntervalMs))
        return FakeSession(transport: transport, timer: timer,
                           clock: clock, client: client)
    }

    /// connect → ready → host HELLO → challenge. Returns with `.pairing`.
    func driveToPairing(host: String = "192.168.1.50", port: Int = 47987) throws {
        client.connect(host: host, port: port)
        transport.fireReady()
        try transport.deliver(FakeSession.hostHello())
        try transport.deliver(FakeSession.challenge())
    }
}

/// Poll `condition` for up to `timeout` seconds (loopback tests only).
func waitFor(timeout: TimeInterval, _ condition: () -> Bool) -> Bool {
    let deadline = Date().addingTimeInterval(timeout)
    while Date() < deadline {
        if condition() { return true }
        Thread.sleep(forTimeInterval: 0.05)
    }
    return condition()
}

// MARK: - Loopback in-test Host (POSIX, 127.0.0.1 only)

/// Minimal in-test Windows Host for loopback integration tests: a POSIX
/// TCP listener on 127.0.0.1 (ephemeral port) that speaks the exact
/// verified handshake — HELLO reply + challenge, pairing verification
/// (code + nonce echo), app_catalog (empty entries, the honest current
/// host state), and heartbeat echo. No LAN, no external network.
final class LoopbackHost {
    struct Failure: Error { let message: String }

    let port: UInt16
    let hostName = "DESKTOP-PC"
    let challengeNonce = "0123456789abcdef0123456789abcdef"
    let code = "246810"

    private let listenFD: Int32
    private let lock = NSLock()
    private var _accepted = false
    private var _openConnections = 0
    private var _heartbeatCount = 0
    private var _catalogRequestCount = 0
    private var _pairingAttempts: [String?] = []
    private var _receivedKinds: [String] = []
    private var thread: Thread?

    var accepted: Bool { lock.lock(); defer { lock.unlock() }; return _accepted }
    var openConnections: Int { lock.lock(); defer { lock.unlock() }; return _openConnections }
    var heartbeatCount: Int { lock.lock(); defer { lock.unlock() }; return _heartbeatCount }
    var catalogRequestCount: Int { lock.lock(); defer { lock.unlock() }; return _catalogRequestCount }
    var pairingAttempts: [String?] { lock.lock(); defer { lock.unlock() }; return _pairingAttempts }
    var receivedKinds: [String] { lock.lock(); defer { lock.unlock() }; return _receivedKinds }

    init() throws {
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { throw Failure(message: "socket() failed: \(errno)") }
        var yes: Int32 = 1
        _ = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                       socklen_t(MemoryLayout<Int32>.size))
        var addr = sockaddr_in()
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = 0
        addr.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))
        let bindResult = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self,
                                  capacity: MemoryLayout<sockaddr_in>.size) { sockPtr in
                bind(fd, sockPtr, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0 else {
            close(fd)
            throw Failure(message: "bind() failed: \(errno)")
        }
        guard listen(fd, 8) == 0 else {
            close(fd)
            throw Failure(message: "listen() failed: \(errno)")
        }
        var len = socklen_t(MemoryLayout<sockaddr_in>.size)
        var bound = sockaddr_in()
        guard withUnsafeMutablePointer(to: &bound) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self,
                                  capacity: MemoryLayout<sockaddr_in>.size) { sockPtr in
                getsockname(fd, sockPtr, &len)
            }
        } == 0 else {
            close(fd)
            throw Failure(message: "getsockname() failed: \(errno)")
        }
        listenFD = fd
        port = UInt16(bigEndian: bound.sin_port)
        let t = Thread { [weak self] in self?.serve() }
        t.name = "rw-test.loopback-host"
        thread = t
        t.start()
    }

    deinit { shutdown() }

    func shutdown() {
        _ = close(listenFD)
    }

    private func serve() {
        while true {
            var peerAddr = sockaddr()
            var peerLen = socklen_t(MemoryLayout<sockaddr>.size)
            let clientFD = accept(listenFD, &peerAddr, &peerLen)
            if clientFD < 0 { return } // listen socket closed
            lock.lock()
            _accepted = true
            _openConnections += 1
            lock.unlock()
            handleClient(clientFD)
            lock.lock()
            _openConnections -= 1
            lock.unlock()
            _ = Darwin.close(clientFD)
        }
    }

    private func handleClient(_ fd: Int32) {
        let reader = FrameReader()
        var buffer = [UInt8](repeating: 0, count: 65536)
        while true {
            let n = buffer.withUnsafeMutableBytes { ptr in
                read(fd, ptr.baseAddress, ptr.count)
            }
            if n <= 0 { break }
            do {
                let frames = try reader.feed(Data(buffer[0..<n]))
                for frame in frames {
                    let envelope = try ProtocolCodec.decodeEnvelope(frame)
                    lock.lock()
                    _receivedKinds.append(envelope.kind.rawValue)
                    lock.unlock()
                    respond(to: envelope, on: fd)
                }
            } catch {
                break // malformed peer data: stop
            }
        }
    }

    private func respond(to envelope: Envelope, on fd: Int32) {
        switch envelope.payload {
        case .hello:
            // Host HELLO (canonical shape) + fresh challenge.
            sendFrame(FakeSession.hostHello(), on: fd)
            sendFrame(FakeSession.challenge(), on: fd)
        case .pairingChallenge(let pc):
            lock.lock()
            _pairingAttempts.append(pc.oneTimeCode)
            lock.unlock()
            let ok = pc.oneTimeCode == code && pc.challengeNonce == challengeNonce
            sendFrame(FakeSession.pairingResult(
                accepted: ok,
                errorCode: ok ? "" : "invalid_code",
                safeMessage: ok ? "" : "Pairing code was not accepted"), on: fd)
        case .appCatalogRequest:
            lock.lock()
            _catalogRequestCount += 1
            lock.unlock()
            // Empty entries: the catalog scanner is a later host task.
            sendFrame(Envelope(kind: .appCatalog, requestID: envelope.requestID,
                               payload: .appCatalog(AppCatalog(catalogRevision: 1,
                                                               entries: []))), on: fd)
        case .heartbeat:
            lock.lock()
            _heartbeatCount += 1
            lock.unlock()
            sendFrame(Envelope(kind: .heartbeat,
                               payload: .heartbeat(Heartbeat(senderTimeMs: 42))), on: fd)
        default:
            break
        }
    }

    private func sendFrame(_ envelope: Envelope, on fd: Int32) {
        guard let json = try? ProtocolCodec.encode(envelope),
              let frame = try? Framing.encode(json) else { return }
        frame.withUnsafeBytes { ptr in
            var sent = 0
            while sent < frame.count {
                let written = send(fd, ptr.baseAddress!.advanced(by: sent),
                                   frame.count - sent, 0)
                if written <= 0 { return }
                sent += written
            }
        }
    }
}

// MARK: - Suite

enum SignalingClientSuite {
    static let cases: [Harness.Case] = [
        .case("initialStateIsDisconnected") {
            let s = FakeSession.make()
            harness.expectEqual(s.client.state, SignalingState.disconnected)
            harness.expect(s.client.lastActivityMs == nil)
        },
        .case("connectSendsHelloThenAdvancesToPairingOnChallenge") {
            let s = FakeSession.make()
            s.client.connect(host: "10.0.0.7", port: 47987)
            harness.expectEqual(s.client.state, SignalingState.connecting)
            harness.expectEqual(s.transport.sendCount, 0,
                                "no traffic before the transport is ready")
            harness.expectEqual(s.transport.connectedEndpoints.first?.host, "10.0.0.7")
            harness.expectEqual(s.transport.connectedEndpoints.first?.port, 47987)
            s.transport.fireReady()
            let hello = s.transport.sentEnvelopes.last
            harness.expectEqual(hello?.kind, RWKind.hello)
            if case .hello(let m)? = hello?.payload {
                harness.expectEqual(m.productName, "macos-client")
                harness.expectEqual(m.capabilities,
                                    [RWCapability.appCatalog, .input, .resize])
            } else {
                try harness.fail("expected the client HELLO payload")
            }
            // Host HELLO alone does not pair.
            try s.transport.deliver(FakeSession.hostHello())
            harness.expectEqual(s.client.state, SignalingState.connecting)
            harness.expectEqual(s.client.hostHello?.productName, "windows-host")
            // Challenge moves the session to pairing.
            try s.transport.deliver(FakeSession.challenge())
            harness.expectEqual(s.client.state, SignalingState.pairing)
            harness.expectEqual(s.client.hostName, FakeSession.testHostName)
            harness.expectEqual(s.client.challenge?.challengeNonce,
                                FakeSession.testNonce)
        },
        .case("pairingSuccessAutoRequestsCatalogAndStoresEmptyCatalog") {
            let s = FakeSession.make()
            var catalogEvents: [AppCatalog] = []
            s.client.onCatalog = { catalogEvents.append($0) }
            try s.driveToPairing()
            harness.expect(s.client.submitPairingCode(FakeSession.testCode),
                           "valid 6-digit code must be accepted")
            if case .pairingChallenge(let pc)? = s.transport.sentEnvelopes.last?.payload {
                harness.expectEqual(pc.oneTimeCode, FakeSession.testCode)
                harness.expectEqual(pc.challengeNonce, FakeSession.testNonce,
                                    "must echo the host nonce")
                harness.expectEqual(pc.hostName, FakeSession.testHostName,
                                    "must echo the host name")
            } else {
                try harness.fail("expected the pairing_challenge payload")
            }
            harness.expectEqual(s.client.state, SignalingState.pairing,
                                "not paired until the host accepts")
            try s.transport.deliver(FakeSession.pairingResult(accepted: true))
            harness.expectEqual(s.client.state, SignalingState.paired)
            let request = s.transport.sentEnvelopes.last
            harness.expectEqual(request?.kind, RWKind.appCatalogRequest,
                                "paired must auto-send app_catalog_request")
            harness.expect(request?.requestID != nil,
                           "app_catalog_request carries a request_id")
            if case .appCatalogRequest(let m)? = request?.payload {
                harness.expectEqual(m.forceRescan, false)
            }
            try s.transport.deliver(Envelope(
                kind: .appCatalog, requestID: request?.requestID,
                payload: .appCatalog(AppCatalog(catalogRevision: 1, entries: []))))
            harness.expectEqual(s.client.catalog?.catalogRevision, UInt32(1))
            harness.expectEqual(s.client.catalog?.entries.count, 0,
                                "empty catalog is the honest current host state")
            harness.expectEqual(catalogEvents.count, 1, "onCatalog fired once")
        },
        .case("wrongCodeRejectedWithHostMessageThenRetrySucceeds") {
            let s = FakeSession.make()
            var results: [PairingResult] = []
            s.client.onPairingResult = { results.append($0) }
            try s.driveToPairing()
            harness.expect(s.client.submitPairingCode("654321"))
            try s.transport.deliver(FakeSession.pairingResult(
                accepted: false, errorCode: "invalid_code",
                safeMessage: "Pairing code was not accepted"))
            harness.expectEqual(s.client.state, SignalingState.rejected)
            harness.expectEqual(s.client.lastPairingResult?.errorCode, "invalid_code")
            harness.expectEqual(s.client.lastPairingResult?.safeMessage,
                                "Pairing code was not accepted")
            // Retry works from the rejected state.
            harness.expect(s.client.submitPairingCode(FakeSession.testCode))
            try s.transport.deliver(FakeSession.pairingResult(accepted: true))
            harness.expectEqual(s.client.state, SignalingState.paired)
            harness.expectEqual(results.count, 2, "both results surfaced")
        },
        .case("lockedCodeEntersLockedStateWithoutAutoRetry") {
            let s = FakeSession.make()
            try s.driveToPairing()
            harness.expect(s.client.submitPairingCode("000000"))
            let sendsBefore = s.transport.sendCount
            try s.transport.deliver(FakeSession.pairingResult(
                accepted: false, errorCode: "locked",
                safeMessage: "Too many attempts; pairing is temporarily locked"))
            harness.expectEqual(s.client.state, SignalingState.locked)
            harness.expectEqual(s.client.lastPairingResult?.errorCode, "locked")
            harness.expectEqual(s.transport.sendCount, sendsBefore,
                                "no automatic retry after locked")
            // Explicit user retry is allowed (never spam).
            harness.expect(s.client.submitPairingCode(FakeSession.testCode))
            harness.expectEqual(s.transport.sendCount, sendsBefore + 1)
        },
        .case("pairingCodeFormatValidatedBeforeSend") {
            let s = FakeSession.make()
            try s.driveToPairing()
            for bad in ["12345", "1234567", "abcdef", "12 345"] {
                harness.expect(!s.client.submitPairingCode(bad),
                               "must reject: \(bad)")
            }
            harness.expectEqual(s.transport.sendCount, 1,
                                "only the HELLO was sent")
            s.client.disconnect()
            harness.expect(!s.client.submitPairingCode(FakeSession.testCode),
                           "blocked while disconnected")
        },
        .case("heartbeatScheduledAtFiveSecondsAndTicksSendEnvelope") {
            let s = FakeSession.make() // default 5000 ms
            harness.expectEqual(s.client.state, SignalingState.disconnected)
            harness.expect(s.timer.isRunning == false,
                           "no heartbeat while disconnected")
            s.client.connect(host: "10.0.0.7", port: 47987)
            s.transport.fireReady()
            harness.expectEqual(s.timer.startedIntervalSeconds, 5.0,
                                "heartbeat interval from config (ms → s)")
            harness.expect(s.timer.isRunning,
                           "heartbeat starts once the transport is ready")
            s.clock.now += 5_000
            s.timer.tick()
            harness.expectEqual(s.client.sentHeartbeatCount, 1)
            let envelope = s.transport.sentEnvelopes.last
            harness.expectEqual(envelope?.kind, RWKind.heartbeat)
            if case .heartbeat(let m)? = envelope?.payload {
                harness.expectEqual(m.senderTimeMs, 1_700_000_005_000)
            }
            harness.expectEqual(s.client.lastActivityMs, 1_700_000_005_000)
            s.timer.tick()
            harness.expectEqual(s.client.sentHeartbeatCount, 2)
        },
        .case("socketErrorResetsStateAndStopsHeartbeat") {
            let s = FakeSession.make()
            try s.driveToPairing()
            harness.expect(s.client.submitPairingCode(FakeSession.testCode))
            try s.transport.deliver(FakeSession.pairingResult(accepted: true))
            try s.transport.deliver(Envelope(
                kind: .appCatalog, requestID: 1,
                payload: .appCatalog(AppCatalog(catalogRevision: 1))))
            harness.expectEqual(s.client.state, SignalingState.paired)
            struct SocketBoom: Error {}
            s.transport.fireClosed(SocketBoom())
            harness.expectEqual(s.client.state, SignalingState.disconnected,
                                "no stale connection after a socket error")
            harness.expect(s.timer.isRunning == false, "heartbeat must stop")
            harness.expect(s.client.catalog == nil, "catalog cleared")
            harness.expect(s.client.challenge == nil, "challenge cleared")
            harness.expect(s.client.lastPairingResult == nil)
            harness.expect(s.client.lastActivityMs == nil)
        },
        .case("disconnectResetsAndReconnectWorks") {
            let s = FakeSession.make()
            try s.driveToPairing()
            s.client.disconnect()
            harness.expectEqual(s.client.state, SignalingState.disconnected)
            harness.expectEqual(s.transport.closedCount, 1)
            // Fresh connect re-sends HELLO and the host re-challenges.
            s.client.connect(host: "10.0.0.7", port: 47987)
            s.transport.fireReady()
            harness.expectEqual(s.transport.sentEnvelopes.last?.kind, RWKind.hello)
            try s.transport.deliver(FakeSession.hostHello())
            try s.transport.deliver(FakeSession.challenge())
            harness.expectEqual(s.client.state, SignalingState.pairing)
        },
        .case("oversizedDeclaredFrameResetsConnection") {
            let s = FakeSession.make()
            try s.driveToPairing()
            // Header declares 8 MiB + 1 — malformed per the 8 MiB cap.
            s.transport.deliverRaw(Data([0xff, 0xff, 0xff, 0x01]))
            harness.expectEqual(s.client.state, SignalingState.disconnected)
            harness.expectEqual(s.transport.closedCount, 1,
                                "corrupt stream must close the transport")
        },
        .case("undecodableEnvelopeDroppedWithoutEndingSession") {
            let s = FakeSession.make()
            try s.driveToPairing()
            s.transport.deliverRaw(try Framing.encode(Data("not json".utf8)))
            harness.expectEqual(s.client.droppedMessageCount, 1)
            harness.expectEqual(s.client.state, SignalingState.pairing,
                                "a bad envelope does not end the session")
            let badHello = #"""
            {"schema_major":1,"schema_minor":0,"kind":"hello","payload":{"product_name":"other","app_version":"0.1.0"}}
            """#
            s.transport.deliverRaw(try Framing.encode(Data(badHello.utf8)))
            harness.expectEqual(s.client.droppedMessageCount, 2,
                                "validation failure is dropped, not fatal")
            harness.expectEqual(s.client.state, SignalingState.pairing)
        },
        .case("catalogRequestOnlyWhilePaired") {
            let s = FakeSession.make()
            try s.driveToPairing()
            harness.expect(!s.client.requestCatalog(), "blocked while pairing")
            harness.expect(s.client.submitPairingCode(FakeSession.testCode))
            try s.transport.deliver(FakeSession.pairingResult(accepted: true))
            harness.expectEqual(s.client.state, SignalingState.paired)
            let before = s.transport.sendCount
            harness.expect(s.client.requestCatalog(forceRescan: true))
            let request = s.transport.sentEnvelopes.last
            harness.expectEqual(request?.kind, RWKind.appCatalogRequest)
            if case .appCatalogRequest(let m)? = request?.payload {
                harness.expect(m.forceRescan, "force_rescan honored")
            }
            harness.expectEqual(s.transport.sendCount, before + 1)
        },
        .case("loopbackFullHandshakeOverRealTCP") {
            let host = try LoopbackHost()
            defer { host.shutdown() }
            let timer = FakeSignalingTimer()
            let client = SignalingClient(
                transport: NWSignalingTransport(),
                timerFactory: { timer },
                config: .init(appVersion: "0.1.0", heartbeatIntervalMs: 200))
            client.connect(host: "127.0.0.1", port: Int(host.port))
            harness.expect(waitFor(timeout: 5) { host.accepted },
                           "in-test host accepted the loopback connection")
            harness.expect(waitFor(timeout: 5) { client.state == .pairing },
                           "challenge received over the real socket")
            harness.expectEqual(client.hostName, host.hostName)
            harness.expect(client.submitPairingCode(host.code))
            harness.expect(waitFor(timeout: 5) { client.state == .paired },
                           "pairing accepted over the real socket")
            harness.expect(waitFor(timeout: 5) { host.catalogRequestCount == 1 },
                           "auto app_catalog_request reached the host")
            harness.expect(waitFor(timeout: 5) { client.catalog != nil })
            harness.expectEqual(client.catalog?.entries.count, 0,
                                "host serves the empty catalog honestly")
            harness.expectEqual(client.catalog?.catalogRevision, UInt32(1))
            timer.tick()
            harness.expect(waitFor(timeout: 5) { host.heartbeatCount >= 1 },
                           "heartbeat observed by the host")
            client.disconnect()
            harness.expectEqual(client.state, SignalingState.disconnected)
            harness.expect(waitFor(timeout: 5) { host.openConnections == 0 },
                           "host saw the client close")
        },
        .case("loopbackWrongCodeRejectedOverRealTCP") {
            let host = try LoopbackHost()
            defer { host.shutdown() }
            let timer = FakeSignalingTimer()
            let client = SignalingClient(
                transport: NWSignalingTransport(),
                timerFactory: { timer },
                config: .init(appVersion: "0.1.0"))
            client.connect(host: "127.0.0.1", port: Int(host.port))
            harness.expect(waitFor(timeout: 5) { client.state == .pairing })
            harness.expect(client.submitPairingCode("000000"))
            harness.expect(waitFor(timeout: 5) { client.state == .rejected },
                           "rejection surfaced over the real socket")
            harness.expectEqual(client.lastPairingResult?.errorCode, "invalid_code")
            harness.expectEqual(client.lastPairingResult?.safeMessage,
                                "Pairing code was not accepted")
            client.disconnect()
            harness.expectEqual(client.state, SignalingState.disconnected)
        },
    ]
}