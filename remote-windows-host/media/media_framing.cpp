
#include "media/media_framing.hpp"

#include "core/sha256.hpp"

namespace remotewindows::media {

namespace {

void putU16Le(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v & 0xFF);
  p[1] = uint8_t((v >> 8) & 0xFF);
}

void putU32Le(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v & 0xFF);
  p[1] = uint8_t((v >> 8) & 0xFF);
  p[2] = uint8_t((v >> 16) & 0xFF);
  p[3] = uint8_t((v >> 24) & 0xFF);
}

uint16_t getU16Le(const uint8_t* p) {
  return uint16_t(p[0] | (uint16_t(p[1]) << 8));
}

uint32_t getU32Le(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
         (uint32_t(p[3]) << 24);
}

}  // namespace

std::vector<uint8_t> serializeHeader(const MediaHeader& h) {
  std::vector<uint8_t> out(kMediaHeaderBytes, 0);
  out[0] = kMediaMagic;
  out[1] = kMediaFormatVersion;
  putU16Le(&out[2], h.stream_id);
  out[4] = h.flags;
  putU32Le(&out[5], h.frame_seq);
  putU32Le(&out[9], h.payload_len);
  return out;
}

bool parseHeader(const uint8_t* buf, size_t len, MediaHeader& out) {
  if (buf == nullptr || len < kMediaHeaderBytes) return false;
  if (buf[0] != kMediaMagic) return false;
  if (buf[1] != kMediaFormatVersion) return false;
  MediaHeader h;
  h.stream_id = getU16Le(&buf[2]);
  h.flags = buf[4];
  h.frame_seq = getU32Le(&buf[5]);
  h.payload_len = getU32Le(&buf[9]);
  if (h.payload_len > kMediaMaxPayloadBytes) return false;
  out = h;
  return true;
}

bool streamIdFor(const std::string& window_id, uint16_t& out) {
  remotewindows::core::Sha256 h;
  h.update(window_id);
  const std::vector<uint8_t> d = h.finalize();
  if (d.size() < kMediaStreamIdBytes) return false;
  out = uint16_t(d[0] | (uint16_t(d[1]) << 8));  // little-endian read
  return true;
}

std::array<uint8_t, kMediaNonceBytes> buildNonce(
    const std::array<uint8_t, kMediaNoncePrefixBytes>& prefix,
    uint32_t counter) {
  std::array<uint8_t, kMediaNonceBytes> nonce{};
  for (size_t i = 0; i < kMediaNoncePrefixBytes; ++i)
    nonce[i] = prefix[i];
  nonce[8] = uint8_t(counter & 0xFF);
  nonce[9] = uint8_t((counter >> 8) & 0xFF);
  nonce[10] = uint8_t((counter >> 16) & 0xFF);
  nonce[11] = uint8_t((counter >> 24) & 0xFF);
  return nonce;
}

}  // namespace remotewindows::media
