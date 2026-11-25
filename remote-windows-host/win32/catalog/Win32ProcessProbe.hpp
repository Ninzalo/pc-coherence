
#ifndef RWH_WIN32_CATALOG_WIN32PROCESSPROBE_HPP
#define RWH_WIN32_CATALOG_WIN32PROCESSPROBE_HPP

// RW-H-009 (H-CATALOG): process liveness probe for launched applications
// (FR-02 close detection). WIN32-only: OpenProcess/GetExitCodeProcess.
// Implements core::ProcessProbe; used by core::LaunchService::sweep (the
// dead-process -> AppStatus{CLOSED} transition).

#ifdef _WIN32

#include <cstdint>

#include "core/catalog_service.hpp"

namespace remotewindows::win32 {

// isAlive(pid): OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION); on
// OpenProcess failure returns TRUE — conservative: an access-restricted
// RUNNING process must not produce a false "closed" event (documented in
// the packet; access failure is indistinguishable from a running process
// we cannot query). Otherwise GetExitCodeProcess: STILL_ACTIVE -> alive,
// anything else -> dead. The handle is always closed. pid 0 is never
// passed by LaunchService (sweep skips sessions without liveness data).
//
// isHandleAlive(held): the authoritative check. The held HANDLE keeps the
// process object alive after termination, so GetExitCodeProcess reports
// the real exit code of a naturally exited process (by pid alone such a
// process is unopenable -> false alive, and a reused pid can look dead ->
// false CLOSED). GetExitCodeProcess failure -> conservative alive.
class Win32ProcessProbe : public core::ProcessProbe {
 public:
  bool isAlive(uint32_t pid) override;
  bool isHandleAlive(void* handle) override;
  void closeProcessHandle(void* handle) override;
};

}  // namespace remotewindows::win32

#endif  // _WIN32

#endif  // RWH_WIN32_CATALOG_WIN32PROCESSPROBE_HPP
