
#ifndef RWH_CORE_PROTOCOL_JSON_HPP
#define RWH_CORE_PROTOCOL_JSON_HPP

// v0.x wire codec: length-prefixed UTF-8 JSON encoding of Envelope
// (see ADR-0002 and docs/PROTOCOL.md). The .proto remains canonical.

#include <nlohmann/json.hpp>

#include "core/protocol_types.hpp"

#include <stdexcept>
#include <string>

namespace remotewindows::protocol {

// Thrown by the codec on structural problems; `code` is a stable
// machine-readable error code (PROTOCOL §8).
class ParseError : public std::runtime_error {
 public:
  ParseError(std::string code, std::string message);
  std::string code;
};

// Serialization.
nlohmann::json toEnvelopeJson(const Envelope& e);
std::string encodeEnvelope(const Envelope& e);            // throws ParseError
bool encodeEnvelopeSafe(const Envelope& e, std::string& out);

// Parsing. fromEnvelopeJson throws ParseError on any structural problem
// (unknown kind, bad oneof, unknown enum, missing required field, ...).
Envelope fromEnvelopeJson(const nlohmann::json& j);
bool decodeEnvelope(const std::string& jsonText, Envelope& out,
                    std::string& err_code);

}  // namespace remotewindows::protocol

#endif
