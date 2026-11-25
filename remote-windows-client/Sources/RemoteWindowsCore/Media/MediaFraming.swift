import Foundation
import CryptoKit

/// PROTOCOL.md §9.4 — binary framing of the dedicated TCP media channel
/// (47988). All integers little-endian. This is the CLIENT mirror of the
/// Host's `media/media_framing.cpp` (RW-H-007); the two implementations
/// are byte-identical by contract.
///
/// Wire header — 13 bytes, exactly the §9.4 field list:
///   u8 magic | u8 version | u16 LE stream_id | u8 flags |
///   u32 LE frame_seq | u32 LE payload_len
/// followed by `payload_len` bytes of ciphertext (plaintext + 16-byte
/// GCM tag appended). The 12-byte nonce is NEVER on the wire:
/// nonce = nonce_prefix(8) || packet_counter (u32 LE), §9.3.
public enum MediaFraming {
    public static let magic: UInt8 = 0x52            // 'R'
    public static let formatVersion: UInt8 = 1
    public static let streamIdBytes = 2
    public static let gcmTagBytes = 16
    public static let nonceBytes = 12
    public static let noncePrefixBytes = 8
    /// `payload_len` MUST be ≤ 16 MiB (§9.4/§9.6: otherwise malformed).
    public static let maxPayloadLen = 16 * 1024 * 1024
    public static let flagKeyframe: UInt8 = 0x01     // flags bit 0

    /// Header size = 1 + 1 + 2 + 1 + 4 + 4 (the §9.4 field list).
    public static let headerBytes = 13
}

/// One media packet header (magic/version are fixed on the wire; they
/// are not carried in the struct — mirrors the Host's `MediaHeader`).
public struct MediaHeader: Equatable {
    /// First 2 bytes of `sha256(window_id)`, read little-endian.
    public var streamId: UInt16
    /// bit0 = keyframe.
    public var flags: UInt8
    /// Per stream, starts at 0, wraps mod 2^32.
    public var frameSeq: UInt32
    /// Ciphertext length = plaintext + 16 (tag).
    public var payloadLen: UInt32

    public init(streamId: UInt16, flags: UInt8 = 0,
                frameSeq: UInt32 = 0, payloadLen: UInt32 = 0) {
        self.streamId = streamId
        self.flags = flags
        self.frameSeq = frameSeq
        self.payloadLen = payloadLen
    }

    public var isKeyframe: Bool { flags & MediaFraming.flagKeyframe != 0 }
}

/// Typed §9.6 malformed-header failures (the receiving side closes the
/// media connection on any of these).
public enum MediaFramingError: Error, Equatable {
    case shortBuffer(length: Int)
    case badMagic(found: UInt8)
    case badVersion(found: UInt8)
    case oversizedPayload(payloadLen: UInt32)
}

extension MediaHeader {
    /// Serializes to the exact 13-byte §9.4 wire header (little-endian).
    public func serialize() -> Data {
        var out = Data(repeating: 0, count: MediaFraming.headerBytes)
        out[0] = MediaFraming.magic
        out[1] = MediaFraming.formatVersion
        out[2] = UInt8(streamId & 0xFF)
        out[3] = UInt8((streamId >> 8) & 0xFF)
        out[4] = flags
        out[5] = UInt8(frameSeq & 0xFF)
        out[6] = UInt8((frameSeq >> 8) & 0xFF)
        out[7] = UInt8((frameSeq >> 16) & 0xFF)
        out[8] = UInt8((frameSeq >> 24) & 0xFF)
        out[9] = UInt8(payloadLen & 0xFF)
        out[10] = UInt8((payloadLen >> 8) & 0xFF)
        out[11] = UInt8((payloadLen >> 16) & 0xFF)
        out[12] = UInt8((payloadLen >> 24) & 0xFF)
        return out
    }

    /// Parses a §9.4 header from the first 13 bytes of `data`. Fails with
    /// a typed error on a short buffer, bad magic, wrong version, or
    /// `payload_len > 16 MiB` (§9.6). The bytes are copied into an
    /// array (read-only Data subscripts from the test context are proven
    /// safe) and the array-space core does the rest.
    public static func parse(_ data: Data) -> Result<MediaHeader, MediaFramingError> {
        guard data.count >= MediaFraming.headerBytes else {
            return .failure(.shortBuffer(length: data.count))
        }
        let n = MediaFraming.headerBytes
        var b = [UInt8](repeating: 0, count: n)
        for i in 0..<n { b[i] = data[i] }
        return parseBytes(b[...])
    }

    /// Array-space parse core (the hot path): reads through concrete
    /// array subscripts only — no `Data` construction, no Foundation
    /// generic witnesses. Mirrors the baseline `Framing.parseLength`
    /// pattern, which is proven green in the test executable under the
    /// Swift 6.4.2 CLT toolchain.
    public static func parseBytes(_ b: ArraySlice<UInt8>) -> Result<MediaHeader, MediaFramingError> {
        guard b.count >= MediaFraming.headerBytes else {
            return .failure(.shortBuffer(length: b.count))
        }
        guard b[0] == MediaFraming.magic else {
            return .failure(.badMagic(found: b[0]))
        }
        guard b[1] == MediaFraming.formatVersion else {
            return .failure(.badVersion(found: b[1]))
        }
        let streamId = UInt16(b[2]) | (UInt16(b[3]) << 8)
        let frameSeq = UInt32(b[5]) | (UInt32(b[6]) << 8) |
                       (UInt32(b[7]) << 16) | (UInt32(b[8]) << 24)
        let payloadLen = UInt32(b[9]) | (UInt32(b[10]) << 8) |
                         (UInt32(b[11]) << 16) | (UInt32(b[12]) << 24)
        guard payloadLen <= MediaFraming.maxPayloadLen else {
            return .failure(.oversizedPayload(payloadLen: payloadLen))
        }
        return .success(MediaHeader(streamId: streamId, flags: b[4],
                                    frameSeq: frameSeq, payloadLen: payloadLen))
    }
}

extension MediaFraming {
    /// §9.4: `stream_id` = first 2 bytes of `sha256(window_id)` (UTF-8
    /// UUID string), read little-endian. Must match the Host's
    /// `media::streamIdFor` byte-for-byte.
    public static func streamID(forWindowID windowID: String) -> UInt16 {
        let digest = Array(SHA256.hash(data: Data(windowID.utf8)))
        return UInt16(digest[0]) | (UInt16(digest[1]) << 8)
    }
}

/// §9.3/§9.4: nonce = nonce_prefix (8 bytes) || counter (u32 LE).
/// Built in array space (no `Data.subdata`/in-place `Data.append` —
/// see the note on `MediaAead.open`; this CLT Foundation build traps
/// on those for certain buffer representations, verified 2026-11-25).
public func mediaNonce(prefix: Data, counter: UInt32) -> Data {
    let p = [UInt8](prefix)
    var nonce = [UInt8](p[0..<MediaFraming.noncePrefixBytes])
    nonce.append(UInt8(counter & 0xFF))
    nonce.append(UInt8((counter >> 8) & 0xFF))
    nonce.append(UInt8((counter >> 16) & 0xFF))
    nonce.append(UInt8((counter >> 24) & 0xFF))
    return Data(nonce)
}

/// Incremental reassembly of media packets from a byte stream (TCP reads
/// may deliver partial headers/payloads). Feed bytes; collect complete
/// (header, ciphertext) pairs. A terminal malformed header is reported
/// via `malformed` (never thrown) — the packets reassembled before it
/// are still returned, and the caller MUST close the channel with the
/// typed reason (§9.6).
///
/// The reassembly buffer is `[UInt8]` (mirroring the baseline
/// `Framing.FrameReader`). Two hazards were hit and fixed under the
/// Swift 6.4.2 CLT toolchain (verified 2026-11-25):
/// 1. A `break` inside a `switch case` exits the switch, NOT the
///    enclosing `while` — a partial-payload guard written that way
///    spins forever re-parsing the same header. `feed` therefore keeps
///    its guards directly in the loop body.
/// 2. In this Foundation build, shrinking a long-lived `Data` with
///    `removeFirst(_:)` corrupts it so later subscripts trap
///    (EXC_BREAKPOINT). The array buffer never uses that path.
public final class MediaFrameReader {
    private var buffer: [UInt8] = []

    /// Feed more bytes; returns the complete packets reassembled from
    /// THIS feed plus any previously buffered bytes, in order. A
    /// terminal malformed header (bad magic / bad version / oversized
    /// payload) is NOT thrown: it is recorded in `malformed`, the
    /// malformed bytes and anything after them are dropped, and the
    /// packets reassembled before them are still returned — they
    /// arrived in-order on TCP and are valid (§9.6: the malformed
    /// packet is dropped and the connection is reset, but good frames
    /// are not discarded with it). The caller MUST close the channel
    /// with the typed reason once `malformed` is set.
    public func feed(_ data: Data) -> [(header: MediaHeader, ciphertext: Data)] {
        if malformed != nil { return [] }   // terminal state
        buffer.append(contentsOf: data)
        var packets: [(MediaHeader, Data)] = []
        let hlen = MediaFraming.headerBytes
        while buffer.count >= hlen {
            // Header parsed entirely in array space (the baseline-proven
            // pattern — no `Data` construction on this path).
            let result = MediaHeader.parseBytes(buffer[0..<hlen])
            if case .failure(let error) = result {
                malformed = error
                buffer.removeAll(keepingCapacity: true)
                return packets
            }
            guard case .success(let header) = result else { continue }
            let total = hlen + Int(header.payloadLen)
            // NOTE: this `break` must sit directly in the `while` body —
            // a `break` inside a `switch case` exits the switch, not the
            // loop, which turns a partial header into an infinite
            // re-parse spin (observed 2026-11-25).
            guard buffer.count >= total else { break }
            let payloadEnd = total
            // Ciphertext as a fresh Data from an array slice (the
            // baseline-proven construction), then advance the byte
            // buffer with removeSubrange.
            packets.append((header, Data(buffer[hlen..<payloadEnd])))
            buffer.removeSubrange(0..<payloadEnd)
        }
        return packets
    }

    /// A terminal malformed header seen by `feed` (nil = none yet).
    /// Stays set until `reset()`; `feed` returns no packets once set.
    public private(set) var malformed: MediaFramingError?

    public func reset() {
        buffer.removeAll(keepingCapacity: true)
        malformed = nil
    }

    public var bufferedByteCount: Int { buffer.count }
}
