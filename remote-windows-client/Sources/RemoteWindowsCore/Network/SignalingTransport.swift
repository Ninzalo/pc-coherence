import Foundation

/// Minimal transport contract for the signaling client (task RW-C-001).
///
/// Implementations deliver received bytes via `onData`, signal once that
/// the transport is ready to carry frames via `onReady`, and report a
/// fatal close via `onClosed` exactly once per connection (nil = clean
/// close, `Error` = I/O or connection failure). The client treats any
/// `onClosed` as "the connection is gone" and resets to
/// `.disconnected` — a stale connection is never kept.
///
/// Implementations must not invoke callbacks synchronously from inside
/// `connect`/`send`/`close`; callbacks may arrive on any thread (the real
/// `NWSignalingTransport` delivers on its own serial queue). Callback
/// properties must be set before `connect` is called.
public protocol SignalingTransport: AnyObject {
    /// Bytes arriving from the peer (partial frames are fine; the client
    /// reassembles with `FrameReader`).
    var onData: ((Data) -> Void)? { get set }
    /// Transport established; the client may now send.
    var onReady: (() -> Void)? { get set }
    /// Connection closed or failed, at most once per connection.
    var onClosed: ((Error?) -> Void)? { get set }

    func connect(host: String, port: Int)
    /// Send one already-framed message (see `Framing.encode`).
    func send(_ bytes: Data)
    func close()
}

/// Millisecond clock for `sender_time_ms` and last-activity tracking.
/// Injectable so tests can drive time deterministically.
public protocol SignalingClock {
    func nowMs() -> Int64
}

public struct SystemClock: SignalingClock {
    public init() {}
    public func nowMs() -> Int64 {
        Int64(Date().timeIntervalSince1970 * 1000)
    }
}

/// Minimal repeating-timer contract for the heartbeat. The app uses
/// `MainRunLoopTimer`; tests inject a fake and drive ticks manually.
/// Class-constrained so callers can configure `onTick` through a
/// reference.
public protocol SignalingTimer: AnyObject {
    var onTick: (() -> Void)? { get set }
    func start(intervalSeconds: Double)
    func stop()
}

/// A `Timer` on the main run loop in `.common` mode so it fires while the
/// user is interacting with the window. Must be created and started on
/// the main thread (the app does this); `stop()` is safe from any thread.
public final class MainRunLoopTimer: SignalingTimer {
    public var onTick: (() -> Void)?
    private var timer: Timer?

    public init() {}

    public func start(intervalSeconds: Double) {
        stop()
        let t = Timer(timeInterval: intervalSeconds, repeats: true) { [weak self] _ in
            self?.onTick?()
        }
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    public func stop() {
        guard let t = timer else { return }
        timer = nil
        DispatchQueue.main.async { t.invalidate() }
    }
}
