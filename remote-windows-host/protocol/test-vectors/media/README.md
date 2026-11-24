# RW media-channel golden vectors

Golden crypto vectors for the ADR-0008 media channel, normative spec in
`docs/PROTOCOL.md` §9 (CONTRACT task RW-H-006). These are the reference values
every AES-256-GCM / SHA-256 implementation in this product must reproduce
**byte-for-byte** so Windows (CNG `BCrypt*`) and macOS (CryptoKit `AES.GCM`)
parity is testable without a shared library.

All inputs and outputs below are **synthetic** — no real pairing code, session
key, or window is present.

## Files

- `aes-256-gcm.json` — 3 AES-256-GCM vectors (empty AAD). Each lists
  `key`, `nonce`, `aad`, `plaintext`, and the expected `ciphertext` + `tag`
  (all lowercase hex). Covers: (a) ascending key/nonce, 64-byte patterned
  plaintext; (b) `0xA5`-filled key, counter nonce, 1000-byte patterned
  plaintext; (c) 1-byte plaintext.
- `key-derivation.json` — expected `session_key`, `media_key`, and
  `nonce_prefix` for the fixed synthetic inputs
  (`code=RWH-DEADBEEF`, `host_name=TESTHOST`,
  `window_id=11111111-2222-3333-4444-555555555555`), per §9.3.

## Reproduction rule

Every AES-256-GCM backend (Windows CNG `BCrypt*`, macOS CryptoKit `AES.GCM`)
MUST, for each vector in `aes-256-gcm.json`, produce **exactly** the listed
`ciphertext` and `tag` for the given `key`/`nonce`/`aad`/`plaintext`. Any
difference is a backend bug. Likewise, §9.3 key derivation MUST produce the
exact `values` in `key-derivation.json` from the listed `inputs`.

## Generator

- Tool: Apple CryptoKit (`AES.GCM.seal` with no AAD; `SHA256.hash`), Swift
  6.4.2, macOS 27.2.1.
- Date: 2026-11-24.
- The generator performs a self round-trip (seal then open) on every vector
  before writing; the committed values are the `seal` outputs.

## Independent verification (all performed 2026-11-24, all PASS)

The committed values were checked against two or more independent
implementations. To re-verify:

1. AES-256-GCM — re-encrypt each vector with `pyca/cryptography`
   (`cryptography.hazmat.primitives.ciphers.aead.AESGCM`) and compare
   `ciphertext`/`tag` to the committed JSON. (Used: pyca/cryptography 50.0.2.)
2. AES-256-GCM + SHA-256 — recompute with OpenSSL (`EVP_aes_256_gcm`,
   `EVP_sha256`) and compare to the committed JSON. (Used: OpenSSL 3.5.9.)
3. Key derivation — recompute with `shasum -a 256`:
   ```sh
   sk=$(printf '%s' 'rwh-pairing-v1|RWH-DEADBEEF|TESTHOST' | shasum -a 256 | cut -d' ' -f1)
   printf '%s' "rwh-media-v1|$sk|11111111-2222-3333-4444-555555555555"   | shasum -a 256   # media_key
   printf '%s' "rwh-media-nonce-v1|$sk|11111111-2222-3333-4444-555555555555" | shasum -a 256  # nonce_prefix (first 8 bytes)
   ```

SHA-256 here is the same algorithm as `core/sha256.cpp` (lowercase hex,
`0123456789abcdef`).

## Regenerating the AES vectors

Any correct AES-256-GCM tool can reproduce the `ciphertext`/`tag` from the
`key`/`nonce`/`aad`/`plaintext` already listed in `aes-256-gcm.json` (the
inputs are self-contained in that file). For example with `pyca/cryptography`:

```python
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
out = AESGCM(bytes.fromhex(key)).encrypt(bytes.fromhex(nonce),
                                         bytes.fromhex(plaintext),
                                         bytes.fromhex(aad))
ciphertext, tag = out[:-16].hex(), out[-16:].hex()
```
