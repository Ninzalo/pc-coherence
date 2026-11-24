# Remote Windows — Control Protocol (v0.1.0)

Canonical schema: `protocol/remote_windows.proto` (owner: CONTRACT role only).
Version: `protocol/VERSION` = **0.1.0**.

## 1. Framing (v0.x)

- Frame: `uint32 big-endian length` + `UTF-8 JSON object`, length ≤ 8 MiB.
- The JSON object is the JSON encoding of `Envelope` (proto field names,
  lower_snake_case enum strings, absent fields omitted, unknown fields
  ignored). See ADR-0002 for the rationale (JSON wire in v0.x while the
  .proto remains the canonical schema).
- One logical session per TCP connection (see §7).
- Transport for v0.1.0: direct TCP on the LAN. Control and signaling use the
  control channel (TCP 47987); video uses the dedicated media channel
  (H.264 over TCP 47988) specified in §9 (ADR-0008). **No video is
  transmitted in v0.1.0** until the media channel is implemented (see
  ADR-0008, STATUS.md).

## 2. Version negotiation

- Every frame carries `schema_major` / `schema_minor`.
- Receivers MUST reject (close the connection) frames with
  `schema_major != 1` for v0.x peers.
- Minor mismatch: a peer with a higher minor may send fields/kinds the
  receiver does not know; receivers MUST ignore unknown fields and kinds
  they do not implement (forward compatibility), and log a warning.
- Handshake: both sides send `hello` first. If capabilities disagree, the
  session continues with the intersection.
- Breaking changes bump MAJOR and require a compatibility note here.

## 3. Kinds and direction

| kind | direction | request_id | sequence |
|---|---|---|---|
| hello | both | – | – |
| pairing_challenge | host→client (nonce), client→host (code) | – | – |
| pairing_result | host→client | – | – |
| app_catalog_request | client→host | required | – |
| app_catalog | host→client | echo | – |
| app_catalog_changed | host→client | – | – |
| launch_app | client→host | required | – |
| launch_result | host→client | echo | – |
| app_status | host→client | – | – |
| window_announce | host→client | – | – |
| window_update | host→client | – | – |
| window_remove | host→client | – | – |
| input_event | client→host | – | required |
| resize_request | client→host | required | required |
| resize_ack | host→client | echo | – |
| window_action | client→host | required | required |
| window_action_result | host→client | echo | – |
| frame_rate_request | client→host | required | – |
| stream_status | host→client | – | – |
| system_power_request | client→host | required | required |
| system_power_result | host→client | echo | – |
| heartbeat | both | – | – |
| performance_stats | host→client | – | – |

Sequence numbers are strictly increasing per sender per connection; the
receiver drops (and counts) out-of-order or repeated sequences for
`input_event` key events (down/up events must never be reordered or lost).
`resize_request` sequences are used to coalesce obsolete requests.

## 4. Identification rules

- `window_id`: UUID v4 assigned by the Host. Native HWND values are never
  transmitted (Windows reuses handles).
- `application_session_id`: `[A-Za-z0-9-]{1,64}`, Host-assigned per launched
  application session. Always present on window messages.
- `app_id`: Host-generated, `[A-Za-z0-9_-]{1,64}`, resolved only through the
  Host catalog. Clients must never send paths, commands, or arguments.
- `parent_window_id` / `owner_window_id`: optional; when present they MUST
  reference an already-announced (or simultaneously announced) `window_id`.
  Absence is legal while session association is present.

## 5. Per-field validation (both peers implement)

Common:
- strings: valid UTF-8, no NUL, max length per field below.
- enums: unknown string values → frame invalid.
- numeric ranges as listed; out of range → frame invalid (drop + counter,
  never crash).
- The Host JSON codec enforces the `window_id` UUID format and the `title`
  length bound at decode time: violations are rejected with the error codes
  `bad_uuid` and `title_too_long` respectively, before any routing.

| field | rule |
|---|---|
| `app_version`, `host_name` | 1..64 chars |
| `display_name` | 1..256 chars |
| `app_id` | 1..64, `[A-Za-z0-9_-]` |
| `application_session_id` | 1..64, `[A-Za-z0-9-]` |
| `window_id` | UUID string (8-4-4-4-12 hex) |
| `title` | 0..512 chars |
| `error_code` | 0..64, `[A-Za-z0-9_.-]` |
| `safe_message` | 0..512 chars |
| `one_time_code` | exactly 6 ASCII digits |
| `challenge_nonce` | exactly 32 lowercase hex chars |
| geometry `x`,`y` | int32 within ±4,194,303 |
| `width_px`, `height_px` | 1..38,400 |
| `dpi_scale_percent` | 25..400 |
| `target_fps` | 0..240 (0 = paused) |
| `windows_vk` | 1..0xFF |
| `modifier_bits` | 0..0xF (bit0=shift, bit1=ctrl, bit2=alt, bit3=win) |
| `wheel_delta` | ±2,000 |
| `catalog_revision` | ≥ 1 |
| `entries` (catalog) | ≤ 10,000 |
| `capabilities` | ≤ 16, no duplicates, no `unspecified` |
| `z_order` | int32 |

Kinds with mandatory content:
- `hello`: `product_name` ∈ {`windows-host`,`macos-client`}; `app_version`.
- `pairing_challenge`: at least one of the three fields; if present each
  field obeys its rule.
- `launch_app` / `launch_result`: `app_id`; `launch_result.state` ≠
  `unspecified`; `application_session_id` present when state is
  `starting`/`started`/`window_detected`.
- `window_announce`: `window_id`, `application_session_id`, `role` ≠
  `unspecified`, geometry present.
- `window_update`: `window_id` plus at least one other field.
- `input_event`: `window_id`; exactly one of `mouse`/`key`;
  `mouse.kind` ≠ `unspecified`; for key events `key_down` is mandatory
  (explicit bool) and `sequence` is mandatory at envelope level.
- `resize_request`: dimensions within range.
- `system_power_request`: `action` ∈ {`sleep`,`shutdown`}.
- `heartbeat`: `sender_time_ms`.

## 6. Authorization gates (Host)

Pre-pairing, the Host accepts only: `hello`, `pairing_challenge`,
`pairing_result`, `app_catalog_request`, `app_catalog`,
`app_catalog_changed`, `heartbeat`. Everything else (launch, input,
resize, window actions, power, frame rate) is rejected with a typed result
(`denied`, `error_code = "not_paired"`) until pairing succeeds. Power
commands additionally require the paired client to be authorized for
power control.

## 7. Session lifecycle

1. Client connects over TCP (manual host IP/port entry in the MVP).
2. Both sides send `hello`; the Host replies with `pairing_challenge`
   (nonce + host name) when not yet paired.
3. Client displays the nonce; the user types the 6-digit one-time code
   shown on the Host screen; client sends `pairing_challenge` with the
   code + echoed nonce.
4. Host validates (code match, nonce echo, attempt limit: 3 failures then
   60 s lockout) and answers `pairing_result`. On success both sides store
   the derived pairing key (Host: protected local file; Client: Keychain).
   The key = `hex(sha256("rwpair1|" + code + "|" + nonce))`.
5. Steady state: heartbeats every 10 s from each side; a peer that is
   silent for 60 s is disconnected by the other side (input state reset
   on the Host: no key or button may stay logically pressed).
6. On any disconnect the Host resets per-connection key/button state and
   sequence trackers; pairing state persists.

## 8. Error codes (open set, documented core)

`not_paired`, `unauthorized`, `unknown_app_id`, `launch_failed`,
`window_not_found`, `window_busy`, `resize_rejected`, `power_privilege`,
`power_not_allowed`, `rate_limited`, `protocol_mismatch`, `malformed_frame`,
`sequence_gap`, `rate_limited_power_retry` (never sent; retry is
forbidden — see §10).

## 9. Media channel (H.264, dedicated TCP 47988)

Normative spec for the ADR-0008 MVP media transport. This section does not
change the v0.1.0 control schema (`protocol/VERSION` stays 0.1.0; the media
channel adds no envelope kinds). All control and signaling stays on the paired
control channel (TCP 47987); this channel carries video only.

### 9.1 Scope

- The media channel carries **video only** (H.264 NAL byte payloads). All
  control and signaling — every kind in §3 — stays on the paired control
  channel (TCP 47987).
- Video never passes through signaling (ADR-0001 constraint, kept).
- The channel is a byte stream of fixed-format packets (§9.4); it carries no
  JSON envelopes and no control semantics.

### 9.2 Port and handshake

- The Host listens on **TCP 47988**.
- A media connection is accepted **only** from a peer that completed pairing
  (shares a session key) over 47987. Unpaired peers are refused.
- Handshake (Client = initiator, Host = responder):
  1. Client connects to 47988.
  2. Host sends 16 random bytes (`host_nonce`, from a CSPRNG).
  3. Client replies with 32 bytes =
     `HMAC-SHA256(session_key_bytes, "rwh-media-v1|" || host_nonce)`, where
     `session_key_bytes` are the 32 raw bytes of the paired session key (§9.3)
     and `||` is byte concatenation.
  4. The Host computes the same HMAC for each currently-paired session key and
     compares. On any match it sends `0x01` (accept); otherwise it sends `0x00`
     and closes the connection.
- The handshake proves knowledge of a valid session key without transmitting
  it. On `0x00`, a timeout, or any malformed handshake step, the Host closes
  the connection.

### 9.3 Key derivation

Exact byte formats (lowercase-hex SHA-256, as in `core/sha256.cpp`; little-
endian where binary). A golden vector MUST reproduce these values exactly
(`protocol/test-vectors/media/key-derivation.json`).

- `session_key` = `hex(sha256("rwh-pairing-v1|" + code + "|" + host_name))` —
  32-byte digest (64 lowercase-hex chars). This is exactly
  `core::PairingManager::deriveSessionKey` (`core/pairing.cpp`); both peers
  derive it from the one-time `code` + `host_name` at pairing time.
- `media_key(window_id)` =
  `hex(sha256("rwh-media-v1|" + session_key_hex + "|" + window_id))` — 32
  bytes; the AES-256 key for that window's stream.
- `nonce_prefix(window_id)` = the first 8 bytes of
  `sha256("rwh-media-nonce-v1|" + session_key_hex + "|" + window_id)`.

`session_key_hex` is the 64 lowercase-hex string form of `session_key`;
`window_id` is the `window_announce` UUID string (not the `stream_id`). Each
`window_id` gets its own `media_key` and `nonce_prefix` (per-stream key
separation).

### 9.4 Packet framing (little-endian, per packet)

Byte layout of one packet; all integers little-endian:

| field | type | value |
|---|---|---|
| `magic` | u8 | `0x52` ('R') |
| `version` | u8 | `1` |
| `stream_id` | u16 | first 2 bytes of `sha256(window_id)` (UTF-8 UUID string), read little-endian |
| `flags` | u8 | bit0 = keyframe |
| `frame_seq` | u32 | per stream, starts at 0, wraps mod 2^32 |
| `payload_len` | u32 | ciphertext length = plaintext length + 16 (tag) |
| `ciphertext` | `payload_len` bytes | AES-256-GCM ciphertext of the H.264 byte payload, 16-byte GCM tag appended |

- `payload_len` is in bytes and equals plaintext length + 16.
- The AES-256-GCM **key** is `media_key(window_id)` (§9.3).
- The AES-256-GCM **nonce** (12 bytes) = `nonce_prefix(window_id)` (8 bytes)
  `||` `packet_counter` (u32 little-endian, per stream, starts at 0).
- AAD is empty.
- `payload_len` MUST be ≤ 16 MiB. A `payload_len` greater than 16 MiB, a
  malformed `magic`, or a wrong `version` is malformed (§9.6).
- The vectors in `protocol/test-vectors/media/aes-256-gcm.json` MUST be
  reproduced exactly by every AES-256-GCM backend (Windows CNG `BCrypt*`,
  macOS CryptoKit `AES.GCM`).

### 9.5 Stream lifecycle

- A media stream follows `window_announce` / `window_remove` on the control
  channel (47987).
- On `window_announce`: the Host starts the stream; its **first** packet on
  that stream MUST be a keyframe (`flags` bit0 set).
- On `window_remove`: the Host stops the stream; the Client discards it.
- After **any** media-channel reconnect, the Host re-sends a keyframe for each
  active stream.
- FPS gating is done Host-side **before** encoding (FR-09); the
  `stream_status` kind stays on 47987, not on the media channel.

### 9.6 Robustness

- A malformed `magic`, a wrong `version`, or `payload_len` > 16 MiB: the
  receiving side closes the media connection.
- The Client reconnects and re-handshakes (§9.2) after a close.
- There is **no** retransmission logic: TCP is reliable and in-order (LAN loss
  is negligible at the measured link rates).
- Malformed input must never crash the process (plan FR-13); malformed packets
  are dropped and the connection is reset.

### 9.7 Security

- No pairing → no session key → the §9.2 handshake cannot succeed; no media is
  possible on an unpaired connection.
- Per-window key separation: each `window_id` has its own `media_key`.
- AES-256-GCM provides authenticated encryption (confidentiality + integrity);
  a bad GCM tag means the packet is discarded.
- The media channel carries no control semantics.
- Frame contents and all derived keys are never logged.
- FR-13 deviation (recorded in ADR-0008 and `docs/STATUS.md`): media is
  protected with per-stream AES-256-GCM derived from the pairing session key,
  not DTLS-SRTP.

## 10. Safety rules

- `system_power_request` is never auto-retried after a timeout; the result
  is reported as unknown. Shutdown never force-closes applications by
  default.
- No message in this protocol carries a filesystem path, command string,
  PID, HWND, or shell expression.
- Malformed frames are dropped and counted; they must not terminate the
  Host process.
