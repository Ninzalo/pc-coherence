#ifndef RWH_APP_APPCONTROLLER_HPP
#define RWH_APP_APPCONTROLLER_HPP

// Windows Host application lifecycle (H-FOUND shell + H-NET session layer):
// structured logging, signaling-server start/stop (FR-10), session
// authorization + pairing state machine over the channel (FR-13,
// RW-H-004), tray presence, and a headless --selftest smoke path for the
// build/test runner.
//
// The pairing handshake is fully wired: HELLO -> HELLO +
// PAIRING_CHALLENGE -> PAIRING (code + nonce echo) -> PAIRING_RESULT,
// then a minimal post-pairing control exchange (empty catalog). Catalog
// scanning and real control routing are later tasks (H-CATALOG et al.).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace remotewindows::app {

struct Options {
  // Listen address for the real server ("" -> 0.0.0.0, the LAN interface
  // per FR-10). The selftest always binds 127.0.0.1.
  std::string addr;
  uint16_t port = 47987;  // product signaling port (plan section 18.6)
  bool selftest = false;
};

struct SelftestResult {
  bool passed = false;
  std::string stage;    // "start" | "exchange" | "none" on pass
  std::string error;
  uint16_t port = 0;
  std::string reply_kind;  // last reply kind received by the test client
  std::size_t envelopes = 0;
  // RW-H-004: full-handshake report fields (machine-readable English).
  std::string pairing;    // "none" | "accepted" | "rejected"
  std::string reply_kinds;  // ';'-joined reply kinds in arrival order
};

class AppController {
 public:
  // GUI mode: logging + signaling server + tray icon + message loop.
  int runGui(const Options& opts);
  // Headless mode (used by the Windows build/test runner): starts the real
  // signaling server on 127.0.0.1:<port>, performs the full pairing
  // handshake with an in-process client (HELLO -> HELLO ->
  // PAIRING_CHALLENGE -> PAIRING_RESULT(accepted) -> APP_CATALOG_REQUEST
  // -> APP_CATALOG(empty)), prints a machine-readable JSON report to
  // stdout. Returns 0 on pass.
  int runSelftest(const Options& opts);
};

}  // namespace remotewindows::app

#endif
