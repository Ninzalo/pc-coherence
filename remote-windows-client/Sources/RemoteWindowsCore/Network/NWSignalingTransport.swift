import Foundation
import Network

/// Real TCP transport for the signaling client, on Network.framework
/// (`NWConnection`). All connection callbacks are delivered on a serial
/// queue; `onClosed` fires at most once per connection. A
/// self-initiated `close()` is reported as a clean close (nil error).
///
/// Loopback and unit tests inject `SignalingTransport` fakes instead; this
/// type is what the app wires up (task RW-C-001).
public final class NWSignalingTransport: SignalingTransport, @unchecked Sendable {
    private var _onData: ((Data) -> Void)?
    private var _onReady: (() -> Void)?
    private var _onClosed: ((Error?) -> Void)?
    private let callbacksLock = NSLock()

    public var onData: ((Data) -> Void)? {
        get { callbacksLock.lock(); defer { callbacksLock.unlock() }; return _onData }
        set { callbacksLock.lock(); _onData = newValue; callbacksLock.unlock() }
    }
    public var onReady: (() -> Void)? {
        get { callbacksLock.lock(); defer { callbacksLock.unlock() }; return _onReady }
        set { callbacksLock.lock(); _onReady = newValue; callbacksLock.unlock() }
    }
    public var onClosed: ((Error?) -> Void)? {
        get { callbacksLock.lock(); defer { callbacksLock.unlock() }; return _onClosed }
        set { callbacksLock.lock(); _onClosed = newValue; callbacksLock.unlock() }
    }

    private var connection: NWConnection?
    private let queue: DispatchQueue
    private var closedReported = false
    private var closedByUs = false

    public init(queue: DispatchQueue? = nil) {
        self.queue = queue ?? DispatchQueue(label: "remotewindows.signaling.transport")
    }

    public func connect(host: String, port: Int) {
        queue.async { [weak self] in
            self?.startConnection(host: host, port: port)
        }
    }

    public func send(_ bytes: Data) {
        queue.async { [weak self] in
            guard let self, let connection = self.connection,
                  !self.closedReported else { return }
            connection.send(content: bytes, completion: .contentProcessed { error in
                if let error { self.reportClosed(error) }
            })
        }
    }

    public func close() {
        queue.async { [weak self] in
            guard let self, let connection = self.connection else { return }
            self.closedByUs = true
            self.connection = nil
            connection.cancel()
        }
    }

    // MARK: Internals (all on `queue`)

    private func startConnection(host: String, port: Int) {
        connection?.cancel()
        closedReported = false
        closedByUs = false
        guard let portValue = NWEndpoint.Port(rawValue: UInt16(port)) else {
            reportClosed(NWSignalingError.badPort)
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
                self.emitReady()
            case .failed(let error):
                self.reportClosed(error)
            case .cancelled:
                self.reportClosed(nil)
            case .waiting, .setup, .preparing:
                break
            @unknown default:
                break
            }
        }
        connection.start(queue: queue)
        receiveLoop(connection)
    }

    private func receiveLoop(_ connection: NWConnection) {
        connection.receive(minimumIncompleteLength: 1,
                            maximumLength: Framing.maxFrameSize + 4) {
            [weak self, weak connection] data, _, isComplete, error in
            guard let self, let connection, self.connection === connection else { return }
            if let data, !data.isEmpty {
                self.emitData(data)
            }
            if let error {
                self.reportClosed(error)
            } else if isComplete {
                self.reportClosed(nil)
            } else {
                self.receiveLoop(connection)
            }
        }
    }

    private func emitData(_ data: Data) {
        callbacksLock.lock()
        let handler = _onData
        callbacksLock.unlock()
        handler?(data)
    }

    private func emitReady() {
        callbacksLock.lock()
        let handler = _onReady
        callbacksLock.unlock()
        handler?()
    }

    private func reportClosed(_ error: Error?) {
        guard !closedReported else { return }
        closedReported = true
        callbacksLock.lock()
        let handler = _onClosed
        callbacksLock.unlock()
        // A self-initiated close is not an error.
        handler?(closedByUs ? nil : error)
    }
}

/// Local failure raised before a connection attempt (e.g. bad port).
public struct NWSignalingError: Error, Equatable {
    public static let badPort = NWSignalingError(message: "port out of range")
    public let message: String
}
