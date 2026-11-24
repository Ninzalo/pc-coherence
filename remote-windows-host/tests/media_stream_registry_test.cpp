
#include <gtest/gtest.h>

#include <map>
#include <string>

#include "media/stream_registry.hpp"

using namespace remotewindows::media;

namespace {

// Synthetic 64-lowercase-hex session key (not a real secret).
const std::string kSessionKeyHex =
    "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

const std::string kWindowA = "11111111-2222-3333-4444-555555555555";
const std::string kWindowB = "99999999-8888-7777-6666-555555555555";

}  // namespace

TEST(MediaStreamRegistry, AddFindRemove) {
  StreamRegistry reg;
  EXPECT_EQ(reg.size(), 0u);
  EXPECT_EQ(reg.find(kWindowA), nullptr);

  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));
  EXPECT_EQ(reg.size(), 1u);

  const MediaStream* s = reg.find(kWindowA);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(s->window_id, kWindowA);

  uint16_t id = 0;
  ASSERT_TRUE(streamIdFor(kWindowA, id));
  EXPECT_EQ(s->stream_id, id);
  EXPECT_EQ(s->packet_counter, 0u);
  EXPECT_EQ(s->frame_seq, 0u);
  EXPECT_TRUE(s->keyframe_due);

  ASSERT_TRUE(reg.remove(kWindowA));
  EXPECT_EQ(reg.size(), 0u);
  EXPECT_EQ(reg.find(kWindowA), nullptr);
  EXPECT_FALSE(reg.remove(kWindowA));  // already gone
}

TEST(MediaStreamRegistry, FpsGatePresentWithCoreDefaults) {
  StreamRegistry reg;
  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));
  const MediaStream* s = reg.find(kWindowA);
  ASSERT_NE(s, nullptr);
  // core::FpsGate defaults: target_fps = 30 (core/fps_policy.hpp).
  EXPECT_EQ(s->fps_gate.target_fps, 30u);
}

TEST(MediaStreamRegistry, IdempotentAddResetsCounters) {
  StreamRegistry reg;
  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));

  // Mutate per-connection state as a capture loop would.
  const MediaStream* s1 = reg.find(kWindowA);
  ASSERT_NE(s1, nullptr);
  const std::array<uint8_t, 32> key_before = s1->key;

  // Re-add: re-derives state, resets counters (§9.5: fresh keyframe due,
  // frame_seq/packet_counter back to 0).
  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));
  EXPECT_EQ(reg.size(), 1u);  // still one stream (idempotent)

  const MediaStream* s2 = reg.find(kWindowA);
  ASSERT_NE(s2, nullptr);
  EXPECT_EQ(s2->packet_counter, 0u);
  EXPECT_EQ(s2->frame_seq, 0u);
  EXPECT_TRUE(s2->keyframe_due);
  EXPECT_EQ(s2->key, key_before);  // same session key ⇒ same derived key
  EXPECT_EQ(s2->stream_id, s1->stream_id);
}

TEST(MediaStreamRegistry, AddRejectsMalformedSessionKey) {
  StreamRegistry reg;
  EXPECT_FALSE(reg.add(kWindowA, "not-a-hex-key"));
  EXPECT_FALSE(reg.add(kWindowA, std::string(64, 'z')));
  EXPECT_EQ(reg.size(), 0u);  // no partial state published
}

TEST(MediaStreamRegistry, DistinctWindowIdsDistinctStreamIds) {
  StreamRegistry reg;
  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));
  ASSERT_TRUE(reg.add(kWindowB, kSessionKeyHex));
  EXPECT_EQ(reg.size(), 2u);
  const MediaStream* a = reg.find(kWindowA);
  const MediaStream* b = reg.find(kWindowB);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_NE(a->stream_id, b->stream_id);
}

TEST(MediaStreamRegistry, FindByStreamId) {
  StreamRegistry reg;
  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));
  ASSERT_TRUE(reg.add(kWindowB, kSessionKeyHex));

  const MediaStream* a = reg.find(kWindowA);
  const MediaStream* b = reg.find(kWindowB);
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);

  EXPECT_EQ(reg.findByStreamId(a->stream_id), a);
  EXPECT_EQ(reg.findByStreamId(b->stream_id), b);
  // An id no stream has: pick one that is not either registered id.
  uint16_t missing = a->stream_id;
  while (missing == a->stream_id || missing == b->stream_id)
    missing = uint16_t(missing + 1);
  EXPECT_EQ(reg.findByStreamId(missing), nullptr);
}

TEST(MediaStreamRegistry, CollisionLastAddWins) {
  // 16-bit stream_id space: find two distinct window_ids whose
  // sha256(window_id) first 2 bytes (LE) collide, and verify the
  // documented behavior — last add wins for findByStreamId, exact
  // window_id lookup is unaffected.
  std::map<uint16_t, std::string> seen;
  std::string first, second;
  uint16_t colliding_id = 0;
  for (uint32_t i = 0; i < 200000 && second.empty(); ++i) {
    const std::string wid = "probe-" + std::to_string(i);
    uint16_t id = 0;
    ASSERT_TRUE(streamIdFor(wid, id));
    auto it = seen.find(id);
    if (it == seen.end()) {
      seen[id] = wid;
    } else {
      first = it->second;
      second = wid;
      colliding_id = id;
    }
  }
  ASSERT_FALSE(second.empty())
      << "no 16-bit stream_id collision found in 200000 probes";

  StreamRegistry reg;
  ASSERT_TRUE(reg.add(first, kSessionKeyHex));
  ASSERT_EQ(reg.findByStreamId(colliding_id), reg.find(first));
  ASSERT_TRUE(reg.add(second, kSessionKeyHex));  // colliding add, later
  // Last add wins the stream_id lookup...
  EXPECT_EQ(reg.findByStreamId(colliding_id), reg.find(second));
  // ...and exact window_id lookups always return the right stream.
  const MediaStream* f = reg.find(first);
  const MediaStream* s = reg.find(second);
  ASSERT_NE(f, nullptr);
  ASSERT_NE(s, nullptr);
  EXPECT_EQ(f->window_id, first);
  EXPECT_EQ(s->window_id, second);
  EXPECT_EQ(reg.size(), 2u);

  // Removing the earlier (shadowed) stream keeps the later one visible.
  ASSERT_TRUE(reg.remove(first));
  EXPECT_EQ(reg.findByStreamId(colliding_id), reg.find(second));
  // Removing the last add frees the id again.
  ASSERT_TRUE(reg.remove(second));
  EXPECT_EQ(reg.findByStreamId(colliding_id), nullptr);
  EXPECT_EQ(reg.size(), 0u);
}

TEST(MediaStreamRegistry, RemoveFreesState) {
  StreamRegistry reg;
  ASSERT_TRUE(reg.add(kWindowA, kSessionKeyHex));
  ASSERT_TRUE(reg.add(kWindowB, kSessionKeyHex));
  const MediaStream* a = reg.find(kWindowA);
  ASSERT_NE(a, nullptr);
  ASSERT_TRUE(reg.remove(kWindowA));
  EXPECT_EQ(reg.findByStreamId(a->stream_id), nullptr);
  EXPECT_EQ(reg.find(kWindowA), nullptr);
  EXPECT_NE(reg.find(kWindowB), nullptr);  // the other stream survives
  EXPECT_EQ(reg.size(), 1u);
}
