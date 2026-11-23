
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

#include "core/protocol_json.hpp"
#include "core/protocol_validation.hpp"

using namespace remotewindows::protocol;

namespace {

std::string readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

// Round-trip one envelope through encode -> decode and check equality.
void expectRoundTrip(Envelope e, const std::string& label) {
  std::string json;
  ASSERT_TRUE(encodeEnvelopeSafe(e, json)) << label << ": encode failed";
  Envelope back;
  std::string code;
  ASSERT_TRUE(decodeEnvelope(json, back, code))
      << label << ": decode failed (" << code << ") " << json;
  EXPECT_EQ(e.kind, back.kind) << label;
  EXPECT_EQ(e.schema_major, back.schema_major) << label;
  EXPECT_EQ(e.schema_minor, back.schema_minor) << label;
  EXPECT_EQ(e.request_id, back.request_id) << label;
  EXPECT_EQ(e.sequence, back.sequence) << label;
  auto v1 = std::visit([](const auto& m) { return typeid(m).name(); }, e.payload);
  auto v2 = std::visit([](const auto& m) { return typeid(m).name(); }, back.payload);
  EXPECT_STREQ(v1, v2) << label << ": payload variant changed";
  // Validate: any well-formed envelope we built should pass semantic rules.
  auto vr = validate(e);
  if (!vr.ok)
    FAIL() << label << ": our own envelope failed validation: " << vr.error_code;
}

std::string makeUuid() {
  return "11111111-2222-4333-8444-555555555555";
}

}  // namespace

TEST(ProtocolJson, HelloRoundTrip) {
  Envelope e;
  e.kind = "hello";
  Hello h;
  h.product_name = "windows-host";
  h.app_version = "0.1.0";
  h.capabilities = {Capability::APP_CATALOG, Capability::POWER};
  e.payload = h;
  expectRoundTrip(std::move(e), "hello");
}

TEST(ProtocolJson, WindowAnnounceRoundTrip) {
  Envelope e;
  e.kind = "window_announce";
  WindowAnnounce w;
  w.window_id = makeUuid();
  w.application_session_id = "sess-0001";
  w.parent_window_id = "3f2b8c1e-9a4d-4e6f-8b7c-1d2e3f4a5b6c";
  w.owner_window_id = "3f2b8c1e-9a4d-4e6f-8b7c-1d2e3f4a5b6c";
  w.role = WindowRole::FILE_PICKER;
  w.modal = true;
  w.visible = true;
  w.z_order = 7;
  w.title = "Open";
  w.x = 100;
  w.y = 200;
  w.width_px = 800;
  w.height_px = 600;
  w.dpi_scale_percent = 150;
  w.state = WindowState::NORMAL;
  e.payload = w;
  expectRoundTrip(std::move(e), "window_announce");
}

TEST(ProtocolJson, InputEventKeyRoundTrip) {
  Envelope e;
  e.kind = "input_event";
  e.sequence = 41;
  InputEvent in;
  in.window_id = makeUuid();
  KeyInput k;
  k.windows_vk = 67;
  k.modifier_bits = 0x2;
  k.key_down = true;
  k.is_repeat = false;
  in.input = k;
  e.payload = in;
  expectRoundTrip(std::move(e), "input_key");
}

TEST(ProtocolJson, InputEventMouseRoundTrip) {
  Envelope e;
  e.kind = "input_event";
  e.sequence = 42;
  InputEvent in;
  in.window_id = makeUuid();
  MouseInput m;
  m.kind = MouseKind::MOVE;
  m.x = 120;
  m.y = 340;
  in.input = m;
  e.payload = in;
  expectRoundTrip(std::move(e), "input_mouse");
}

TEST(ProtocolJson, SystemPowerRoundTrip) {
  Envelope e;
  e.kind = "system_power_request";
  e.request_id = 32;
  e.sequence = 46;
  SystemPowerRequest p;
  p.action = PowerAction::SHUTDOWN;
  e.payload = p;
  expectRoundTrip(std::move(e), "power");
}

TEST(ProtocolJson, AppCatalogRoundTrip) {
  Envelope e;
  e.kind = "app_catalog";
  e.request_id = 9;  // catalog is a response: must echo the request id
  AppCatalog c;
  AppEntry a;
  a.app_id = "app-abcdef1234567890";
  a.display_name = "File Explorer";
  a.type = AppType::WIN32_SHORTCUT;
  a.available = true;
  a.icon_ref = "icons/explorer.png";
  c.entries.push_back(a);
  c.catalog_revision = 3;
  e.payload = c;
  expectRoundTrip(std::move(e), "catalog");
}

TEST(ProtocolJson, ResizeRequestRoundTrip) {
  Envelope e;
  e.kind = "resize_request";
  e.request_id = 7;
  e.sequence = 44;  // state-changing command: sequence required
  ResizeRequest r;
  r.window_id = makeUuid();
  r.client_width_px = 1280;
  r.client_height_px = 720;
  e.payload = r;
  expectRoundTrip(std::move(e), "resize_request");
}

TEST(ProtocolJson, WindowUpdatePartialRoundTrip) {
  Envelope e;
  e.kind = "window_update";
  WindowUpdate u;
  u.window_id = makeUuid();
  u.title = "New Title";
  u.modal = true;
  u.x = -5;
  e.payload = u;
  expectRoundTrip(std::move(e), "window_update");
}

TEST(ProtocolJson, HeartbeatAndStatsRoundTrip) {
  Envelope e;
  e.kind = "heartbeat";
  e.payload = Heartbeat{1700000000000ULL};
  expectRoundTrip(std::move(e), "heartbeat");

  Envelope s;
  s.kind = "performance_stats";
  PerformanceStats ps;
  ps.window_id = makeUuid();
  ps.fps = 59;
  ps.bitrate_kbps = 4096;
  ps.dropped_frames = 2;
  s.payload = ps;
  expectRoundTrip(std::move(s), "performance_stats");
}

TEST(ProtocolJson, RebootPowerActionRejectedAtCodec) {
  // "reboot" is not a legal enum value anywhere.
  std::string json = R"({"schema_major":1,"schema_minor":0,"kind":"system_power_request","payload":{"action":"reboot"},"request_id":32,"sequence":46})";
  Envelope e;
  std::string code;
  EXPECT_FALSE(decodeEnvelope(json, e, code));
  EXPECT_EQ(code, "invalid_enum");
}

TEST(ProtocolJson, MissingKindRejected) {
  Envelope e;
  std::string code;
  EXPECT_FALSE(decodeEnvelope(R"({"schema_major":1,"schema_minor":0})", e, code));
  EXPECT_FALSE(code.empty());
}

TEST(ProtocolJson, GarbageInputRejectedNoCrash) {
  Envelope e;
  std::string code;
  EXPECT_FALSE(decodeEnvelope("not json at all", e, code));
  EXPECT_FALSE(decodeEnvelope("", e, code));
  EXPECT_FALSE(decodeEnvelope("[1,2,3]", e, code));
  EXPECT_FALSE(decodeEnvelope(R"({"schema_major":"x","schema_minor":0,"kind":"heartbeat","payload":{}})", e, code));
}

TEST(ProtocolJson, TestVectorsDirectory) {
  const std::string dir = RWH_TEST_VECTORS_DIR;
  std::vector<std::filesystem::path> files;
  for (auto& p : std::filesystem::directory_iterator(dir))
    if (p.path().extension() == ".json") files.push_back(p.path());
  ASSERT_GE(files.size(), 20u) << "expected the 22-vector bundle in " << dir;

  int valid = 0, invalid = 0;
  for (const auto& f : files) {
    auto text = readFile(f.string());
    nlohmann::json doc = nlohmann::json::parse(text);
    ASSERT_TRUE(doc.contains("expected")) << f.string();
    std::string expected = doc["expected"];
    auto envJson = doc["envelope"].dump();
    Envelope e;
    std::string code;
    bool decoded = decodeEnvelope(envJson, e, code);
    if (expected == "valid") {
      ++valid;
      ASSERT_TRUE(decoded) << f.string() << ": " << code << "\n" << envJson;
      auto vr = validate(e);
      ASSERT_TRUE(vr.ok) << f.string() << ": " << vr.error_code << " " << vr.detail;
    } else {
      ++invalid;
      if (!decoded) {
        // Codec-level rejection (structural / enum).
        if (doc.contains("violation"))
          EXPECT_EQ(code, doc["violation"].get<std::string>())
              << f.string();
      } else {
        // Valid wire frame rejected by semantic validation.
        auto vr = validate(e);
        ASSERT_FALSE(vr.ok) << f.string() << " expected invalid but validated";
        if (doc.contains("violation"))
          EXPECT_EQ(vr.error_code, doc["violation"].get<std::string>())
              << f.string() << " got: " << vr.detail;
      }
    }
  }
  EXPECT_EQ(valid, 14);
  EXPECT_EQ(invalid, 8);
}
