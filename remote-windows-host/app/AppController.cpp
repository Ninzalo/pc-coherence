
#include "app/AppController.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
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

#ifdef _WIN32
// One-shot window-creation probe for the runProbeTray bisect: register
// a fresh class (DefWindowProc), create a message-only window, report
// "ok" or "null:<lasterror>", destroy the window.
std::string probeWindow(const wchar_t* cls) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = DefWindowProcW;
  wc.hInstance = GetModuleHandleW(nullptr);
  wc.lpszClassName = cls;
  if (!RegisterClassExW(&wc))
    return "reg_fail:" + std::to_string(GetLastError());
  HWND h = CreateWindowExW(0, cls, L"rwh-probe", 0, 0, 0, 0, 0,
                           HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr),
                           nullptr);
  const std::string r = h ? "ok" : "null:" + std::to_string(GetLastError());
  if (h) DestroyWindow(h);
  return r;
}

// i: mirrors TrayController::wndProc — reads GWLP_USERDATA on EVERY
//    message (before the switch), including WM_NCCREATE.
LRESULT CALLBACK probeSelfOfTop(HWND h, UINT m, WPARAM w, LPARAM l) {
  auto* self = reinterpret_cast<void*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  (void)self;
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// j: reads GWLP_USERDATA only inside WM_NCCREATE.
LRESULT CALLBACK probeReadInNcCreate(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    const LONG_PTR cur = GetWindowLongPtrW(h, GWLP_USERDATA);
    (void)cur;
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// k: standard pattern — SetWindowLongPtrW only, never reads USERDATA
//    during creation.
LRESULT CALLBACK probeStandardOnly(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// l: intercepts WM_NCCREATE but only forwards to DefWindowProcW
//    (pure-interception test — no property writes).
LRESULT CALLBACK probeNcCreateForward(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) return DefWindowProcW(h, m, w, l);
  return DefWindowProcW(h, m, w, l);
}

// m: WM_NCCREATE via SetPropW (the documented SetProp/GetProp
//    alternative to GWLP_USERDATA).
LRESULT CALLBACK probeSetPropInNc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetPropW(h, L"RWHProbeSelf", cs->lpCreateParams);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// n: WM_NCCREATE forwarded; SetWindowLongPtrW(GWLP_USERDATA) done
//    later, after creation, in response to a custom message.
LRESULT CALLBACK probeDeferredSet(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) return DefWindowProcW(h, m, w, l);
  if (m == WM_APP + 42) {
    SetWindowLongPtrW(h, GWLP_USERDATA, 0x1234);
    return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

// q: candidate fix — SetWindowLongPtrW(GWLP_USERDATA) in WM_NCCREATE
//    AND return DefWindowProcW's result (the documented contract).
LRESULT CALLBACK probeFixedSetLong(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetWindowLongPtrW(h, GWLP_USERDATA,
                      reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    return DefWindowProcW(h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

// r: candidate fix — SetPropW in WM_NCCREATE + DefWindowProcW result.
LRESULT CALLBACK probeFixedSetProp(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
    SetPropW(h, L"RWHProbeSelf", cs->lpCreateParams);
    return DefWindowProcW(h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}
#endif

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
#ifdef _WIN32
  // Console-subsystem build: a console window exists in GUI mode.
  // Closing it (CTRL_CLOSE) or Ctrl+C must NOT stop the host — the
  // tray is the UI and the server must keep running (diagnosis
  // 2026-11-24: user-visible "host stops after 1 s" failure mode).
  ::SetConsoleCtrlHandler([](DWORD) { return TRUE; }, TRUE);
#endif
  int rc = tray.run();
  if (rc != 0) {
    log.write(LogLevel::Error, "tray.failed",
              {{"code", std::to_string(rc)},
               {"detail", tray.lastError().empty() ? "n/a" : tray.lastError()},
               {"window", tray.windowKind()}});
  } else {
    log.write(LogLevel::Info, "tray.exit",
              {{"window", tray.windowKind()}});
  }

  server.stop();
  drainConnectionWorkers();
  log.write(LogLevel::Info, "host.stop",
            {{"envelopes", std::to_string(server.envelopesReceived())}});
  return rc;
}

int AppController::runProbeTray(const Options& opts) {
#ifdef _WIN32
  Logger& log = Logger::instance();
  std::string err;
  if (!log.init(Logger::defaultPath(err))) {
    std::fprintf(stderr, "warning: log file not opened: %s\n", err.c_str());
  }

  // a: right after logger init — before any networking object exists.
  log.write(LogLevel::Info, "probe.a_logger_only",
            {{"window", probeWindow(L"RWHProbeA")}});

  SignalingSessionHandler handler(hostName(), &log);
  log.write(LogLevel::Info, "probe.b_handler",
            {{"window", probeWindow(L"RWHProbeB")}});

  net::SignalingServer server(&handler);
  if (!server.start("0.0.0.0", opts.port, err)) {
    log.write(LogLevel::Error, "probe.start_failed",
              {{"port", std::to_string(opts.port)}, {"error", err}});
    return 2;
  }
  log.write(LogLevel::Info, "probe.c_server",
            {{"window", probeWindow(L"RWHProbeC")}});

  ::SetConsoleCtrlHandler([](DWORD) { return TRUE; }, TRUE);
  log.write(LogLevel::Info, "probe.d_ctrl_handler",
            {{"window", probeWindow(L"RWHProbeD")}});

  // e/f/g/h: replicate the exact TrayController::run() window setup to
  // isolate which element (class name / wndProc / lpCreateParams /
  // title) breaks creation — d above passed with DefWindowProc + fresh
  // names + nullptr param + title L"rwh-probe".
  struct ProbeParam {
    int x;
  };
  ProbeParam param{0};
  struct ProbeCase {
    const char* label;
    const wchar_t* cls;
    const wchar_t* title;
    LRESULT (*proc)(HWND, UINT, WPARAM, LPARAM);
    void* lp_param;
  };
  const ProbeCase cases[13] = {
      // e: exact TrayController replication (name+proc+param+title)
      {"probe.e_exact", L"RWHTrayWindow", L"RWH", &TrayController::wndProc,
       &param},
      // f: fresh class name, everything else same as e -> isolates name
      {"probe.f_freshname", L"RWHTrayWindowF", L"RWH",
       &TrayController::wndProc, &param},
      // g: control — identical to the passing a-d pattern
      {"probe.g_control", L"RWHTrayWindowG", L"rwh-probe", DefWindowProcW,
       nullptr},
      // h: fresh name + tray wndProc + nullptr param -> isolates lpParam
      {"probe.h_nullparam", L"RWHTrayWindowH", L"RWH",
       &TrayController::wndProc, nullptr},
      // i: USERDATA read on every message (exact TrayController shape)
      {"probe.i_selfoftop", L"RWHTrayWindowI", L"RWH", &probeSelfOfTop,
       nullptr},
      // j: USERDATA read inside WM_NCCREATE only
      {"probe.j_readinnc", L"RWHTrayWindowJ", L"RWH", &probeReadInNcCreate,
       nullptr},
      // k: standard pattern — set USERDATA, never read during creation
      {"probe.k_standard", L"RWHTrayWindowK", L"RWH", &probeStandardOnly,
       nullptr},
      // l: intercepts WM_NCCREATE, only forwards (pure interception)
      {"probe.l_ncforward", L"RWHTrayWindowL", L"RWH", &probeNcCreateForward,
       nullptr},
      // m: WM_NCCREATE via SetPropW (alternative storage)
      {"probe.m_setprop", L"RWHTrayWindowM", L"RWH", &probeSetPropInNc,
       nullptr},
      // n: WM_NCCREATE forwarded; SetWindowLongPtrW deferred post-creation
      {"probe.n_deferred", L"RWHTrayWindowN", L"RWH", &probeDeferredSet,
       nullptr},
      // p: DefWindowProcW + title L"RWH" -> isolates the title
      {"probe.p_title", L"RWHTrayWindowP", L"RWH", DefWindowProcW, nullptr},
      // q: candidate fix — SetWindowLongPtrW + DefWindowProcW result
      {"probe.q_fixedsetlong", L"RWHTrayWindowQ", L"RWH", &probeFixedSetLong,
       &param},
      // r: candidate fix — SetPropW + DefWindowProcW result
      {"probe.r_fixedsetprop", L"RWHTrayWindowR", L"RWH", &probeFixedSetProp,
       &param},
  };
  for (const ProbeCase& c : cases) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = c.proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = c.cls;
    const ATOM atom = RegisterClassExW(&wc);
    const DWORD reg_err = GetLastError();
    std::string window = "reg_fail:" + std::to_string(reg_err);
    if (atom) {
      HWND h = CreateWindowExW(0, c.cls, c.title, 0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr,
                               GetModuleHandleW(nullptr), c.lp_param);
      window = h ? "ok" : "null:" + std::to_string(GetLastError());
      if (h) {
        // n: prove SetWindowLongPtrW(GWLP_USERDATA) works post-creation.
        if (std::strcmp(c.label, "probe.n_deferred") == 0) {
          PostMessageW(h, WM_APP + 42, 0, 0);
          Sleep(50);
          MSG m2{};
          while (PeekMessageW(&m2, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m2);
            DispatchMessageW(&m2);
          }
          const LONG_PTR v = GetWindowLongPtrW(h, GWLP_USERDATA);
          window += " lateuserdata=" + std::to_string((unsigned long)v);
        }
        DestroyWindow(h);
      }
    }
    log.write(LogLevel::Info, c.label,
              {{"window", window}, {"atom", std::to_string(atom)}});
  }

  server.stop();
  drainConnectionWorkers();
  log.write(LogLevel::Info, "probe.done", {});
  return 0;
#else
  std::fprintf(stderr, "probe-tray is Windows-only\n");
  return 2;
#endif
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
