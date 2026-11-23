
#include <gtest/gtest.h>
#include "core/power_policy.hpp"

using namespace remotewindows::core;

TEST(PowerPolicy, UnpairedDenied) {
  PowerController c(PowerExecutor{});
  std::string err, msg;
  PowerRequest r;
  r.action = PowerAction::SLEEP;
  r.paired = false;
  EXPECT_EQ(c.request(r, err, msg), PowerOutcome::DENIED);
  EXPECT_EQ(err, "not_paired");
}

TEST(PowerPolicy, UnauthorizedDenied) {
  PowerController c(PowerExecutor{});
  std::string err, msg;
  PowerRequest r;
  r.action = PowerAction::SHUTDOWN;
  r.paired = true;
  r.authorized = false;
  EXPECT_EQ(c.request(r, err, msg), PowerOutcome::DENIED);
  EXPECT_EQ(err, "not_authorized");
}

TEST(PowerPolicy, AcceptedSleepMessage) {
  bool called = false;
  PowerController c(PowerExecutor{[&](PowerAction, std::string&) {
    called = true;
    return true;
  }});
  std::string err, msg;
  PowerRequest r;
  r.action = PowerAction::SLEEP;
  r.paired = true;
  r.authorized = true;
  EXPECT_EQ(c.request(r, err, msg), PowerOutcome::ACCEPTED);
  EXPECT_TRUE(called);
  EXPECT_EQ(msg, "Windows is entering sleep");
}

TEST(PowerPolicy, ExecutorFailureReported) {
  PowerController c(PowerExecutor{[](PowerAction, std::string& e) {
    e = "privilege_denied";
    return false;
  }});
  std::string err, msg;
  PowerRequest r;
  r.action = PowerAction::SHUTDOWN;
  r.paired = true;
  r.authorized = true;
  EXPECT_EQ(c.request(r, err, msg), PowerOutcome::FAILED);
  EXPECT_EQ(err, "privilege_denied");
  EXPECT_NE(msg.find("privilege_denied"), std::string::npos);
}

TEST(PowerPolicy, NoRetryContract) {
  int calls = 0;
  PowerController c(PowerExecutor{[&](PowerAction, std::string&) {
    ++calls;
    return true;
  }});
  std::string err, msg;
  PowerRequest r;
  r.action = PowerAction::SLEEP;
  r.paired = true;
  r.authorized = true;
  c.request(r, err, msg);
  c.request(r, err, msg);
  // Each request = one executor call; retries are the CLIENT's policy
  // decision and FR-11 forbids them for power actions.
  EXPECT_EQ(calls, 2);
  EXPECT_EQ(c.requestCount(), 2u);
}
