
#include <gtest/gtest.h>
#include "core/input.hpp"

using namespace remotewindows::core;

TEST(Input, CmdCTranslatesToCtrlC) {
  ModState mods;
  mods.cmd = true;
  auto ev = translateMacKey(kMacC, true, false, mods);
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->vk, kVkC);
  EXPECT_EQ(ev->modifier_bits, kWinModWin);
}

TEST(Input, CmdVIsVKV) {
  ModState mods;
  mods.cmd = true;
  auto ev = translateMacKey(kMacV, true, false, mods);
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->vk, kVkV);
}

TEST(Input, DigitsTranslate) {
  auto ev = translateMacKey(kMacD5, true, false, ModState{});
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->vk, kVkD5);
}

TEST(Input, FunctionKeysTranslate) {
  auto ev = translateMacKey(kMacF5, true, false, ModState{});
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->vk, kVkF5);
}

TEST(Input, ModifierKeysHaveTheirOwnVk) {
  auto ev = translateMacKey(kMacShiftL, true, false, ModState{});
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->vk, kVkLShift);
  ev = translateMacKey(kMacCommandL, false, false, ModState{});
  ASSERT_TRUE(ev.has_value());
  EXPECT_EQ(ev->vk, kVkLWin);
}

TEST(Input, UnknownKeyYieldsNoEvent) {
  EXPECT_FALSE(translateMacKey(9999, true, false, ModState{}).has_value());
}

TEST(Input, StuckKeyTrackingAndReset) {
  KeyStateTracker t;
  ModState mods;
  mods.cmd = true;
  auto c = *translateMacKey(kMacC, true, false, mods);
  t.apply(c);
  EXPECT_TRUE(t.hasPressed());
  EXPECT_EQ(t.keyCount(), 1u);
  auto releases = t.resetAll();
  ASSERT_EQ(releases.size(), 1u);
  EXPECT_FALSE(releases[0].key_down);
  EXPECT_EQ(releases[0].vk, kVkC);
  EXPECT_FALSE(t.hasPressed());
  EXPECT_EQ(t.resetAll().size(), 0u);
}

TEST(Input, MouseButtonTracking) {
  KeyStateTracker t;
  t.mouseDown(0);
  t.mouseDown(0);  // duplicate
  t.mouseDown(1);
  EXPECT_EQ(t.mousePressed().size(), 2u);
  t.mouseUp(0);
  EXPECT_EQ(t.mousePressed().size(), 1u);
  t.resetMouse();
  EXPECT_EQ(t.mousePressed().size(), 0u);
}

TEST(Input, ShortcutConflictDetection) {
  // {mac_key, shift, ctrl, alt, cmd, enabled, id}
  ShortcutRule a{kMacC, false, false, false, true, true, "cmd-c"};  // Cmd+C
  ShortcutRule b = a;
  b.id = "other-cmd-c";
  EXPECT_TRUE(rulesConflict(a, b));   // identical source combo, different id
  ShortcutRule d = a;
  d.mac_key = kMacD;
  EXPECT_FALSE(rulesConflict(a, d));  // different key
  ShortcutRule s = a;
  s.shift = true;
  EXPECT_FALSE(rulesConflict(a, s));  // different modifier set
}
