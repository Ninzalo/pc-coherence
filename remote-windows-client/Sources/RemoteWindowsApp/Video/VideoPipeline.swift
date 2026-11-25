import AppKit
import CoreMedia
import VideoToolbox

/// Per-stream H.264 decode (PROTOCOL §9.1/§9.5): one
/// `VTDecompressionSession` per `window_id`, fed with the decrypted
/// H.264 NAL payloads delivered by `MediaChannel`, rendered through the
/// stream's `VideoSurfaceView`.
///
/// The wire payload is Annex-B (start-code delimited) NAL units. The
/// Host's first packet on a stream MUST be a keyframe (§9.5); the
/// SPS/PPS parameter sets travel in-band in that access unit, so the
/// pipeline extracts them from the first keyframe before creating the
/// session. The VideoToolbox session is configured as AVCC
/// (`NALUnitHeaderLength = 4`), so each access unit is converted
/// Annex-B → AVCC (4-byte big-endian length prefixes) before decoding.
///
/// A keyframe after a gap triggers a decoder reset via the
/// `resetDecoderBeforeDecoding` sample attachment (the successor of
/// `VTDecompressionSessionFlush`) so the session can restart decoding
/// cleanly (§9.4 resync).
///
/// API notes (macOS 27.2 CLT SDK): the classic C VideoToolbox/CoreMedia
/// entry points are used (macOS 10.7–10.15 availability — the package
/// deploys to macOS 13): `VTDecompressionSessionCreate` with a null
/// output-callback record + `VTDecompressionSessionDecodeFrame` with a
/// per-decode `outputHandler` block (the macOS 14+ completion-handler
/// overload and the macOS 26+ `CMMutableDataBlockBuffer` value API are
/// not used). The H.264 format description is created with the Swift
/// `CMVideoFormatDescription(h264ParameterSets:)` initializer
/// (macOS 10.15).
///
/// Content policy: frame payloads are never logged; only counters
/// (decoded/dropped/decode-error) and stream IDs are tracked.
/// Decoded pixels are delivered on the main thread (AppKit).
/// Pixel output itself is environment-dependent (NOT RUN headless).
final class VideoPipeline {
    final class StreamSession {
        let windowID: String
        var session: VTDecompressionSession?
        var formatDescription: CMVideoFormatDescription?
        /// Decoded frames delivered to the render callback.
        private(set) var decodedFrameCount: UInt64 = 0
        /// Frames dropped by the decoder (backpressure).
        private(set) var droppedFrameCount: UInt64 = 0
        /// Decoder-reported errors (never a crash, FR-13).
        private(set) var decodeErrorCount: UInt64 = 0
        /// `frame_seq` of the last frame FED to the decoder (nil = none).
        var lastFedFrameSeq: UInt32?

        init(windowID: String) { self.windowID = windowID }

        func noteDecoded() { decodedFrameCount += 1 }
        func noteDropped() { droppedFrameCount += 1 }
        func noteDecodeError() { decodeErrorCount += 1 }
        func noteDecodedOrNothing(dropped: Bool, isError: Bool) {
            if dropped { noteDropped() }
            if isError { noteDecodeError() }
            if !dropped && !isError { noteDecoded() }
        }
    }

    private let lock = NSLock()
    private var sessions: [String: StreamSession] = [:]
    private var surfaces: [String: VideoSurfaceView] = [:]

    init() {}

    // MARK: Stream lifecycle

    /// Register (or re-register) a stream: frees any previous session
    /// for the window (re-registration = fresh decode state, §9.5) and
    /// opens the surface. A re-announce (e.g. window resize) replaces
    /// the surface — the old window is closed, never leaked.
    @discardableResult
    func register(windowID: String, surface: VideoSurfaceView) -> Bool {
        lock.lock()
        let existing = sessions[windowID]
        let session = existing ?? StreamSession(windowID: windowID)
        if existing == nil { sessions[windowID] = session }
        let oldSurface = surfaces[windowID]
        surfaces[windowID] = surface
        lock.unlock()
        if let existing { releaseSession(existing) }
        if let oldSurface, oldSurface !== surface { oldSurface.close() }
        return true
    }

    /// Remove a stream: release the `VTDecompressionSession` and the
    /// surface (idempotent).
    func remove(windowID: String) {
        lock.lock()
        let session = sessions.removeValue(forKey: windowID)
        let surface = surfaces.removeValue(forKey: windowID)
        lock.unlock()
        releaseSession(session)
        surface?.close()
    }

    /// Release every stream (signaling disconnect/reset).
    func removeAll() {
        lock.lock()
        let toRelease = Array(sessions.values)
        sessions.removeAll()
        let surfaceList = Array(surfaces.values)
        surfaces.removeAll()
        lock.unlock()
        for session in toRelease { releaseSession(session) }
        for surface in surfaceList { surface.close() }
    }

    /// Release one stream's VideoToolbox objects (takes the lock).
    private func releaseSession(_ session: StreamSession?) {
        guard let session else { return }
        lock.lock()
        session.session = nil
        session.formatDescription = nil
        lock.unlock()
    }

    // MARK: Feed (called on the main thread, from MediaChannel.onFrame)

    /// Feed one decrypted H.264 Annex-B access unit to the stream.
    func feed(windowID: String, payload: Data, isKeyframe: Bool,
              frameSeq: UInt32) {
        lock.lock()
        guard let session = sessions[windowID] else {
            lock.unlock()
            return
        }
        if session.session == nil {
            // Waiting for the first keyframe (MUST be the first packet,
            // §9.5): extract the in-band SPS/PPS and start the session.
            lock.unlock()
            startIfPossible(session, firstKeyframePayload: payload,
                            isKeyframe: isKeyframe, frameSeq: frameSeq)
            return
        }
        let resetNeeded = isKeyframe
            && session.lastFedFrameSeq.map { frameSeq != $0 &+ 1 } ?? true
        lock.unlock()
        feedFrame(session, payload: payload, frameSeq: frameSeq,
                  resetDecoder: resetNeeded)
    }

    // MARK: VideoToolbox plumbing

    private func startIfPossible(_ session: StreamSession,
                                 firstKeyframePayload: Data,
                                 isKeyframe: Bool, frameSeq: UInt32) {
        guard isKeyframe else {
            // Protocol violation (the first packet MUST be a keyframe,
            // §9.5): count it, keep waiting — never a crash (FR-13).
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }
        // The SPS/PPS travel in-band in the first access unit.
        let nal = Self.parseNalUnits(firstKeyframePayload)
        guard let sps = nal.sps, let pps = nal.pps else {
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }
        guard let formatDescription = Self.makeFormatDescription(sps: sps, pps: pps)
        else {
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }
        var vtSession: VTDecompressionSession?
        // Classic C create (macOS 10.8): null output-callback record —
        // frames are emitted through the per-decode `outputHandler`
        // block below.
        let status = VTDecompressionSessionCreate(
            allocator: nil, formatDescription: formatDescription,
            decoderSpecification: nil, imageBufferAttributes: nil,
            outputCallback: nil,
            decompressionSessionOut: &vtSession)
        guard status == noErr, let vtSession else {
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }
        lock.lock()
        session.session = vtSession
        session.formatDescription = formatDescription
        lock.unlock()
        // The same payload starts the stream: feed it now.
        feedFrame(session, payload: firstKeyframePayload, frameSeq: frameSeq,
                  resetDecoder: true)
    }

    private static func makeFormatDescription(sps: Data, pps: Data)
        -> CMVideoFormatDescription? {
        // NALUnitHeaderLength 4 = AVCC (4-byte length prefixes). The
        // Annex-B → AVCC conversion happens per access unit in
        // `annexBToAVCC`; 0 (raw) is not accepted by the macOS 27
        // overlay's initializer.
        do {
            return try CMVideoFormatDescription(
                h264ParameterSets: [sps, pps], nalUnitHeaderLength: 4)
        } catch {
            return nil
        }
    }

    /// Feed one access unit to the VT session. Builds the sample
    /// buffer (AVCC payload + one timing entry) and starts the decode;
    /// the output handler routes the result (frame or error counters +
    /// main-thread presentation).
    private func feedFrame(_ session: StreamSession, payload: Data,
                           frameSeq: UInt32, resetDecoder: Bool) {
        lock.lock()
        guard let vtSession = session.session,
              let formatDescription = session.formatDescription else {
            lock.unlock()
            return
        }
        lock.unlock()

        let avcc = Self.annexBToAVCC(payload)
        guard !avcc.isEmpty else {
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }

        // Own the AVCC bytes in a malloc'd block; the block buffer takes
        // ownership and frees it via the default allocator on release
        // (classic CMBlockBufferCreateWithMemoryBlock semantics).
        let memory = UnsafeMutableRawPointer.allocate(
            byteCount: avcc.count, alignment: 16)
        avcc.withUnsafeBytes {
            memory.copyMemory(from: $0.baseAddress!, byteCount: avcc.count)
        }
        var blockBuffer: CMBlockBuffer?
        let bufferStatus = CMBlockBufferCreateWithMemoryBlock(
            allocator: nil, memoryBlock: memory, blockLength: avcc.count,
            blockAllocator: kCFAllocatorDefault, customBlockSource: nil,
            offsetToData: 0, dataLength: avcc.count, flags: 0,
            blockBufferOut: &blockBuffer)
        guard bufferStatus == noErr, let blockBuffer else {
            memory.deallocate()
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }

        var timing = CMSampleTimingInfo()
        timing.duration = CMTime(value: 1, timescale: 30)
        timing.presentationTimeStamp =
            CMTime(value: Int64(frameSeq), timescale: 30)
        timing.decodeTimeStamp = .invalid
        var sampleSizes = [avcc.count]
        var sampleBuffer: CMSampleBuffer?
        let sampleStatus = CMSampleBufferCreate(
            allocator: nil, dataBuffer: blockBuffer, dataReady: true,
            makeDataReadyCallback: nil, refcon: nil,
            formatDescription: formatDescription,
            sampleCount: 1, sampleTimingEntryCount: 1,
            sampleTimingArray: &timing,
            sampleSizeEntryCount: 1, sampleSizeArray: &sampleSizes,
            sampleBufferOut: &sampleBuffer)
        guard sampleStatus == noErr, let sampleBuffer else {
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
            return
        }
        if resetDecoder {
            // Successor of VTDecompressionSessionFlush: reset before
            // decoding this sample (gap resync, §9.4).
            sampleBuffer.attachments[.resetDecoderBeforeDecoding] =
                .shouldPropagate(kCFBooleanTrue as Any)
        }

        lock.lock()
        session.lastFedFrameSeq = frameSeq
        let windowID = session.windowID
        lock.unlock()

        var infoFlags: VTDecodeInfoFlags = []
        let status = VTDecompressionSessionDecodeFrame(
            vtSession, sampleBuffer: sampleBuffer, flags: VTDecodeFrameFlags(),
            infoFlagsOut: &infoFlags,
            outputHandler: { [weak self] status, flags, imageBuffer, _, _ in
                let dropped = flags.contains(.frameDropped)
                let isError = status != noErr
                guard let self else { return }
                if imageBuffer != nil || dropped || isError {
                    DispatchQueue.main.async {
                        self.noteDecode(windowID: windowID,
                                        pixelBuffer: imageBuffer,
                                        dropped: dropped, isError: isError)
                    }
                }
            })
        if status != noErr {
            lock.lock()
            session.noteDecodeError()
            lock.unlock()
        }
    }

    // MARK: Result routing (main thread)

    private func noteDecode(windowID: String, pixelBuffer: CVImageBuffer?,
                            dropped: Bool, isError: Bool) {
        lock.lock()
        let surface = surfaces[windowID]
        let session = sessions[windowID]
        session?.noteDecodedOrNothing(dropped: dropped, isError: isError)
        lock.unlock()
        guard !dropped, !isError, let pixelBuffer else { return }
        surface?.present(pixelBuffer: pixelBuffer)
    }

    // MARK: NAL parsing (test surface)

    struct ParsedNAL {
        var sps: Data?
        var pps: Data?
        var hasIDR: Bool
    }

    /// Split an Annex-B access unit into NAL units; returns the SPS
    /// (type 7) and PPS (type 8) payloads, and whether an IDR (type 5)
    /// or non-IDR (type 1) slice is present.
    static func parseNalUnits(_ payload: Data) -> ParsedNAL {
        var result = ParsedNAL(hasIDR: false)
        var bytes = [UInt8](payload)
        // Normalize 4-byte start codes (0x00000001) to 3-byte (0x000001).
        if let idx = bytes.indices.first(where: { i in
            i + 3 < bytes.count && bytes[i] == 0 && bytes[i + 1] == 0
                && bytes[i + 2] == 0 && bytes[i + 3] == 1
        }) {
            bytes.remove(at: idx + 2)
        }
        var current: [UInt8]?
        var type: UInt8 = 0
        func commit() {
            guard let nal = current else { return }
            switch type {
            case 7: result.sps = Data(nal)
            case 8: result.pps = Data(nal)
            case 1, 5: result.hasIDR = true
            default: break
            }
            current = nil
        }
        var i = 0
        while i < bytes.count {
            if i + 2 < bytes.count && bytes[i] == 0 && bytes[i + 1] == 0
                && bytes[i + 2] == 1 {
                commit()
                i += 3
                if i < bytes.count {
                    type = bytes[i] & 0x1F
                    current = [UInt8]()
                }
            } else if var nal = current {
                nal.append(bytes[i])
                current = nal
                i += 1
            } else {
                i += 1
            }
        }
        commit()
        return result
    }

    /// Annex-B → AVCC: strip start codes, prefix each NAL unit with a
    /// 4-byte big-endian length (matches the session's
    /// `NALUnitHeaderLength = 4`).
    static func annexBToAVCC(_ annexB: Data) -> Data {
        var out = Data()
        annexB.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
            let bytes = raw.bindMemory(to: UInt8.self)
            let count = bytes.count
            var i = 0
            func isStartCode(_ j: Int) -> Bool {
                guard j + 2 < count else { return false }
                if bytes[j] == 0 && bytes[j + 1] == 0 && bytes[j + 2] == 1 { return true }
                return j + 3 < count && bytes[j] == 0 && bytes[j + 1] == 0
                    && bytes[j + 2] == 0 && bytes[j + 3] == 1
            }
            while i < count {
                if isStartCode(i) {
                    let start = bytes[i + 2] == 1 ? i + 3 : i + 4
                    var nal = Data()
                    var j = start
                    while j < count {
                        if isStartCode(j) { break }
                        nal.append(bytes[j])
                        j += 1
                    }
                    guard !nal.isEmpty else { i = j; continue }
                    var len = UInt32(nal.count).bigEndian
                    withUnsafeBytes(of: &len) { out.append(contentsOf: $0) }
                    out.append(nal)
                    i = j
                } else {
                    i += 1
                }
            }
        }
        return out
    }
}
