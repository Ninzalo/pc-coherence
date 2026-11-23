#ifndef RWH_CORE_PROTOCOL_VERSION_HPP
#define RWH_CORE_PROTOCOL_VERSION_HPP

#include <cstdint>
#include <string>

namespace remotewindows::protocol {

struct Version {
  uint32_t major = 0;
  uint32_t minor = 0;
  uint32_t patch = 0;
  std::string toString() const;
};

// Parses "MAJOR.MINOR.PATCH" (no leading zeros required, non-negative).
// Returns false and leaves out unchanged on malformed input.
bool parseVersion(const std::string& text, Version& out);

// True if a peer speaking `peer` can interoperate with a local `mine`
// under the v0.x rules: same major; minor mismatch is tolerated (the
// higher-minor side may send ignorable fields/kinds).
bool compatible(uint32_t my_major, uint32_t my_minor,
                uint32_t peer_major, uint32_t peer_minor);

}  // namespace remotewindows::protocol

#endif
