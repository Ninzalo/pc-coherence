
#ifndef RWH_MEDIA_FRAMING_HPP
#define RWH_MEDIA_FRAMING_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace remotewindows::media {

// PROTOCOL.md §9.4 (normative, added by RW-H-006): binary framing of the
// dedicated TCP media channel (47988). All integers little-endian.
//
// Wire header — 13 bytes, exactly the §9.4 field list:
//   u8 magic | u8 version | u16 LE stream_id | u8 flags |
//   u32 LE frame_seq | u32 LE payload_len
// followed by `payload_len` bytes of ciphertext (plaintext + 16-byte GCM
// tag appended). The 12-byte nonce is NEVER on the wire:
// nonce = nonce_prefix(8) || packet_counter (u32 LE), §9.3.

constexpr uint8_t kMediaMagic = 0x52;  // 'R'
constexpr uint8_t kMediaFormatVersion = 1;
constexpr size_t kMediaStreamIdBytes = 2;
constexpr size_t kMediaGcmTagBytes = 16;
constexpr size_t kMediaNonceBytes = 12;
constexpr size_t kMediaNoncePrefixBytes = 8;
constexpr uint32_t kMediaMaxPayloadBytes = 16 * 1024 * 1024;
constexpr uint8_t kFlagKeyframe = 0x01;  // flags bit 0

// Header size = 1 + 1 + 2 + 1 + 4 + 4 (the §9.4 field list).
constexpr size_t kMediaHeaderBytes = 13;

// One media packet header (magic/version are fixed on the wire; they are
// not carried in the struct).
struct MediaHeader {
  uint16_t stream_id = 0;  // first 2 bytes of sha256(window_id), LE
  uint8_t flags = 0;       // bit0 = keyframe
  uint32_t frame_seq = 0;  // per stream, starts at 0, wraps mod 2^32
  uint32_t payload_len = 0;  // ciphertext length = plaintext + 16 (tag)
};

// Serializes `h` to the exact 13-byte §9.4 wire header (LE).
std::vector<uint8_t> serializeHeader(const MediaHeader& h);

// Parses a §9.4 header from `buf` (first kMediaHeaderBytes bytes).
// Returns false if `len < kMediaHeaderBytes`, the magic or version does
// not match, or `payload_len > kMediaMaxPayloadBytes` (§9.6: malformed).
bool parseHeader(const uint8_t* buf, size_t len, MediaHeader& out);

// §9.4: stream_id = first 2 bytes of core::Sha256 over the UTF-8
// window_id, read little-endian.
bool streamIdFor(const std::string& window_id, uint16_t& out);

// §9.3/§9.4: nonce = nonce_prefix (8 bytes) || counter (u32 LE).
std::array<uint8_t, kMediaNonceBytes> buildNonce(
    const std::array<uint8_t, kMediaNoncePrefixBytes>& prefix,
    uint32_t counter);

}  // namespace remotewindows::media

#endif  // RWH_MEDIA_FRAMING_HPP
