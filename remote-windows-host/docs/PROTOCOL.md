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
- Transport for v0.1.0: direct TCP on the LAN (signaling + control). Video
  will travel on WebRTC media tracks once the pinned WebRTC build is
  integrated; **no video is transmitted in v0.1.0** (see STATUS.md).

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
forbidden — see §9).

## 9. Safety rules

- `system_power_request` is never auto-retried after a timeout; the result
  is reported as unknown. Shutdown never force-closes applications by
  default.
- No message in this protocol carries a filesystem path, command string,
  PID, HWND, or shell expression.
- Malformed frames are dropped and counted; they must not terminate the
  Host process.
