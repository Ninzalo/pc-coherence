
#include "app/Logger.hpp"

#include <chrono>
#include <ctime>

#ifdef _WIN32
#include "app/Win32Env.hpp"
#endif

namespace remotewindows::app {

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

bool Logger::init(const std::string& path) {
  std::lock_guard<std::mutex> lk(mu_);
  if (file_open_) return true;
#ifdef _WIN32
  // Best-effort parent-directory creation (split on backslash, create each
  // level). Failure is non-fatal: fopen below decides.
  {
    std::wstring wide;
    for (char c : path) wide += (wchar_t)(c < 128 ? c : '?');
    size_t pos = 0;
    while ((pos = wide.find(L'\\', pos + 1)) != std::wstring::npos) {
      CreateDirectoryW(wide.substr(0, pos).c_str(), nullptr);
    }
  }
#endif
  file_ = std::fopen(path.c_str(), "a");
  if (!file_) return false;
  file_open_ = true;
  return true;
}

void Logger::write(LogLevel level, const std::string& event,
                   const std::vector<std::pair<std::string, std::string>>&
                       fields) {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm_utc{};
#ifdef _WIN32
  gmtime_s(&tm_utc, &t);
#else
  gmtime_r(&t, &tm_utc);
#endif
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

  const char* lvl = level == LogLevel::Info
                        ? "INFO"
                        : (level == LogLevel::Warn ? "WARN" : "ERROR");
  std::string line;
  line += ts;
  line += " ";
  line += lvl;
  line += " event=\"";
  line += event;
  line += "\"";
  for (const auto& f : fields) {
    line += " ";
    line += f.first;
    line += "=\"";
    line += f.second;
    line += "\"";
  }

  {
    std::lock_guard<std::mutex> lk(mu_);
    if (file_open_ && file_) std::fprintf(file_, "%s\n", line.c_str());
  }
  // Mirror to stdout (the app is a console-subsystem binary; the build/test
  // runner captures this stream).
  std::printf("%s\n", line.c_str());
}

std::string Logger::defaultPath(std::string& err) {
#ifdef _WIN32
  // GetKnownFolderPath (the documented replacement for SHGetAllocatedString,
  // which this SDK has removed) requires an initialized COM apartment; the
  // LOCALAPPDATA environment variable is always set in the interactive user
  // session and keeps the logger COM-free.
  wchar_t buf[MAX_PATH] = {0};
  DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
  if (n == 0 || n >= MAX_PATH) {
    err = "cannot resolve %LOCALAPPDATA%";
    return {};
  }
  std::wstring w(buf);
  std::wstring full = w + L"\\RemoteWindows\\host.log";
  std::string out;
  out.reserve(full.size());
  for (wchar_t c : full) out += (char)(c < 128 ? c : '?');
  err.clear();
  return out;
#else
  const char* home = std::getenv("HOME");
  err.clear();
  return std::string(home ? home : "/tmp") + "/.remote-windows/host.log";
#endif
}

}  // namespace remotewindows::app
