
// RW-H-004: session state machine + pairing handshake over the signaling
// channel (unit-level: no Win32, injectable clock; plus two loopback e2e
// tests over the real SignalingServer/Client).
//
// Covers the plan gating (FR-02/FR-05, PROTOCOL §6):
//   - HELLO -> HELLO + PAIRING_CHALLENGE, state unpaired -> pairing
//   - valid code + nonce echo -> paired, session key derived
//   - invalid code / nonce mismatch / expired -> unpaired, typed error
//   - 3 failures -> 60 s lockout (locked)
//   - every gated control kind rejected while unpaired (not_paired) or
//     dropped when schema v0.1.0 has no typed result kind for it
//   - heartbeat allowed in every state
//   - post-pairing: empty catalog reply, not_implemented elsewhere

#include <gtest/gtest.h>

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/SessionAuth.hpp"
#include "core/pairing.hpp"
#include "network/signaling_client.hpp"
#include "network/signaling_server.hpp"

using namespace remotewindows;
namespace proto = remotewindows::protocol;

namespace {

uint64_t kT0 = 1'000'000'000ull;  // ms; comfortably above any zero stamp

const char* kUuid = "11111111-2222-3333-4444-555555555555";

struct Harness {
  uint64_t now = kT0;
  std::string host_name = "DESKTOP-PC";
  std::shared_ptr<app::PairingHost> host;
  std::shared_ptr<app::SessionAuth> session;
  std::string code;

  Harness() : host(std::make_shared<app::PairingHost>()) {
    host->beginEpoch(now);
    code = host->currentCode();
    EXPECT_EQ(code.size(), 6u);
    session = std::make_shared<app::SessionAuth>(
        host.get(), host_name, [this] { return now; });
  }

  proto::Envelope hello() const {
    proto::Envelope e;
    e.kind = "hello";
    proto::Hello h;
    h.product_name = "macos-client";
    h.app_version = "0.1.0";
    e.payload = h;
    return e;
  }

  std::string challengeNonce(const std::vector<proto::Envelope>& out) const {
    for (const auto& e : out) {
      if (e.kind != "pairing_challenge") continue;
      if (const auto* pc = std::get_if<proto::PairingChallenge>(&e.payload))
        if (pc->challenge_nonce.has_value()) return *pc->challenge_nonce;
    }
    return {};
  }

  proto::Envelope pairingAttempt(const std::string& presented,
                                 const std::string& nonce) const {
    proto::Envelope e;
    e.kind = "pairing_challenge";
    proto::PairingChallenge pc;
    pc.one_time_code = presented;
    pc.challenge_nonce = nonce;
    pc.host_name = host_name;
    e.payload = pc;
    return e;
  }

  std::string wrongCode() const {
    std::string c = code;
    c.back() = (c.back() == '9') ? '0' : '9';
    return c;
  }

  bool pairUp() {
    auto o1 = session->handle(hello());
    std::string n = challengeNonce(o1);
    auto o2 = session->handle(pairingAttempt(code, n));
    if (o2.empty() || o2[0].kind != "pairing_result") return false;
    const auto* pr = std::get_if<proto::PairingResult>(&o2[0].payload);
    return pr && pr->accepted;
  }

  // A schema-valid envelope for each gated control kind (the server-side
  // validator would accept these; built the way a real client sends them).
  proto::Envelope gated(const std::string& kind) const {
    proto::Envelope e;
    e.kind = kind;
    if (kind == "app_catalog_request") {
      e.request_id = 1;
      e.payload = proto::AppCatalogRequest{};
    } else if (kind == "launch_app") {
      e.request_id = 2;
      proto::LaunchApp la;
      la.app_id = "a000000000000001";
      e.payload = la;
    } else if (kind == "input_event") {
      e.sequence = 1;
      proto::InputEvent iv;
      iv.window_id = kUuid;
      proto::MouseInput m;
      m.kind = proto::MouseKind::MOVE;
      m.x = 1;
      m.y = 2;
      iv.input = m;
      e.payload = iv;
    } else if (kind == "resize_request") {
      e.request_id = 3;
      e.sequence = 2;
      proto::ResizeRequest rr;
      rr.window_id = kUuid;
      rr.client_width_px = 800;
      rr.client_height_px = 600;
      e.payload = rr;
    } else if (kind == "window_action") {
      e.request_id = 4;
      e.sequence = 3;
      proto::WindowAction wa;
      wa.window_id = kUuid;
      wa.kind = proto::WindowActionKind::ACTIVATE;
      e.payload = wa;
    } else if (kind == "frame_rate_request") {
      proto::FrameRateRequest fr;
      fr.window_id = kUuid;
      fr.target_fps = 30;
      fr.reason = proto::FrameRateReason::FOCUS_CHANGED;
      e.payload = fr;
    } else if (kind == "system_power_request") {
      e.request_id = 5;
      e.sequence = 4;
      proto::SystemPowerRequest sp;
      sp.action = proto::PowerAction::SLEEP;
      e.payload = sp;
    }
    return e;
  }

  // Extracts the error_code from a result envelope (all five
  // error-bearing result kinds).
  static std::string errorCodeOf(const proto::Envelope& e) {
    if (e.kind == "launch_result") {
      const auto* p = std::get_if<proto::LaunchResult>(&e.payload);
      return p ? p->error_code : std::string("<none>");
    }
    if (e.kind == "resize_ack") {
      const auto* p = std::get_if<proto::ResizeAck>(&e.payload);
      return p ? p->error_code : std::string("<none>");
    }
    if (e.kind == "window_action_result") {
      const auto* p = std::get_if<proto::WindowActionResult>(&e.payload);
      return p ? p->error_code : std::string("<none>");
    }
    if (e.kind == "stream_status") {
      const auto* p = std::get_if<proto::StreamStatus>(&e.payload);
      return p ? p->error_code : std::string("<none>");
    }
    if (e.kind == "system_power_result") {
      const auto* p = std::get_if<proto::SystemPowerResult>(&e.payload);
      return p ? p->error_code : std::string("<none>");
    }
    return "<n/a>";
  }
};

}  // namespace

// ------------------------------------------------------------------ happy path

TEST(SessionAuth, HelloIssuesChallengeAndMovesToPairing) {
  Harness h;
  auto out = h.session->handle(h.hello());
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].kind, "hello");
  const auto* hh = std::get_if<proto::Hello>(&out[0].payload);
  ASSERT_NE(hh, nullptr);
  EXPECT_EQ(hh->product_name, "windows-host");
  EXPECT_EQ(out[1].kind, "pairing_challenge");
  const auto* pc = std::get_if<proto::PairingChallenge>(&out[1].payload);
  ASSERT_NE(pc, nullptr);
  EXPECT_EQ(pc->host_name.value_or(""), h.host_name);
  ASSERT_TRUE(pc->challenge_nonce.has_value());
  EXPECT_EQ(pc->challenge_nonce->size(), 32u);
  for (char c : *pc->challenge_nonce)
    EXPECT_TRUE(std::isxdigit((unsigned char)c)) << c;
  // The challenge must NOT carry the code (canonical vector shape).
  EXPECT_FALSE(pc->one_time_code.has_value());
  EXPECT_EQ(h.session->state(), app::SessionState::Pairing);
}

TEST(SessionAuth, ValidCodePairsAndDerivesSessionKey) {
  Harness h;
  auto o1 = h.session->handle(h.hello());
  std::string n = h.challengeNonce(o1);
  ASSERT_FALSE(n.empty());

  auto o2 = h.session->handle(h.pairingAttempt(h.code, n));
  ASSERT_EQ(o2.size(), 1u);
  EXPECT_EQ(o2[0].kind, "pairing_result");
  const auto* pr = std::get_if<proto::PairingResult>(&o2[0].payload);
  ASSERT_NE(pr, nullptr);
  EXPECT_TRUE(pr->accepted);
  EXPECT_TRUE(pr->error_code.empty());

  EXPECT_EQ(h.session->state(), app::SessionState::Paired);
  EXPECT_EQ(h.session->sessionKey(),
            core::PairingManager::deriveSessionKey(h.code, h.host_name));
  EXPECT_EQ(h.session->sessionKey().size(), 64u);
  // The one-time code is consumed after success.
  EXPECT_FALSE(h.host->hasCode());
}

// ---------------------------------------------------------- negative pairing

TEST(SessionAuth, InvalidCodeStaysUnpairedWithTypedError) {
  Harness h;
  auto o1 = h.session->handle(h.hello());
  std::string n = h.challengeNonce(o1);
  auto o2 = h.session->handle(h.pairingAttempt(h.wrongCode(), n));
  ASSERT_EQ(o2.size(), 1u);
  const auto* pr = std::get_if<proto::PairingResult>(&o2[0].payload);
  ASSERT_NE(pr, nullptr);
  EXPECT_FALSE(pr->accepted);
  EXPECT_EQ(pr->error_code, "invalid_code");
  EXPECT_NE(h.session->state(), app::SessionState::Paired);

  // Control is still rejected while unpaired.
  auto o3 = h.session->handle(h.gated("launch_app"));
  ASSERT_EQ(o3.size(), 1u);
  EXPECT_EQ(Harness::errorCodeOf(o3[0]), "not_paired");
}

TEST(SessionAuth, NonceMismatchRejected) {
  Harness h;
  auto o1 = h.session->handle(h.hello());
  std::string n = h.challengeNonce(o1);

  // Wrong nonce, even with the right code: no pairing.
  auto o2 = h.session->handle(h.pairingAttempt(h.code, n + "0"));
  const auto* pr2 = std::get_if<proto::PairingResult>(&o2[0].payload);
  ASSERT_NE(pr2, nullptr);
  EXPECT_FALSE(pr2->accepted);
  EXPECT_EQ(pr2->error_code, "invalid_code");

  // Missing nonce: no pairing.
  auto o3 = h.session->handle(h.pairingAttempt(h.code, ""));
  const auto* pr3 = std::get_if<proto::PairingResult>(&o3[0].payload);
  ASSERT_NE(pr3, nullptr);
  EXPECT_FALSE(pr3->accepted);
  EXPECT_NE(h.session->state(), app::SessionState::Paired);
}

TEST(SessionAuth, PairingAttemptBeforeHelloIsInvalidState) {
  Harness h;
  auto out = h.session->handle(
      h.pairingAttempt(h.code, std::string(32, '0')));
  ASSERT_EQ(out.size(), 1u);
  const auto* pr = std::get_if<proto::PairingResult>(&out[0].payload);
  ASSERT_NE(pr, nullptr);
  EXPECT_FALSE(pr->accepted);
  EXPECT_EQ(pr->error_code, "invalid_state");
  EXPECT_EQ(h.session->state(), app::SessionState::Unpaired);
}

TEST(SessionAuth, ClientSentPairingResultIsTypedError) {
  Harness h;
  h.session->handle(h.hello());
  proto::Envelope e;
  e.kind = "pairing_result";
  proto::PairingResult pr;
  pr.accepted = false;
  e.payload = pr;
  auto out = h.session->handle(e);
  ASSERT_EQ(out.size(), 1u);
  const auto* back = std::get_if<proto::PairingResult>(&out[0].payload);
  ASSERT_NE(back, nullptr);
  EXPECT_FALSE(back->accepted);
  EXPECT_EQ(back->error_code, "invalid_state");
}

TEST(SessionAuth, LockoutAfterMaxAttempts) {
  Harness h;
  auto o1 = h.session->handle(h.hello());
  std::string n = h.challengeNonce(o1);

  auto r1 = h.session->handle(h.pairingAttempt(h.wrongCode(), n));
  auto r2 = h.session->handle(h.pairingAttempt(h.wrongCode(), n));
  auto r3 = h.session->handle(h.pairingAttempt(h.wrongCode(), n));
  const auto* p1 = std::get_if<proto::PairingResult>(&r1[0].payload);
  const auto* p3 = std::get_if<proto::PairingResult>(&r3[0].payload);
  ASSERT_NE(p1, nullptr);
  ASSERT_NE(p3, nullptr);
  EXPECT_EQ(p1->error_code, "invalid_code");  // 1st and 2nd: plain failure
  EXPECT_EQ(p3->error_code, "locked");        // 3rd: lockout engaged
  EXPECT_EQ(h.session->state(), app::SessionState::Locked);

  // During lockout even the right code is rejected.
  h.now += 5000;
  auto r4 = h.session->handle(h.pairingAttempt(h.code, n));
  const auto* p4 = std::get_if<proto::PairingResult>(&r4[0].payload);
  ASSERT_NE(p4, nullptr);
  EXPECT_FALSE(p4->accepted);
  EXPECT_EQ(p4->error_code, "locked");
  EXPECT_NE(h.session->state(), app::SessionState::Paired);

  // After the 60 s lockout expires the code works again.
  h.now += 61'000;
  auto r5 = h.session->handle(h.pairingAttempt(h.code, n));
  const auto* p5 = std::get_if<proto::PairingResult>(&r5[0].payload);
  ASSERT_NE(p5, nullptr);
  EXPECT_TRUE(p5->accepted);
  EXPECT_EQ(h.session->state(), app::SessionState::Paired);
}

TEST(SessionAuth, ExpiredCodeRejected) {
  Harness h;
  auto o1 = h.session->handle(h.hello());
  std::string n = h.challengeNonce(o1);
  h.now += 301'000;  // beyond the 300 s code TTL
  auto out = h.session->handle(h.pairingAttempt(h.code, n));
  const auto* pr = std::get_if<proto::PairingResult>(&out[0].payload);
  ASSERT_NE(pr, nullptr);
  EXPECT_FALSE(pr->accepted);
  EXPECT_EQ(pr->error_code, "expired");
  EXPECT_NE(h.session->state(), app::SessionState::Paired);
}

TEST(SessionAuth, CodeIsSingleUseAcrossSessions) {
  Harness h;
  ASSERT_TRUE(h.pairUp());
  // A second connection must NOT be able to pair with the consumed code.
  app::SessionAuth second(h.host.get(), h.host_name,
                          [] { return kT0; });
  auto o1 = second.handle(h.hello());
  std::string n = h.challengeNonce(o1);
  auto o2 = second.handle(h.pairingAttempt(h.code, n));
  const auto* pr = std::get_if<proto::PairingResult>(&o2[0].payload);
  ASSERT_NE(pr, nullptr);
  EXPECT_FALSE(pr->accepted);
  EXPECT_EQ(pr->error_code, "invalid_code");
  EXPECT_NE(second.state(), app::SessionState::Paired);
}

// --------------------------------------------------------------- gating rules

namespace {
struct GatedExpectation {
  const char* kind;
  bool has_reply;  // false: v0.1.0 has no typed result kind for it
  const char* reply_kind;
  const char* error_code;
};
const std::vector<GatedExpectation>& gatedCases() {
  static const std::vector<GatedExpectation> v = {
      {"launch_app", true, "launch_result", "not_paired"},
      {"input_event", false, "", ""},
      {"resize_request", true, "resize_ack", "not_paired"},
      {"window_action", true, "window_action_result", "not_paired"},
      {"frame_rate_request", true, "stream_status", "not_paired"},
      {"system_power_request", true, "system_power_result", "not_paired"},
      {"app_catalog_request", false, "", ""},
  };
  return v;
}
}  // namespace

// Pre-pairing control rejection for EVERY gated kind (requirement: no
// unpaired control message can be processed). Kinds without a typed
// result envelope in schema v0.1.0 (input_event, app_catalog_request)
// are dropped — see CONTRACT change request for the missing result kinds.
TEST(SessionAuth, UnpairedGatedKindsAllRejectedOrDropped) {
  for (const auto& c : gatedCases()) {
    Harness h;
    auto out = h.session->handle(h.gated(c.kind));
    SCOPED_TRACE(c.kind);
    if (c.has_reply) {
      ASSERT_EQ(out.size(), 1u) << c.kind;
      EXPECT_EQ(out[0].kind, c.reply_kind) << c.kind;
      EXPECT_EQ(Harness::errorCodeOf(out[0]), c.error_code) << c.kind;
    } else {
      EXPECT_TRUE(out.empty()) << c.kind;
    }
    EXPECT_EQ(h.session->state(), app::SessionState::Unpaired) << c.kind;
  }
}

TEST(SessionAuth, GatedKindsRejectedWhilePairingToo) {
  Harness h;
  h.session->handle(h.hello());  // state: pairing (challenge issued)
  auto out = h.session->handle(h.gated("system_power_request"));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].kind, "system_power_result");
  EXPECT_EQ(Harness::errorCodeOf(out[0]), "not_paired");
}

TEST(SessionAuth, LockedSessionStillRejectsControl) {
  Harness h;
  auto o1 = h.session->handle(h.hello());
  std::string n = h.challengeNonce(o1);
  for (int i = 0; i < 3; ++i)
    h.session->handle(h.pairingAttempt(h.wrongCode(), n));
  EXPECT_EQ(h.session->state(), app::SessionState::Locked);
  auto out = h.session->handle(h.gated("launch_app"));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(Harness::errorCodeOf(out[0]), "not_paired");
}

TEST(SessionAuth, HeartbeatAllowedInEveryState) {
  Harness h;
  auto check = [&](const char* label) {
    proto::Envelope e;
    e.kind = "heartbeat";
    proto::Heartbeat hb;
    hb.sender_time_ms = (int64_t)h.now;
    e.payload = hb;
    auto out = h.session->handle(e);
    EXPECT_EQ(out.size(), 1u) << label;
    EXPECT_EQ(out[0].kind, "heartbeat") << label;
    const auto* rh = std::get_if<proto::Heartbeat>(&out[0].payload);
    ASSERT_NE(rh, nullptr) << label;
    EXPECT_EQ(rh->sender_time_ms, (int64_t)h.now) << label;
  };
  check("unpaired");
  h.session->handle(h.hello());
  check("pairing");
  auto o1 = h.session->handle(h.hello());
  h.session->handle(h.pairingAttempt(h.code, h.challengeNonce(o1)));
  EXPECT_EQ(h.session->state(), app::SessionState::Paired);
  check("paired");
}

// -------------------------------------------------------- post-pairing slice

TEST(SessionAuth, PostPairingCatalogReplyEmpty) {
  Harness h;
  ASSERT_TRUE(h.pairUp());
  auto out = h.session->handle(h.gated("app_catalog_request"));
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].kind, "app_catalog");
  EXPECT_EQ(out[0].request_id.value_or(0), 1u);
  const auto* cat = std::get_if<proto::AppCatalog>(&out[0].payload);
  ASSERT_NE(cat, nullptr);
  EXPECT_EQ(cat->catalog_revision, 1u);
  EXPECT_TRUE(cat->entries.empty());  // honest placeholder, no invented apps
}

TEST(SessionAuth, PostPairingOtherControlNotImplemented) {
  Harness h;
  ASSERT_TRUE(h.pairUp());
  struct {
    const char* kind;
    const char* reply;
  } cases[] = {
      {"launch_app", "launch_result"},
      {"resize_request", "resize_ack"},
      {"window_action", "window_action_result"},
      {"frame_rate_request", "stream_status"},
      {"system_power_request", "system_power_result"},
  };
  for (const auto& c : cases) {
    SCOPED_TRACE(c.kind);
    auto out = h.session->handle(h.gated(c.kind));
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].kind, c.reply);
    EXPECT_EQ(Harness::errorCodeOf(out[0]), "not_implemented");
  }
  // input_event still has no result kind in v0.1.0: dropped even when
  // paired (real input handling is the H-INPUT task).
  EXPECT_TRUE(h.session->handle(h.gated("input_event")).empty());
  EXPECT_EQ(h.session->state(), app::SessionState::Paired);
}

TEST(SessionAuth, HelloWhilePairedDoesNotRechallenge) {
  Harness h;
  ASSERT_TRUE(h.pairUp());
  auto out = h.session->handle(h.hello());
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].kind, "hello");
  EXPECT_EQ(h.session->state(), app::SessionState::Paired);
}

// ------------------------------------------------------------ loopback e2e

namespace {
proto::Envelope clientHello() {
  proto::Envelope e;
  e.kind = "hello";
  proto::Hello h;
  h.product_name = "macos-client";
  h.app_version = "0.1.0";
  e.payload = h;
  return e;
}
}  // namespace

TEST(SessionAuthE2E, FullHandshakeOverRealChannel) {
  app::SignalingSessionHandler handler("DESKTOP-PC");  // logger: nullptr
  net::SignalingServer server(&handler);
  std::string err;
  ASSERT_TRUE(server.start("127.0.0.1", 0, err)) << err;
  auto port = server.port(err);

  net::SignalingClient client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;
  ASSERT_TRUE(client.sendEnvelope(clientHello(), err)) << err;

  auto read = [&client, &err]() {
    std::string code;
    return client.readEnvelope(5000, err, code);
  };

  auto r1 = read();
  ASSERT_TRUE(r1.has_value()) << err;
  EXPECT_EQ(r1->kind, "hello");

  auto r2 = read();
  ASSERT_TRUE(r2.has_value()) << err;
  EXPECT_EQ(r2->kind, "pairing_challenge");
  const auto* pc = std::get_if<proto::PairingChallenge>(&r2->payload);
  ASSERT_NE(pc, nullptr);
  ASSERT_TRUE(pc->challenge_nonce.has_value());

  // Pair with the in-process code (same pattern as the selftest).
  proto::Envelope attempt;
  attempt.kind = "pairing_challenge";
  proto::PairingChallenge ap;
  ap.one_time_code = handler.currentPairingCode();
  ap.challenge_nonce = *pc->challenge_nonce;
  attempt.payload = ap;
  ASSERT_TRUE(client.sendEnvelope(attempt, err)) << err;

  auto r3 = read();
  ASSERT_TRUE(r3.has_value()) << err;
  EXPECT_EQ(r3->kind, "pairing_result");
  const auto* pr = std::get_if<proto::PairingResult>(&r3->payload);
  ASSERT_NE(pr, nullptr);
  EXPECT_TRUE(pr->accepted);

  // Post-pairing control exchange.
  proto::Envelope req;
  req.kind = "app_catalog_request";
  req.request_id = 9;
  req.payload = proto::AppCatalogRequest{};
  ASSERT_TRUE(client.sendEnvelope(req, err)) << err;
  auto r4 = read();
  ASSERT_TRUE(r4.has_value()) << err;
  EXPECT_EQ(r4->kind, "app_catalog");
  EXPECT_EQ(r4->request_id.value_or(0), 9u);
  const auto* cat = std::get_if<proto::AppCatalog>(&r4->payload);
  ASSERT_NE(cat, nullptr);
  EXPECT_TRUE(cat->entries.empty());

  // Heartbeat still works after pairing.
  proto::Envelope hb;
  hb.kind = "heartbeat";
  proto::Heartbeat hbp;
  hbp.sender_time_ms = 42;
  hb.payload = hbp;
  ASSERT_TRUE(client.sendEnvelope(hb, err)) << err;
  auto r5 = read();
  ASSERT_TRUE(r5.has_value()) << err;
  EXPECT_EQ(r5->kind, "heartbeat");

  client.close();
  server.stop();
}

TEST(SessionAuthE2E, UnpairedClientControlRejectedOverChannel) {
  app::SignalingSessionHandler handler("DESKTOP-PC");  // logger: nullptr
  net::SignalingServer server(&handler);
  std::string err;
  ASSERT_TRUE(server.start("127.0.0.1", 0, err)) << err;
  auto port = server.port(err);

  net::SignalingClient client;
  ASSERT_TRUE(client.connectTo("127.0.0.1", port, err)) << err;
  ASSERT_TRUE(client.sendEnvelope(clientHello(), err)) << err;
  // Consume the host hello + challenge WITHOUT pairing.
  std::string code;
  auto r1 = client.readEnvelope(5000, err, code);
  ASSERT_TRUE(r1.has_value()) << err;
  EXPECT_EQ(r1->kind, "hello");
  auto r2 = client.readEnvelope(5000, err, code);
  ASSERT_TRUE(r2.has_value()) << err;
  EXPECT_EQ(r2->kind, "pairing_challenge");

  // Unpaired launch request -> typed not_paired rejection.
  proto::Envelope req;
  req.kind = "launch_app";
  req.request_id = 7;
  proto::LaunchApp la;
  la.app_id = "a000000000000001";
  req.payload = la;
  ASSERT_TRUE(client.sendEnvelope(req, err)) << err;
  auto r3 = client.readEnvelope(5000, err, code);
  ASSERT_TRUE(r3.has_value()) << err;
  EXPECT_EQ(r3->kind, "launch_result");
  EXPECT_EQ(r3->request_id.value_or(0), 7u);
  const auto* lr = std::get_if<proto::LaunchResult>(&r3->payload);
  ASSERT_NE(lr, nullptr);
  EXPECT_EQ(lr->state, proto::LaunchState::FAILED);
  EXPECT_EQ(lr->error_code, "not_paired");

  // Heartbeat keeps working while unpaired.
  proto::Envelope hb;
  hb.kind = "heartbeat";
  proto::Heartbeat hbp;
  hbp.sender_time_ms = 7;
  hb.payload = hbp;
  ASSERT_TRUE(client.sendEnvelope(hb, err)) << err;
  auto r4 = client.readEnvelope(5000, err, code);
  ASSERT_TRUE(r4.has_value()) << err;
  EXPECT_EQ(r4->kind, "heartbeat");

  client.close();
  server.stop();
}
