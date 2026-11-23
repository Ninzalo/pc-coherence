
#include <gtest/gtest.h>
#include "core/window_relationships.hpp"

using namespace remotewindows::core;

namespace {
SharedSession session() {
  SharedSession s;
  s.session_id = "sess-1";
  s.primary_pid = 100;
  s.member_hwnds = {"0x1000"};
  s.known_pids = {100, 101};
  return s;
}
}  // namespace

TEST(WindowRelationships, OwnerInSessionAssociates) {
  WindowEvidence w;
  w.hwnd = "0x2000";
  w.process_id = 999;  // different process!
  w.owner_hwnd = "0x1000";
  auto d = decideAssociation(w, session());
  EXPECT_TRUE(d.associated);
  EXPECT_EQ(d.reason, "owner_in_session");
}

TEST(WindowRelationships, RootOwnerInSessionAssociates) {
  WindowEvidence w;
  w.hwnd = "0x2001";
  w.process_id = 999;
  w.root_owner_hwnd = "0x1000";
  auto d = decideAssociation(w, session());
  EXPECT_TRUE(d.associated);
  EXPECT_EQ(d.reason, "root_owner_in_session");
}

TEST(WindowRelationships, PidAloneNeverAssociates) {
  WindowEvidence w;
  w.hwnd = "0x2002";
  w.process_id = 101;  // known pid but no structural evidence
  auto d = decideAssociation(w, session());
  EXPECT_FALSE(d.associated);
  EXPECT_EQ(d.reason, "not_associated");
}

TEST(WindowRelationships, PidPlusModalAssociates) {
  WindowEvidence w;
  w.hwnd = "0x2003";
  w.process_id = 101;
  w.is_modal = true;
  auto d = decideAssociation(w, session());
  EXPECT_TRUE(d.associated);
  EXPECT_EQ(d.reason, "pid_in_known_app_processes");
}

TEST(WindowRelationships, IdentityStablePerLifetime) {
  WindowIdentityAllocator alloc;
  auto id1 = alloc.allocate("0x1000");
  auto id2 = alloc.allocate("0x1000");
  EXPECT_EQ(id1, id2);
  auto other = alloc.allocate("0x3000");
  EXPECT_NE(id1, other);
  EXPECT_EQ(alloc.count(), 2u);
  EXPECT_EQ(alloc.lookup("0x1000"), id1);
}

TEST(WindowRelationships, ReusedHandleGetsFreshIdentity) {
  WindowIdentityAllocator alloc;
  auto first = alloc.allocate("0x1000");
  alloc.release("0x1000");
  EXPECT_EQ(alloc.lookup("0x1000"), std::nullopt);
  auto second = alloc.allocate("0x1000");
  EXPECT_NE(first, second);
  EXPECT_EQ(alloc.count(), 1u);
}
