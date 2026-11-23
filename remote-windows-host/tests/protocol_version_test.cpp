#include <gtest/gtest.h>
#include "core/protocol_version.hpp"

using namespace remotewindows::protocol;

namespace {
std::optional<Version> parse(const std::string& s) {
  Version v;
  return parseVersion(s, v) ? std::make_optional(v) : std::nullopt;
}
}  // namespace

TEST(ProtocolVersion, ParsesSemver) {
  auto v = parse("0.1.0");
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(v->major, 0);
  EXPECT_EQ(v->minor, 1);
  EXPECT_EQ(v->patch, 0);
  EXPECT_EQ(v->toString(), "0.1.0");
}

TEST(ProtocolVersion, RejectsGarbage) {
  EXPECT_FALSE(parse("").has_value());
  EXPECT_FALSE(parse("1").has_value());
  EXPECT_FALSE(parse("1.2").has_value());
  EXPECT_FALSE(parse("a.b.c").has_value());
  EXPECT_FALSE(parse("1.2.3.4").has_value());
  EXPECT_FALSE(parse("1.2.3-x").has_value());
  EXPECT_FALSE(parse("-1.2.3").has_value());
}

TEST(ProtocolVersion, CompatibleRules) {
  // Same major required; minor mismatch tolerated.
  EXPECT_TRUE(compatible(0, 1, 0, 1));
  EXPECT_TRUE(compatible(0, 1, 0, 2));
  EXPECT_TRUE(compatible(0, 2, 0, 1));
  EXPECT_FALSE(compatible(0, 1, 1, 0));
  EXPECT_FALSE(compatible(1, 0, 0, 9));
}
