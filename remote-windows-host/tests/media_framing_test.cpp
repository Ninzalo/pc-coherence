
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "core/sha256.hpp"
#include "media/media_framing.hpp"

using namespace remotewindows::media;

TEST(MediaFraming, ConstantsMatchProtocol94) {
  EXPECT_EQ(kMediaMagic, 0x52u);
  EXPECT_EQ(kMediaFormatVersion, 1u);
  EXPECT_EQ(kMediaStreamIdBytes, 2u);
  EXPECT_EQ(kMediaGcmTagBytes, 16u);
  EXPECT_EQ(kMediaNonceBytes, 12u);
  EXPECT_EQ(kMediaNoncePrefixBytes, 8u);
  EXPECT_EQ(kMediaMaxPayloadBytes, 16u * 1024u * 1024u);
  EXPECT_EQ(kFlagKeyframe, 0x01u);
  // Header = the exact §9.4 field list: u8+u8+u16+u8+u32+u32.
  EXPECT_EQ(kMediaHeaderBytes, 13u);
}

TEST(MediaFraming, SerializeExactWireBytes) {
  MediaHeader h;
  h.stream_id = 0x1234;
  h.flags = kFlagKeyframe;
  h.frame_seq = 0x0000000A;
  h.payload_len = 0x00010020;  // ciphertext bytes = plaintext + 16

  const std::vector<uint8_t> wire = serializeHeader(h);
  // Exact little-endian bytes per §9.4:
  // magic | version | stream_id LE | flags | frame_seq LE | payload_len LE
  const std::vector<uint8_t> expected = {
      0x52, 0x01, 0x34, 0x12, 0x01, 0x0A, 0x00, 0x00, 0x00, 0x20, 0x00,
      0x01, 0x00};
  EXPECT_EQ(wire.size(), 13u);
  EXPECT_EQ(wire, expected);
}

TEST(MediaFraming, SerializeParseRoundTrip) {
  MediaHeader in;
  in.stream_id = 0xBEEF;
  in.flags = 0x00;
  in.frame_seq = 0xDEADBEEF;
  in.payload_len = 16 * 1024 * 1024;  // maximum allowed

  const std::vector<uint8_t> wire = serializeHeader(in);
  MediaHeader out{};
  ASSERT_TRUE(parseHeader(wire.data(), wire.size(), out));
  EXPECT_EQ(out.stream_id, in.stream_id);
  EXPECT_EQ(out.flags, in.flags);
  EXPECT_EQ(out.frame_seq, in.frame_seq);
  EXPECT_EQ(out.payload_len, in.payload_len);
}

TEST(MediaFraming, ParseRejectsShortBuffer) {
  MediaHeader out{};
  const std::vector<uint8_t> wire = serializeHeader({});
  for (size_t len = 0; len < kMediaHeaderBytes; ++len)
    EXPECT_FALSE(parseHeader(wire.data(), len, out)) << "len=" << len;
}

TEST(MediaFraming, ParseRejectsBadMagic) {
  MediaHeader out{};
  std::vector<uint8_t> wire = serializeHeader({});
  wire[0] = 0x53;  // 'S', not 'R'
  EXPECT_FALSE(parseHeader(wire.data(), wire.size(), out));
}

TEST(MediaFraming, ParseRejectsBadVersion) {
  MediaHeader out{};
  std::vector<uint8_t> wire = serializeHeader({});
  wire[1] = 2;
  EXPECT_FALSE(parseHeader(wire.data(), wire.size(), out));
}

TEST(MediaFraming, ParseRejectsOversizePayloadLen) {
  MediaHeader out{};
  std::vector<uint8_t> wire = serializeHeader({});
  // payload_len = 16 MiB + 1 = 0x00010001, little-endian in bytes 9..12.
  wire[9] = 0x01;
  wire[10] = 0x00;
  wire[11] = 0x00;
  wire[12] = 0x01;
  EXPECT_FALSE(parseHeader(wire.data(), wire.size(), out));
  // Exactly 16 MiB = 0x00010000 is allowed.
  wire[9] = 0x00;
  wire[10] = 0x00;
  wire[11] = 0x00;
  wire[12] = 0x01;
  EXPECT_TRUE(parseHeader(wire.data(), wire.size(), out));
  EXPECT_EQ(out.payload_len, 16u * 1024u * 1024u);
}

TEST(MediaFraming, StreamIdForIsFirstTwoSha256BytesLe) {
  const std::string window_id = "11111111-2222-3333-4444-555555555555";
  uint16_t id = 0;
  ASSERT_TRUE(streamIdFor(window_id, id));

  // Hand-computed reference: first 2 bytes of core::Sha256, read LE.
  remotewindows::core::Sha256 h;
  h.update(window_id);
  const std::vector<uint8_t> d = h.finalize();
  ASSERT_EQ(d.size(), 32u);
  EXPECT_EQ(id, uint16_t(d[0] | (uint16_t(d[1]) << 8)));
}

TEST(MediaFraming, BuildNonceExactLayout) {
  std::array<uint8_t, kMediaNoncePrefixBytes> prefix{};
  for (size_t i = 0; i < kMediaNoncePrefixBytes; ++i) prefix[i] = uint8_t(0x10 + i);

  const std::array<uint8_t, kMediaNonceBytes> nonce =
      buildNonce(prefix, 0x01020304);
  ASSERT_EQ(nonce.size(), 12u);
  // prefix (8 bytes) || counter u32 LE
  for (size_t i = 0; i < kMediaNoncePrefixBytes; ++i)
    EXPECT_EQ(nonce[i], prefix[i]);
  EXPECT_EQ(nonce[8], 0x04);
  EXPECT_EQ(nonce[9], 0x03);
  EXPECT_EQ(nonce[10], 0x02);
  EXPECT_EQ(nonce[11], 0x01);
}
