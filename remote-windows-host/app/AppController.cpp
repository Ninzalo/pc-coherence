
#include "app/AppController.hpp"

#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "app/Logger.hpp"
#include "app/SessionAuth.hpp"
#include "app/TrayController.hpp"
#include "core/pairing.hpp"
#include "core/protocol_types.hpp"
#include "network/signaling_client.hpp"
#include "network/signaling_server.hpp"

#ifdef _WIN32
#include "app/Win32Env.hpp"
#endif

using namespace remotewindows;

namespace {

std::string hostName() {
#ifdef _WIN32
  wchar_t buf[64] = {0};
  DWORD n = 64;
  if (GetComputerNameW(buf, &n)) {
    std::string s;
    for (DWORD i = 0; i < n; ++i) s += (char)buf[i];
    return s;
  }
#endif
  return "windows-host";
}

// Machine-readable selftest report (consumed by the build/test runner).
// Keys are stable machine-readable English; values never contain secrets.
void printSelftest(const remotewindows::app::SelftestResult& res) {
  std::printf("{\"selftest\":true,\"passed\":%s,\"stage\":\"%s\","
              "\"port\":%u,\"reply_kind\":\"%s\",\"envelopes\":%zu,"
              "\"pairing\":\"%s\",\"reply_kinds\":\"%s\","
              "\"error\":\"%s\"}\n",
              res.passed ? "true" : "false", res.stage.c_str(),
              (unsigned)res.port, res.reply_kind.c_str(), res.envelopes,
              res.pairing.c_str(), res.reply_kinds.c_str(),
              res.error.c_str());
}

// SignalingServer::Handler is stateless with respect to connections
// (per-connection worker threads); the detached workers may briefly run
// onDisconnection after stop(). Wait for them to drain before the
// handler (and its session map) is destroyed.
void drainConnectionWorkers() {
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

}  // namespace

namespace remotewindows::app {

int AppController::runGui(const Options& opts) {
  Logger& log = Logger::instance();
  std::string err;
  if (!log.init(Logger::defaultPath(err))) {
    std::fprintf(stderr, "warning: log file not opened: %s\n",
                 err.c_str());
  }

  SignalingSessionHandler handler(hostName(), &log);
  net::SignalingServer server(&handler);
  const std::string addr = opts.addr.empty() ? "0.0.0.0" : opts.addr;
  if (!server.start(addr, opts.port, err)) {
    log.write(LogLevel::Error, "host.start_failed",
              {{"addr", addr}, {"port", std::to_string(opts.port)},
               {"error", err}});
    return 2;
  }
  uint16_t actual = server.port(err);
  log.write(LogLevel::Info, "host.start",
            {{"addr", addr}, {"port", std::to_string(actual)}});

  TrayController tray(
      [&server, &handler]() -> TrayStatus {
        std::string e2;
        return TrayStatus{server.port(e2), server.connectionCount(),
                          handler.sessionCount()};
      },
      [&handler]() -> std::string {
        // FR-13 UI channel: shown via MessageBox by the tray; the code
        // is never logged or persisted (AGENTS rule 9).
        return handler.rotateOrCurrentCode();
      });
  int rc = tray.run();
  if (rc != 0) {
    log.write(LogLevel::Error, "tray.failed",
              {{"code", std::to_string(rc)},
               {"detail", tray.lastError().empty() ? "n/a" : tray.lastError()}});
  } else {
    log.write(LogLevel::Info, "tray.exit", {});
  }

  server.stop();
  drainConnectionWorkers();
  log.write(LogLevel::Info, "host.stop",
            {{"envelopes", std::to_string(server.envelopesReceived())}});
  return rc;
}

int AppController::runSelftest(const Options& opts) {
  Logger& log = Logger::instance();
  std::string err;
  if (!log.init(Logger::defaultPath(err))) {
    std::fprintf(stderr, "warning: log file not opened: %s\n",
                 err.c_str());
  }

  SelftestResult res;
  res.pairing = "none";

  SignalingSessionHandler handler(hostName(), &log);
  net::SignalingServer server(&handler);
  if (!server.start("127.0.0.1", opts.port, err)) {
    res.stage = "start";
    res.error = err;
    log.write(LogLevel::Error, "selftest.fail",
              {{"stage", "start"}, {"error", err}});
    printSelftest(res);
    return 1;
  }
  res.port = server.port(err);
  log.write(LogLevel::Info, "selftest.server_up",
            {{"port", std::to_string(res.port)}});

  // In-process client performing the full handshake (the server runs its
  // accept/worker threads itself):
  //   HELLO -> HELLO -> PAIRING_CHALLENGE -> PAIRING_RESULT(accepted)
  //          -> APP_CATALOG_REQUEST -> APP_CATALOG(empty)
  {
    net::SignalingClient client;
    if (client.connectTo("127.0.0.1", res.port, err)) {
      auto expect = [&client, &res, &err](
                        const char* want_kind,
                        const char* fail_stage = "exchange")
          -> std::optional<protocol::Envelope> {
        std::string err_code;
        auto env = client.readEnvelope(5000, err, err_code);
        if (!env) {
          res.stage = fail_stage;
          res.error = err.empty() ? err_code : err;
          return std::nullopt;
        }
        if (env->kind != want_kind) {
          res.stage = fail_stage;
          res.error = std::string("expected kind ") + want_kind + " got " +
                      env->kind;
          return std::nullopt;
        }
        res.reply_kind = env->kind;
        res.reply_kinds += (res.reply_kinds.empty() ? "" : ";") +
                           env->kind;
        return env;
      };

      // 1. Client HELLO.
      protocol::Envelope hello;
      hello.kind = "hello";
      protocol::Hello h;
      h.product_name = "macos-client";
      h.app_version = "0.1.0";
      h.capabilities = {protocol::Capability::APP_CATALOG,
                        protocol::Capability::INPUT};
      hello.payload = h;
      if (client.sendEnvelope(hello, err)) {
        // 2. Host HELLO.
        auto r1 = expect("hello");
        if (r1 && r1->schema_major != protocol::kProtocolMajor) {
          res.stage = "exchange";
          res.error = "schema_major mismatch";
          r1.reset();
        }

        // 3. Host PAIRING_CHALLENGE (nonce + host name, no code).
        auto r2 = r1 ? expect("pairing_challenge") : std::nullopt;
        std::string nonce, echoed_host;
        if (r2) {
          if (const auto* pc =
                  std::get_if<protocol::PairingChallenge>(&r2->payload)) {
            nonce = pc->challenge_nonce.value_or("");
            echoed_host = pc->host_name.value_or("");
          }
          if (nonce.empty()) {
            res.stage = "exchange";
            res.error = "challenge without nonce";
            r2.reset();
          }
        }

        // 4. Client pairing attempt: code (in-process, simulating the
        //    user typing the Host-displayed code) + echoed nonce.
        if (r2) {
          protocol::Envelope attempt;
          attempt.kind = "pairing_challenge";
          protocol::PairingChallenge pc;
          pc.one_time_code = handler.currentPairingCode();
          pc.challenge_nonce = nonce;
          pc.host_name = echoed_host;
          attempt.payload = pc;
          if (!client.sendEnvelope(attempt, err)) {
            res.stage = "exchange";
            res.error = err;
          }
        }

        // 5. Host PAIRING_RESULT (accepted per the canonical vector).
        auto r3 = r2 ? expect("pairing_result") : std::nullopt;
        if (r3) {
          if (const auto* pr =
                  std::get_if<protocol::PairingResult>(&r3->payload)) {
            res.pairing = pr->accepted ? "accepted" : "rejected";
            if (!pr->accepted) {
              res.stage = "exchange";
              res.error = "pairing rejected: " + pr->error_code;
            }
          }
        }

        // 6. Post-pairing control exchange: APP_CATALOG_REQUEST.
        std::optional<protocol::Envelope> r4;
        if (r3 && res.pairing == "accepted") {
          protocol::Envelope req;
          req.kind = "app_catalog_request";
          req.request_id = 1;
          protocol::AppCatalogRequest acr;
          acr.force_rescan = false;
          req.payload = acr;
          if (client.sendEnvelope(req, err))
            r4 = expect("app_catalog");
          else {
            res.stage = "exchange";
            res.error = err;
            r4.reset();
          }
        }

        // 7. APP_CATALOG: empty entries (honest placeholder; the catalog
        //    scanner is a later task).
        if (r4) {
          if (const auto* cat =
                  std::get_if<protocol::AppCatalog>(&r4->payload)) {
            if (cat->entries.empty() &&
                r4->request_id.value_or(0) == 1u) {
              res.passed = true;
            } else {
              res.stage = "exchange";
              res.error = "app_catalog unexpected content";
            }
          } else {
            res.stage = "exchange";
            res.error = "app_catalog bad payload";
          }
        }
      } else {
        res.stage = "exchange";
        res.error = err;
      }
      client.close();
    } else {
      res.stage = "exchange";
      res.error = err;
    }
  }

  res.envelopes = server.envelopesReceived();
  server.stop();
  drainConnectionWorkers();
  if (res.passed) {
    log.write(LogLevel::Info, "selftest.pass",
              {{"port", std::to_string(res.port)},
               {"pairing", res.pairing}});
  } else {
    log.write(LogLevel::Error, "selftest.fail",
              {{"stage", res.stage}, {"error", res.error}});
  }
  printSelftest(res);
  return res.passed ? 0 : 1;
}

}  // namespace remotewindows::app
