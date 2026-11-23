# ADR-0002: v0.x wire format — length-prefixed JSON over TCP

Date: 2026-11-23 · Status: accepted

## Context

The canonical schema is Protocol Buffers (plan §3.3). Using binary
protobuf on the wire requires pinning a protoc codegen toolchain in both
builds. The development environment (this Mac mini + an as-yet-unprovisioned
Windows runner) does not yet have that toolchain verified, and the MVP
control channel is low-bandwidth (no video on it in v0.1.0).

## Decision

v0.1.0 frames are `uint32 BE length + UTF-8 JSON object`, where the JSON
is the JSON encoding of the canonical `Envelope` (proto field names,
lower_snake_case enum strings). The .proto remains the canonical schema
and the single source of truth for field names/numbers; the runtime codec
implements the same schema directly. Binary protobuf framing (or
SwiftProtobuf/nanopb generated types) is a **later change that MUST bump
the protocol version**.

## Rationale

- Real, testable, cross-language interoperability today without codegen
  dependencies (C++ core and Swift client share golden JSON vectors).
- Control-plane bandwidth is negligible; video never uses this channel.
- The canonical schema is unchanged, so the upgrade is mechanical.

## Consequences

- Frame sizes are larger than binary (acceptable at control rates).
- Strict JSON validation is part of the security boundary (PROTOCOL §5).
- STATUS.md records this as a known v0.1.0 simplification.
