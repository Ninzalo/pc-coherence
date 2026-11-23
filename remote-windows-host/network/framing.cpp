
#include "network/framing.hpp"

namespace remotewindows::net {

std::vector<uint8_t> encodeFrame(const std::string& json) {
  std::vector<uint8_t> out(4 + json.size());
  uint32_t len = (uint32_t)json.size();
  out[0] = (uint8_t)(len >> 24);
  out[1] = (uint8_t)(len >> 16);
  out[2] = (uint8_t)(len >> 8);
  out[3] = (uint8_t)(len);
  if (!json.empty())
    std::memcpy(out.data() + 4, json.data(), json.size());
  return out;
}

bool recvExact(TcpSocket& s, uint8_t* out, size_t n, std::string& err) {
  size_t got = 0;
  while (got < n) {
    ssize_t r = s.recvSome(out + got, n - got, err);
    if (r < 0) {
      if (got == 0) return false;
      err = "connection lost mid-frame";
      return false;
    }
    if (r == 0) {
      err = got == 0 ? "connection closed" : "connection lost mid-frame";
      return false;
    }
    got += (size_t)r;
  }
  return true;
}

bool readFrame(TcpSocket& s, std::string& jsonOut, std::string& err) {
  uint8_t header[4];
  if (!recvExact(s, header, 4, err)) return false;
  uint32_t len = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) |
                 ((uint32_t)header[2] << 8) | (uint32_t)header[3];
  if (len > kMaxFrameBytes) {
    err = "frame_too_large";
    return false;
  }
  std::string body;
  body.resize(len);
  if (len > 0) {
    if (!recvExact(s, reinterpret_cast<uint8_t*>(body.data()), len, err))
      return false;
  }
  if (!body.empty() && body.back() != '\0' && body.find('\0') != std::string::npos) {
    // JSON must not contain raw NUL bytes.
    err = "malformed_json";
    return false;
  }
  jsonOut = std::move(body);
  return true;
}

}  // namespace remotewindows::net
