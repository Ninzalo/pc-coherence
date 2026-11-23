
#ifndef RWH_CORE_PROTOCOL_VALIDATION_HPP
#define RWH_CORE_PROTOCOL_VALIDATION_HPP

#include "core/protocol_types.hpp"

#include <string>

namespace remotewindows::protocol {

struct ValidationResult {
  bool ok = true;
  std::string error_code;  // empty when ok; stable codes per PROTOCOL §8
  std::string detail;
};

// Semantic validation on a successfully decoded envelope (structural
// problems are already reported by the codec as ParseError codes).
// Rules: PROTOCOL.md §4-6; bounds per §6.
ValidationResult validate(const Envelope& e);

// Field helpers shared by Host and Client implementations.
bool isUuid(const std::string& s);
bool isAppId(const std::string& s);      // ^[A-Za-z0-9][A-Za-z0-9_-]{0,127}$
bool isSixDigitCode(const std::string& s);
bool isHex32(const std::string& s);
bool validAppName(const std::string& s); // 1..64 printable
int32_t utf8Length(const std::string& s); // code-point count, -1 if invalid

}  // namespace remotewindows::protocol

#endif
