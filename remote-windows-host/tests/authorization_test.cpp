
#include <gtest/gtest.h>
#include "core/authorization.hpp"

using namespace remotewindows;

namespace {
protocol::Envelope mk(const std::string& kind) {
  protocol::Envelope e;
  e.kind = kind;
  if (kind == "heartbeat")
    e.payload = protocol::Heartbeat{0};
  else if (kind == "hello") {
    protocol::Hello h;
    h.product_name = "macos-client";
    h.app_version = "0.1.0";
    e.payload = h;
  } else if (kind == "pairing_challenge")
    e.payload = protocol::PairingChallenge{};
  else if (kind == "system_power_request") {
    protocol::SystemPowerRequest p;
    p.action = protocol::PowerAction::SLEEP;
    e.payload = p;
  } else if (kind == "launch_app") {
    protocol::LaunchApp l;
    l.app_id = "app-x";
    e.payload = l;
  } else {
    protocol::Heartbeat hb{0};
    e.payload = hb;
  }
  return e;
}
}  // namespace

TEST(Authorization, UnpairedOnlyPairingKinds) {
  core::AuthorizationPolicy pol(false, false);
  EXPECT_EQ(pol.check(mk("hello")).result, core::GateResult::ALLOWED);
  EXPECT_EQ(pol.check(mk("pairing_challenge")).result,
            core::GateResult::ALLOWED);
  EXPECT_EQ(pol.check(mk("heartbeat")).result, core::GateResult::ALLOWED);
  auto launch = pol.check(mk("launch_app"));
  EXPECT_EQ(launch.result, core::GateResult::DENIED_NOT_PAIRED);
  EXPECT_EQ(launch.error_code, "not_paired");
  auto power = pol.check(mk("system_power_request"));
  EXPECT_EQ(power.result, core::GateResult::DENIED_NOT_PAIRED);
}

TEST(Authorization, PairedButPowerGated) {
  core::AuthorizationPolicy pol(true, false);
  EXPECT_EQ(pol.check(mk("launch_app")).result, core::GateResult::ALLOWED);
  auto power = pol.check(mk("system_power_request"));
  EXPECT_EQ(power.result, core::GateResult::DENIED_NOT_AUTHORIZED);
  EXPECT_EQ(power.error_code, "not_authorized");
}

TEST(Authorization, PairedAndAuthorizedAllowsPower) {
  core::AuthorizationPolicy pol(true, true);
  EXPECT_EQ(pol.check(mk("system_power_request")).result,
            core::GateResult::ALLOWED);
}
