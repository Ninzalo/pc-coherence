
#ifndef RWH_NETWORK_FRAMING_HPP
#define RWH_NETWORK_FRAMING_HPP

// PROTOCOL §3: frame = 4-byte big-endian length + UTF-8 JSON payload.

#include <cstdint>
#include <string>
#include <vector>

#include "network/tcp_socket.hpp"

namespace remotewindows::net {

inline constexpr size_t kMaxFrameBytes = 8 * 1024 * 1024;

std::vector<uint8_t> encodeFrame(const std::string& json);
// Reads exactly n bytes (blocking). Returns false on error/disconnect.
bool recvExact(TcpSocket& s, uint8_t* out, size_t n, std::string& err);
// Reads one length-prefixed frame. Rejects frames > kMaxFrameBytes.
bool readFrame(TcpSocket& s, std::string& jsonOut, std::string& err);

}  // namespace remotewindows::net

#endif
