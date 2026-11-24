#ifndef RWH_APP_LOGGER_HPP
#define RWH_APP_LOGGER_HPP

// Structured logging scaffolding for the Windows Host (H-FOUND).
//
// Line-based structured log: ISO-8601 UTC timestamp, level, event name,
// key=value fields. Content policy (plan section 3.1 / hard rules):
// event names and safe identifiers (ports, counts, ids, error codes) only.
// Never log window contents, typed text, or secrets.

#include <cstdio>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace remotewindows::app {

enum class LogLevel { Info, Warn, Error };

class Logger {
 public:
  static Logger& instance();

  // Opens <path> in append mode, creating parent directories best-effort.
  // Returns false on failure; logging then falls back to stdout only.
  bool init(const std::string& path);
  bool fileOpen() const { return file_open_; }

  void write(LogLevel level, const std::string& event,
             const std::vector<std::pair<std::string, std::string>>& fields =
                 {});

  // Log file location: %LOCALAPPDATA%\RemoteWindows\host.log on Windows.
  static std::string defaultPath(std::string& err);

 private:
  Logger() = default;

  std::mutex mu_;
  std::FILE* file_ = nullptr;
  bool file_open_ = false;
};

}  // namespace remotewindows::app

#endif
