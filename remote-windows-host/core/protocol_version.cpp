#include "core/protocol_version.hpp"

#include <cctype>

namespace remotewindows::protocol {

std::string Version::toString() const {
  return std::to_string(major) + "." + std::to_string(minor) + "." +
         std::to_string(patch);
}

namespace {
bool parseUint(const std::string& s, uint32_t& out) {
  if (s.empty() || s.size() > 9) return false;
  uint64_t v = 0;
  for (char c : s) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    v = v * 10 + (c - '0');
    if (v > 0xffffffffull) return false;
  }
  out = uint32_t(v);
  return true;
}
}  // namespace

bool parseVersion(const std::string& text, Version& out) {
  Version v;
  size_t start = 0;
  for (int part = 0; part < 3; ++part) {
    size_t dot = text.find('.', start);
    std::string seg = text.substr(start, dot == std::string::npos
                                      ? std::string::npos
                                      : dot - start);
    uint32_t num;
    if (!parseUint(seg, num)) return false;
    if (part == 0) v.major = num;
    else if (part == 1) v.minor = num;
    else v.patch = num;
    if (dot == std::string::npos) {
      if (part != 2) return false;  // expected more dots
      // Final segment consumed the rest of the string: valid.
      out = v;
      return true;
    }
    start = dot + 1;
  }
  // Three dots were found ("1.2.3." or "1.2.3.4") — invalid.
  return false;
}

bool compatible(uint32_t my_major, uint32_t my_minor,
                uint32_t peer_major, uint32_t peer_minor) {
  (void)my_minor;
  (void)peer_minor;
  // v0.x rule: majors must match exactly; minors are forward-compatible
  // (unknown fields/kinds are ignored by the lower-minor peer).
  return my_major == peer_major;
}

}  // namespace remotewindows::protocol
