#ifndef RWH_APP_APPCONTROLLER_HPP
#define RWH_APP_APPCONTROLLER_HPP

// Windows Host application lifecycle (H-FOUND, plan section 4.1):
// structured logging, signaling-server start/stop (FR-10), tray presence,
// and a headless --selftest smoke path for the build/test runner.
//
// Foundation scope only: this starts the real signaling channel and answers
// HELLO. The pairing state machine, session authorization, and all control
// routing are the next H-NET task on top of this shell.

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
  std::string stage;   // "start" | "exchange" | "none" on pass
  std::string error;
  uint16_t port = 0;
  std::string reply_kind;
  std::size_t envelopes = 0;
};

class AppController {
 public:
  // GUI mode: logging + signaling server + tray icon + message loop.
  int runGui(const Options& opts);
  // Headless mode (used by the Windows build/test runner): starts the real
  // signaling server on 127.0.0.1:<port>, performs a real HELLO /
  // pairing-challenge exchange with an in-process client, prints a
  // machine-readable JSON report to stdout. Returns 0 on pass.
  int runSelftest(const Options& opts);
};

}  // namespace remotewindows::app

#endif
