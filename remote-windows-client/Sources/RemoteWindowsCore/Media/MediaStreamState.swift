import Foundation

/// Per-window media stream state (PROTOCOL §9.3/§9.4/§9.5): the
/// registered windows, each with its derived `media_key` /
/// `nonce_prefix` / `stream_id`, its `MediaStreamCipher`, and its
/// per-stream `frame_seq` tracking (gap detection).
///
/// Thread safety: `MediaChannel` reads/writes entries on its own queue
/// (receive path); `AppController` registers/removes on the main queue.
/// All access goes through one lock.
public final class MediaStreamState {
    /// One registered window's stream state.
    public final class StreamEntry {
        public let windowID: String
        public let streamID: UInt16
        public let cipher: MediaStreamCipher
        /// Last delivered `frame_seq` on this stream (nil = none yet).
        public var lastFrameSeq: UInt32?
        /// Frames delivered since registration.
        public var frameCount: UInt64 = 0
        /// `frame_seq` discontinuities seen (a gap is logged and the
        /// stream continues — resync happens on the next keyframe,
        /// §9.4/§9.5; a gap never closes the channel).
        public var gapCount: UInt32 = 0

        init(windowID: String, streamID: UInt16, cipher: MediaStreamCipher) {
            self.windowID = windowID
            self.streamID = streamID
            self.cipher = cipher
        }
    }

    private let sessionKeyHex: String
    private let lock = NSLock()
    private var byWindowID: [String: StreamEntry] = [:]
    private var byStreamID: [UInt16: String] = [:]

    /// `sessionKeyHex` must be a 64-char lowercase-hex session key
    /// (validated on registration, per §9.3).
    public init(sessionKeyHex: String) {
        self.sessionKeyHex = sessionKeyHex
    }

    /// Register a window announced on the control channel: derive
    /// `media_key` + `nonce_prefix` + `stream_id` for it. Re-registering
    /// an already-registered window id REPLACES its entry (a fresh
    /// cipher — the stream restarts on its next keyframe, §9.5).
    /// Throws `MediaKeys.MediaKeysError.invalidSessionKeyHex` if the
    /// stored session key is malformed (defensive: derived keys are
    /// always well-formed).
    @discardableResult
    public func register(windowID: String) throws -> StreamEntry {
        let key = try MediaKeys.mediaKey(sessionKeyHex: sessionKeyHex, windowID: windowID)
        let prefix = try MediaKeys.noncePrefix(sessionKeyHex: sessionKeyHex,
                                               windowID: windowID)
        let entry = StreamEntry(windowID: windowID,
                                streamID: MediaFraming.streamID(forWindowID: windowID),
                                cipher: MediaStreamCipher(keyData: key, noncePrefix: prefix))
        lock.lock()
        byWindowID[windowID] = entry
        byStreamID[entry.streamID] = windowID
        lock.unlock()
        return entry
    }

    /// Remove a window (control-channel `window_remove`): frees its
    /// stream state. Idempotent.
    public func remove(windowID: String) {
        lock.lock()
        if let entry = byWindowID.removeValue(forKey: windowID) {
            byStreamID.removeValue(forKey: entry.streamID)
        }
        lock.unlock()
    }

    /// Drop every registered stream (channel close / disconnect).
    public func reset() {
        lock.lock()
        byWindowID.removeAll()
        byStreamID.removeAll()
        lock.unlock()
    }

    public func entry(forWindowID windowID: String) -> StreamEntry? {
        lock.lock(); defer { lock.unlock() }
        return byWindowID[windowID]
    }

    public func entry(forStreamID streamID: UInt16) -> StreamEntry? {
        lock.lock(); defer { lock.unlock() }
        guard let windowID = byStreamID[streamID] else { return nil }
        return byWindowID[windowID]
    }

    public var registeredWindowIDs: [String] {
        lock.lock(); defer { lock.unlock() }
        return byWindowID.keys.sorted()
    }

    /// Receive-path frame handling under the lock: decrypt the payload
    /// with the stream's cipher (counter = `frameSeq`), update the
    /// `frame_seq` tracking (gap detection), and return the plaintext.
    /// Throws `MediaAeadError` on a bad GCM tag (§9.6: close the
    /// channel) and `MediaUnknownStream` for an unregistered
    /// `stream_id` (§9.6: close).
    public func processFrame(streamID: UInt16, frameSeq: UInt32, isKeyframe: Bool,
                             ciphertext: Data) throws -> (windowID: String,
                                                          isKeyframe: Bool,
                                                          plaintext: Data) {
        lock.lock(); defer { lock.unlock() }
        guard let windowID = byStreamID[streamID],
              let entry = byWindowID[windowID] else {
            // Unregistered stream: not a GCM error — the channel maps
            // this to its own typed close reason.
            throw MediaUnknownStream(streamID: streamID)
        }
        let plaintext = try entry.cipher.open(ciphertext, frameSeq: frameSeq)
        if let last = entry.lastFrameSeq, frameSeq != last &+ 1 {
            entry.gapCount += 1   // gap: counted, stream continues (§9.4)
        }
        entry.lastFrameSeq = frameSeq
        entry.frameCount += 1
        return (windowID, isKeyframe, plaintext)
    }
}

/// A packet arrived for a `stream_id` with no registered window
/// (protocol violation — §9.6 close).
public struct MediaUnknownStream: Error, Equatable {
    public let streamID: UInt16
    public init(streamID: UInt16) { self.streamID = streamID }
}
