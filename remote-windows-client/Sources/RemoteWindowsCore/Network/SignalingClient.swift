import Foundation

/// Connection state of the signaling session (task RW-C-001).
public enum SignalingState: String, Equatable, CaseIterable {
    case disconnected
    case connecting
    /// TCP is up, HELLO was sent; waiting for the host's challenge.
    case pairing
    case paired
    /// The host rejected the pairing code. `lastPairingResult` carries the
    /// host's error code + safe message; the user may retry.
    case rejected
    /// The host locked pairing after repeated failures (60 s host-side
    /// lockout). The client never retries automatically; the user may
    /// retry explicitly once the lockout has passed.
    case locked
}

/// Client-side state machine for the Windows Host signaling session:
/// connect → HELLO → host challenge → pairing code → paired → app catalog,
/// with a periodic heartbeat while connected.
///
/// Handshake counterpart of the Host's `app/SessionAuth.cpp` (verified on
/// Windows, Host commit 870aec1): the client sends HELLO as soon as the
/// transport is ready, presents the user-entered 6-digit one-time code
/// together with the echoed `challenge_nonce` and `host_name`, and — once
/// the host accepts — sends `app_catalog_request` exactly once. Heartbeats
/// go out every `heartbeatIntervalMs` in every non-disconnected state
/// (the host accepts heartbeats in all states).
///
/// The transport is injected (`SignalingTransport`) so unit tests can
/// drive the state machine without a socket. All state mutations are
/// serialized on one lock; callbacks (`onStateChanged`, `onCatalog`,
/// `onPairingResult`, `onLaunchResult`, `onAppStatus`) are delivered after
/// the state section completes and may arrive on the transport's queue —
/// the UI must hop to the main queue. Set the callbacks once, before the
/// first `connect`.
public final class SignalingClient {

    public struct Config: Equatable {
        /// Wire product name (PROTOCOL.md §7; Host validation).
        public var productName: String
        public var appVersion: String
        /// Client capabilities per `valid_hello_client.json`.
        public var capabilities: [RWCapability]
        /// Heartbeat period while connected; default 5 s per RW-C-001.
        public var heartbeatIntervalMs: Int64

        public init(productName: String = "macos-client",
                    appVersion: String,
                    capabilities: [RWCapability] = [.appCatalog, .input, .resize],
                    heartbeatIntervalMs: Int64 = 5000) {
            self.productName = productName
            self.appVersion = appVersion
            self.capabilities = capabilities
            self.heartbeatIntervalMs = heartbeatIntervalMs
        }
    }

    // MARK: Public state (read through the lock)

    public var state: SignalingState {
        get { lock.lock(); defer { lock.unlock() }; return _state }
    }
    /// Last time any envelope was sent or received (ms, per the clock).
    public var lastActivityMs: Int64? {
        get { lock.lock(); defer { lock.unlock() }; return _lastActivityMs }
    }
    /// The host's HELLO (capabilities), once received.
    public var hostHello: Hello? {
        get { lock.lock(); defer { lock.unlock() }; return _hostHello }
    }
    /// Host name from the pairing challenge, once received.
    public var hostName: String? {
        get { lock.lock(); defer { lock.unlock() }; return _hostName }
    }
    /// Current pairing challenge (nonce + host name to echo back).
    public var challenge: PairingChallenge? {
        get { lock.lock(); defer { lock.unlock() }; return _challenge }
    }
    /// Last app catalog received (empty `entries` is the honest current
    /// host state — the catalog scanner is a later host task).
    public var catalog: AppCatalog? {
        get { lock.lock(); defer { lock.unlock() }; return _catalog }
    }
    /// Last pairing_result (error code + safe message on rejection).
    public var lastPairingResult: PairingResult? {
        get { lock.lock(); defer { lock.unlock() }; return _lastPairingResult }
    }
    /// The derived §9.3 media session key (64 lowercase-hex chars),
    /// available once pairing is accepted and `nil` otherwise. Needed by
    /// the media channel handshake / per-stream key derivation
    /// (RW-C-003). NEVER logged (content policy).
    public var mediaSessionKey: String? {
        get { lock.lock(); defer { lock.unlock() }; return _mediaSessionKey }
    }
    /// Inbound envelopes that failed decode or semantic validation.
    public var droppedMessageCount: Int {
        get { lock.lock(); defer { lock.unlock() }; return _droppedCount }
    }
    /// Heartbeats sent since `connect`.
    public var sentHeartbeatCount: Int {
        get { lock.lock(); defer { lock.unlock() }; return _sentHeartbeats }
    }
    /// The live heartbeat timer (for inspection; tests inject a fake).
    public var heartbeatTimer: SignalingTimer? {
        get { lock.lock(); defer { lock.unlock() }; return _timer }
    }

    // MARK: Callbacks (set once, before the first connect)

    public var onStateChanged: ((SignalingState) -> Void)?
    public var onCatalog: ((AppCatalog) -> Void)?
    public var onPairingResult: ((PairingResult) -> Void)?
    /// Host `launch_result` for a `launch_app` this client sent
    /// (echoes the `app_id`; non-sensitive, safe to surface to the UI).
    /// Same delivery contract as `onCatalog`.
    public var onLaunchResult: ((LaunchResult) -> Void)?
    /// Host `app_status` liveness (`running`/`closed`), heartbeat-piggybacked.
    /// Same delivery contract as `onCatalog`.
    public var onAppStatus: ((AppStatus) -> Void)?
    /// Host `window_announce` (control kind, paired-only): a host window
    /// is being streamed — start/refresh its media stream (RW-C-003).
    /// Same delivery contract as `onCatalog`.
    public var onWindowAnnounce: ((WindowAnnounce) -> Void)?
    /// Host `window_remove` (control kind, paired-only): a host window
    /// stopped streaming — discard its media stream (RW-C-003).
    /// Same delivery contract as `onCatalog`.
    public var onWindowRemove: ((WindowRemove) -> Void)?

    // MARK: Internals

    private let transport: SignalingTransport
    private let clock: SignalingClock
    private let timerFactory: () -> SignalingTimer
    private let config: Config
    private let lock = NSLock()

    private var _state: SignalingState = .disconnected
    private var _lastActivityMs: Int64?
    private var _hostHello: Hello?
    private var _hostName: String?
    private var _challenge: PairingChallenge?
    private var _catalog: AppCatalog?
    private var _lastPairingResult: PairingResult?
    /// The pairing code last presented by the user (kept only to derive
    /// the §9.3 session key on acceptance; never logged).
    private var _pairingCode: String?
    /// Derived §9.3 session key (hex) — set on pairing acceptance,
    /// cleared on disconnect/reset.
    private var _mediaSessionKey: String?
    private var _droppedCount = 0
    private var _sentHeartbeats = 0
    private var _timer: SignalingTimer?
    private var _frameReader = FrameReader()
    private var _nextRequestID: UInt32 = 1
    private var pendingCallbacks: [() -> Void] = []
    private var pendingPosts: [() -> Void] = []

    public init(transport: SignalingTransport,
                clock: SignalingClock = SystemClock(),
                timerFactory: @escaping () -> SignalingTimer = { MainRunLoopTimer() },
                config: Config) {
        self.transport = transport
        self.clock = clock
        self.timerFactory = timerFactory
        self.config = config
        transport.onData = { [weak self] in self?.handleData($0) }
        transport.onReady = { [weak self] in self?.handleReady() }
        transport.onClosed = { [weak self] in self?.handleClosed($0) }
    }

    deinit {
        _timer?.stop()
    }

    /// Client-side pairing-code format check (6 ASCII digits; PROTOCOL §7.4).
    public static func isValidPairingCodeFormat(_ code: String) -> Bool {
        ProtocolValidation.isSixDigitCode(code)
    }

    // MARK: Actions

    /// Start (or restart) a session: resets all state, opens the transport,
    /// sends HELLO once it is ready. Safe from any state.
    public func connect(host: String, port: Int) {
        withSync {
            resetToDisconnected()
            _frameReader = FrameReader()
            _nextRequestID = 1
            setState(.connecting)
        } post: { [weak self] in
            self?.transport.connect(host: host, port: port)
        }
    }

    /// Tear down the session: closes the transport and resets to
    /// `.disconnected` (no stale connection is ever kept).
    public func disconnect() {
        withSync {
            resetToDisconnected()
        } post: { [weak self] in
            self?.transport.close()
        }
    }

    /// Present the user-entered pairing code. Returns `false` (and sends
    /// nothing) unless the code is exactly 6 digits and the session is in
    /// `.pairing`, `.rejected`, or `.locked`. An explicit retry from
    /// `.locked` is allowed (user-initiated, never automatic).
    @discardableResult
    public func submitPairingCode(_ code: String) -> Bool {
        guard Self.isValidPairingCodeFormat(code) else { return false }
        return withSyncResult {
            guard _state == .pairing || _state == .rejected || _state == .locked,
                  let challenge = _challenge else { return false }
            let envelope = Envelope(
                kind: .pairingChallenge,
                payload: .pairingChallenge(PairingChallenge(
                    oneTimeCode: code,
                    hostName: challenge.hostName,
                    challengeNonce: challenge.challengeNonce)))
            sendEnvelope(envelope)
            // Retain the just-presented code: the §9.3 session key is
            // derived from it (+ host_name) once the host accepts.
            _pairingCode = code
            return true
        }
    }

    /// Send an app catalog request. Only valid while `.paired` (the host
    /// drops gated requests otherwise); the auto request after pairing is
    /// sent with `force_rescan = false`.
    @discardableResult
    public func requestCatalog(forceRescan: Bool = false) -> Bool {
        return withSyncResult {
            guard _state == .paired else { return false }
            sendEnvelope(makeAppCatalogRequest(forceRescan: forceRescan))
            return true
        }
    }

    /// Launch a host application by its stable `app_id` (FR-02). Only
    /// valid while `.paired`; carries a fresh `request_id` like
    /// `requestCatalog`. One click, one `launch_app` — no retry logic, no
    /// auto-resend (the client never sends paths or commands).
    @discardableResult
    public func launchApp(appID: String) -> Bool {
        return withSyncResult {
            guard _state == .paired else { return false }
            sendEnvelope(Envelope(kind: .launchApp, requestID: nextRequestID(),
                                  payload: .launchApp(LaunchApp(appID: appID))))
            return true
        }
    }

    // MARK: Transport events

    private func handleReady() {
        withSync {
            guard _state == .connecting else { return } // stale ready
            sendEnvelope(Envelope(kind: .hello, payload: .hello(Hello(
                productName: config.productName,
                appVersion: config.appVersion,
                capabilities: config.capabilities))))
            startHeartbeat()
        }
    }

    private func handleData(_ data: Data) {
        withSync {
            do {
                let frames = try _frameReader.feed(data)
                for frame in frames {
                    guard let envelope = try? ProtocolCodec.decodeEnvelope(frame) else {
                        _droppedCount += 1
                        continue
                    }
                    guard ProtocolValidation.validate(envelope).ok else {
                        _droppedCount += 1
                        continue
                    }
                    _lastActivityMs = clock.nowMs()
                    route(envelope)
                }
            } catch {
                // A malformed frame (e.g. declared length > 8 MiB) corrupts
                // the stream — drop the connection instead of desyncing.
                resetToDisconnected()
                pendingPosts.append { [weak self] in self?.transport.close() }
            }
        }
    }

    private func handleClosed(_ error: Error?) {
        withSync {
            guard _state != .disconnected else { return }
            resetToDisconnected()
        }
    }

    // MARK: Routing

    private func route(_ envelope: Envelope) {
        switch envelope.payload {
        case .hello(let hello):
            // Host HELLO (capabilities). We only ever accept it from the
            // host; it does not change the state by itself.
            if _state == .connecting {
                _hostHello = hello
            }
        case .pairingChallenge(let challenge):
            if _state == .connecting {
                _challenge = challenge
                _hostName = challenge.hostName
                setState(.pairing)
            }
        case .pairingResult(let result):
            _lastPairingResult = result
            if result.accepted {
                setState(.paired)
                // §9.3: derive the media session key from the just-
                // presented code + the challenge host name (both held).
                // Retained read-only for the media channel; never
                // logged.
                if let code = _pairingCode, let hostName = _hostName {
                    _mediaSessionKey = MediaKeys.sessionKey(code: code,
                                                            hostName: hostName)
                }
                // Exactly one catalog request per pairing (RW-C-001).
                sendEnvelope(makeAppCatalogRequest(forceRescan: false))
            } else if result.errorCode == "locked" {
                setState(.locked)
            } else {
                setState(.rejected)
            }
            pendingCallbacks.append { [weak self] in
                self?.onPairingResult?(result)
            }
        case .appCatalog(let catalog):
            if _state == .paired {
                _catalog = catalog
                pendingCallbacks.append { [weak self] in
                    self?.onCatalog?(catalog)
                }
            }
        case .appCatalogChanged:
            // The host's catalog changed: re-request the snapshot (the
            // host answers with the fresh catalog). Non-forced — the host
            // already knows the catalog changed (RW-C-002).
            if _state == .paired {
                sendEnvelope(makeAppCatalogRequest(forceRescan: false))
            }
        case .launchResult(let result):
            if _state == .paired {
                pendingCallbacks.append { [weak self] in
                    self?.onLaunchResult?(result)
                }
            }
        case .appStatus(let status):
            if _state == .paired {
                pendingCallbacks.append { [weak self] in
                    self?.onAppStatus?(status)
                }
            }
        case .windowAnnounce(let announce):
            // Media stream lifecycle (RW-C-003): paired-only, like the
            // other host-driven kinds.
            if _state == .paired {
                pendingCallbacks.append { [weak self] in
                    self?.onWindowAnnounce?(announce)
                }
            }
        case .windowRemove(let remove):
            if _state == .paired {
                pendingCallbacks.append { [weak self] in
                    self?.onWindowRemove?(remove)
                }
            }
        case .heartbeat:
            break // liveness only; last activity already updated
        default:
            break // other kinds (windows, input, power) are later tasks
        }
    }

    // MARK: Sending

    private func sendEnvelope(_ envelope: Envelope) {
        do {
            let json = try ProtocolCodec.encode(envelope)
            let frame = try Framing.encode(json)
            transport.send(frame)
            _lastActivityMs = clock.nowMs()
            if envelope.kind == .heartbeat { _sentHeartbeats += 1 }
        } catch {
            // Even encoding failed: the stream cannot be trusted.
            resetToDisconnected()
            pendingPosts.append { [weak self] in self?.transport.close() }
        }
    }

    private func makeAppCatalogRequest(forceRescan: Bool) -> Envelope {
        let id = nextRequestID()
        return Envelope(kind: .appCatalogRequest, requestID: id,
                        payload: .appCatalogRequest(AppCatalogRequest(forceRescan: forceRescan)))
    }

    /// Fresh monotonic `request_id` (wraps UInt32.max → 1).
    private func nextRequestID() -> UInt32 {
        let id = _nextRequestID
        _nextRequestID = id == UInt32.max ? 1 : id &+ 1
        return id
    }

    // MARK: Heartbeat

    private func startHeartbeat() {
        _timer?.stop()
        let timer = timerFactory()
        timer.onTick = { [weak self] in self?.sendHeartbeat() }
        timer.start(intervalSeconds: Double(config.heartbeatIntervalMs) / 1000.0)
        _timer = timer
    }

    private func stopHeartbeat() {
        _timer?.stop()
        _timer = nil
    }

    private func sendHeartbeat() {
        withSync {
            guard _state != .disconnected else { return }
            sendEnvelope(Envelope(kind: .heartbeat,
                                  payload: .heartbeat(Heartbeat(senderTimeMs: clock.nowMs()))))
        }
    }

    // MARK: State helpers

    private func setState(_ new: SignalingState) {
        guard _state != new else { return }
        _state = new
        pendingCallbacks.append { [weak self] in
            self?.onStateChanged?(new)
        }
    }

    private func resetToDisconnected() {
        stopHeartbeat()
        _hostHello = nil
        _hostName = nil
        _challenge = nil
        _catalog = nil
        _lastPairingResult = nil
        _pairingCode = nil
        _mediaSessionKey = nil
        _lastActivityMs = nil
        _droppedCount = 0
        _sentHeartbeats = 0
        setState(.disconnected)
    }

    /// Run a state section under the lock; queued callbacks fire only
    /// after the lock is released (callbacks must not re-enter the client
    /// synchronously).
    private func withSync(_ body: () -> Void, post: (() -> Void)? = nil) {
        lock.lock()
        body()
        let callbacks = pendingCallbacks
        pendingCallbacks.removeAll()
        let posts = pendingPosts
        pendingPosts.removeAll()
        lock.unlock()
        for post in posts { post() }
        if let post { post() }
        for callback in callbacks { callback() }
    }

    private func withSyncResult<T>(_ body: () -> T) -> T {
        lock.lock()
        let result = body()
        let callbacks = pendingCallbacks
        pendingCallbacks.removeAll()
        let posts = pendingPosts
        pendingPosts.removeAll()
        lock.unlock()
        for post in posts { post() }
        for callback in callbacks { callback() }
        return result
    }
}
