import Foundation
import CryptoKit

/// PROTOCOL.md §9.4 — per-stream AES-256-GCM for the media channel.
/// The Client uses CryptoKit's `AES.GCM`; the Host uses Windows CNG
/// `BCrypt*`. The golden vectors in
/// `Protocol/Contract/test-vectors/media/aes-256-gcm.json` MUST be
/// reproduced exactly by both backends — that is the cross-language
/// compatibility proof.
///
/// Wire format of one packet's payload: `ciphertext || tag` (the tag is
/// the 16-byte GCM authentication tag, appended). AAD is empty.
public enum MediaAead {
    /// Typed authentication failure: a tampered ciphertext or tag. The
    /// §9.6 receiving side closes the media connection.
    public enum MediaAeadError: Error, Equatable {
        case tampered
        /// Payload/key/nonce shorter than the required length.
        case shortInput(length: Int)
    }

    /// Seal `plaintext` under an explicit 12-byte nonce, returning
    /// (ciphertext, tag). The golden-vector surface: the vectors carry
    /// fixed 12-byte nonces, not the per-stream prefix+counter form.
    public static func seal(plaintext: Data, keyData: Data,
                            nonceData: Data) throws -> (ciphertext: Data, tag: Data) {
        let key = try makeKey(keyData)
        let nonce = try makeNonce(nonceData)
        let box = try AES.GCM.seal(plaintext, using: key, nonce: nonce)
        return (box.ciphertext, box.tag)
    }

    /// Open a wire payload (`ciphertext || tag`) under an explicit
    /// 12-byte nonce. A failed authentication throws
    /// `MediaAeadError.tampered`. The split is done in array space:
    /// in this Swift 6.4.2 CLT Foundation build, `Data.subdata(in:)`
    /// on buffers produced by CryptoKit `seal` + `append` traps with
    /// EXC_BREAKPOINT (verified 2026-11-25), while `[UInt8](data)`
    /// construction and `Data(ArraySlice)` are safe.
    public static func open(payload: Data, keyData: Data,
                            nonceData: Data) throws -> Data {
        guard payload.count >= MediaFraming.gcmTagBytes else {
            throw MediaAeadError.shortInput(length: payload.count)
        }
        let bytes = [UInt8](payload)
        let split = bytes.count - MediaFraming.gcmTagBytes
        return try open(ciphertext: Data(bytes[0..<split]),
                        tag: Data(bytes[split..<bytes.count]),
                        keyData: keyData, nonceData: nonceData)
    }

    /// Open with separated ciphertext and tag.
    public static func open(ciphertext: Data, tag: Data, keyData: Data,
                            nonceData: Data) throws -> Data {
        let key = try makeKey(keyData)
        let nonce = try makeNonce(nonceData)
        let box = try AES.GCM.SealedBox(nonce: nonce,
                                        ciphertext: Data(ciphertext),
                                        tag: Data(tag))
        do {
            return try AES.GCM.open(box, using: key)
        } catch {
            throw MediaAeadError.tampered
        }
    }

    private static func makeKey(_ keyData: Data) throws -> SymmetricKey {
        guard keyData.count == 32 else {
            throw MediaAeadError.shortInput(length: keyData.count)
        }
        return SymmetricKey(data: keyData)
    }

    private static func makeNonce(_ nonceData: Data) throws -> AES.GCM.Nonce {
        guard nonceData.count == MediaFraming.nonceBytes else {
            throw MediaAeadError.shortInput(length: nonceData.count)
        }
        return try AES.GCM.Nonce(data: nonceData)
    }
}

/// Per-stream cipher state (one instance per `window_id`): the derived
/// `media_key` + `nonce_prefix` plus the per-stream 4-byte frame
/// counter. The 12-byte nonce for frame N is
/// `nonce_prefix(8) || N (u32 LE)` — §9.3/§9.4.
///
/// `seal` uses the internal counter and increments it per frame (the
/// Host-side direction / test loopback). `open` takes the explicit
/// counter — on the receive path it is the header's `frame_seq`, which
/// tracks the sender's packet counter 1:1 (no retransmission; TCP is
/// in-order, §9.6).
public final class MediaStreamCipher {
    private let key: SymmetricKey
    private let rawKey: Data
    private let noncePrefix: Data   // 8 bytes
    private var frameCounter: UInt32 = 0
    private let lock = NSLock()

    /// `keyData` must be 32 bytes and `noncePrefix` 8 bytes (validated
    /// up front; CryptoKit would otherwise reject at use time).
    public init(keyData: Data, noncePrefix: Data) {
        guard keyData.count == 32, noncePrefix.count == MediaFraming.noncePrefixBytes
        else {
            // Defensive: the derive path never produces these sizes.
            fatalError("MediaStreamCipher: bad key/noncePrefix size")
        }
        self.key = SymmetricKey(data: keyData)
        self.rawKey = keyData
        self.noncePrefix = noncePrefix
    }

    /// The counter the NEXT `seal` will consume (for inspection/tests).
    public var nextFrameCounter: UInt32 {
        lock.lock(); defer { lock.unlock() }
        return frameCounter
    }

    /// Seal `plaintext` with the next counter; the counter is consumed
    /// (increments per frame, wraps mod 2^32 with the frame_seq). The
    /// wire payload `ciphertext || tag` is built in array space — no
    /// in-place `Data.append` on CryptoKit's ciphertext (the
    /// representation that traps `Data.subdata` on this toolchain).
    public func seal(_ plaintext: Data) throws -> Data {
        let counter: UInt32
        lock.lock()
        counter = frameCounter
        frameCounter = frameCounter &+ 1
        lock.unlock()
        let box = try AES.GCM.seal(plaintext,
                                   using: key,
                                   nonce: makeNonce(counter: counter))
        var out = [UInt8](box.ciphertext)
        out.append(contentsOf: [UInt8](box.tag))
        return Data(out)
    }

    /// Seal `plaintext` with an EXPLICIT `frameSeq` used as the nonce
    /// `packet_counter`. §9.4: the nonce is never on the wire — the
    /// receiver reconstructs its counter from the header's `frame_seq`
    /// — so the sender MUST encrypt with `packet_counter == frame_seq`
    /// (the same value it puts in the header). The internal counter is
    /// synced to the next value so `nextFrameCounter` stays meaningful.
    public func seal(_ plaintext: Data, frameSeq: UInt32) throws -> Data {
        lock.lock()
        frameCounter = frameSeq &+ 1
        lock.unlock()
        let box = try AES.GCM.seal(plaintext,
                                   using: key,
                                   nonce: makeNonce(counter: frameSeq))
        var out = [UInt8](box.ciphertext)
        out.append(contentsOf: [UInt8](box.tag))
        return Data(out)
    }

    /// Open a wire payload (`ciphertext || tag`) for `frameSeq` — the
    /// sender's packet counter. Throws `MediaAeadError.tampered` on a
    /// bad tag.
    public func open(_ payload: Data, frameSeq: UInt32) throws -> Data {
        try MediaAead.open(payload: payload, keyData: rawKey,
                           nonceData: mediaNonce(prefix: noncePrefix, counter: frameSeq))
    }

    private func makeNonce(counter: UInt32) throws -> AES.GCM.Nonce {
        try AES.GCM.Nonce(data: mediaNonce(prefix: noncePrefix, counter: counter))
    }
}
