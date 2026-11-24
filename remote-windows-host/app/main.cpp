
// Windows Host entry point (H-FOUND, plan section 4.1).
//
//   rwh-host [--port N] [--addr A]
//     GUI mode: tray icon + LAN signaling server (default port 47987).
//   rwh-host --selftest [--port N]
//     Headless smoke test for the build/test runner: real server on
//     127.0.0.1 + real in-process HELLO/pairing-challenge exchange,
//     machine-readable JSON report on stdout. Exit 0 on pass.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "app/AppController.hpp"

namespace {
void printUsage() {
  std::fprintf(stderr,
               "usage: rwh-host [--port N] [--addr A] | "
               "rwh-host --selftest [--port N]\n");
}
}  // namespace

int main(int argc, char** argv) {
  remotewindows::app::Options opts;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) {
      opts.port = (uint16_t)std::strtoul(argv[++i], nullptr, 10);
    } else if (a == "--addr" && i + 1 < argc) {
      opts.addr = argv[++i];
    } else if (a == "--selftest") {
      opts.selftest = true;
    } else if (a == "--probe-tray") {
      opts.probe_tray = true;
    } else {
      printUsage();
      return 2;
    }
  }
  remotewindows::app::AppController controller;
  return opts.selftest      ? controller.runSelftest(opts)
         : opts.probe_tray  ? controller.runProbeTray(opts)
                            : controller.runGui(opts);
}
