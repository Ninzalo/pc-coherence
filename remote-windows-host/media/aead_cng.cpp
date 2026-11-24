
// Windows CNG (BCrypt*) AES-256-GCM backend for the media channel
// (PROTOCOL.md §9.4, ADR-0008). WIN32-only translation unit: compiled
// only on Windows (see CMakeLists.txt), unit-tested only on the Windows
// runner — NOT RUN on macOS.
//
// SDK notes (Windows SDK 10.0.26100.0, verified against the installed
// shared/bcrypt.h on the runner host):
//  - BCryptCreateKey is NOT declared in this SDK's bcrypt.h; key
//    material is loaded with
//    BCryptGenerateSymmetricKey(alg, &key, NULL, 0, secret, 32, 0).
//  - GCM: the nonce and the authentication tag must be carried in a
//    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO passed as BCryptEncrypt/
//    BCryptDecrypt's pPaddingInfo (pbIV is NOT used for GCM; a nonce
//    passed only via pbIV yields STATUS_INVALID_PARAMETER). The tag
//    goes to a separate info.pbTag buffer — the function output buffer
//    holds the ciphertext only.
//  - BCRYPT_INIT_AUTH_MODE_INFO(info) takes the struct NAME: the 26100
//    macro wraps its parameter as (&param), so &info would expand to
//    &&info.
//  - BCRYPT_CHAIN_MODE_GCM expands to the wide literal L"ChainingModeGCM"
//    and BCryptSetProperty's value parameter is typed PUCHAR, hence the
//    byte reinterpretation below (size WITHOUT the terminating null, per
//    the MSDN CNG samples).

#if !defined(_WIN32)
#error "media/aead_cng.cpp is a Windows-only translation unit (CNG/BCrypt)"
#endif

// Include order per the app/Win32Env.hpp pattern: pin the Windows 10 API
// surface and keep out of min/max + media headers before <windows.h>;
// <windows.h> before <bcrypt.h>.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

#include <windows.h>
#include <bcrypt.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "media/aead_cng.hpp"
#include "media/media_framing.hpp"

namespace remotewindows::media {

namespace {

// BCryptSetProperty takes the property value as PUCHAR even when the
// property is a wide string: reinterpret the L"..." literal bytes.
inline PUCHAR propertyBytes(const wchar_t* w) {
  return reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(w));
}

// Opens the AES algorithm provider configured for GCM chaining mode.
// Returns NULL on failure; caller closes the handle.
BCRYPT_ALG_HANDLE openAesGcmAlgorithm() {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
    return nullptr;
  const NTSTATUS st = BCryptSetProperty(
      alg, BCRYPT_CHAINING_MODE, propertyBytes(BCRYPT_CHAIN_MODE_GCM),
      // Size WITHOUT the terminating null, exactly as the MSDN CNG
      // samples compute it (wcslen * sizeof(WCHAR)).
      static_cast<ULONG>(wcslen(BCRYPT_CHAIN_MODE_GCM) * sizeof(wchar_t)),
      0);
  if (st != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return nullptr;
  }
  return alg;
}

// Loads the 32-byte AES-256 key into a software key object.
// Returns NULL on failure; caller destroys the key.
BCRYPT_KEY_HANDLE loadKey(BCRYPT_ALG_HANDLE alg, const uint8_t key[32]) {
  BCRYPT_KEY_HANDLE hkey = nullptr;
  const NTSTATUS st = BCryptGenerateSymmetricKey(
      alg, &hkey, nullptr, 0,
      reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(key)), 32, 0);
  if (st != 0) {
    return nullptr;
  }
  return hkey;
}

}  // namespace

bool AeadCng::seal(const uint8_t key[32], const uint8_t nonce[12],
                   const uint8_t* in, size_t in_len, uint8_t* out_ct_tag,
                   size_t& out_len) const {
  if (out_ct_tag == nullptr || (in_len > 0 && in == nullptr)) return false;
  const size_t out_capacity = in_len + kMediaGcmTagBytes;

  const BCRYPT_ALG_HANDLE alg = openAesGcmAlgorithm();
  if (alg == nullptr) return false;

  bool ok = false;
  const BCRYPT_KEY_HANDLE hkey = loadKey(alg, key);
  if (hkey != nullptr) {
    // GCM flow (CNG docs, "Authenticated cipher mode info"): the nonce
    // MUST be carried in BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO (a nonce
    // passed only via pbIV makes BCryptEncrypt return
    // STATUS_INVALID_PARAMETER), and the authentication tag is written
    // to a SEPARATE buffer pointed to by info.pbTag — the function
    // output buffer receives only the ciphertext.
    //
    // Wire layout is ciphertext||tag: hand BCryptEncrypt the first
    // in_len bytes of out_ct_tag as the ciphertext output and the last
    // 16 bytes as the tag buffer, so the wire format lands in place.
    // No AAD (pbAuthData=NULL), no chaining (dwFlags=0, pbMacContext
    // =NULL — both zeroed by BCRYPT_INIT_AUTH_MODE_INFO).
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    // NOTE: the 26100 macro wraps its param as (&param), so pass the
    // struct NAME (not &info) — &info would expand to &&info.
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<uint8_t*>(nonce);
    info.cbNonce = static_cast<ULONG>(kMediaNonceBytes);
    info.pbTag = out_ct_tag + in_len;
    info.cbTag = static_cast<ULONG>(kMediaGcmTagBytes);

    ULONG cb_result = 0;
    const NTSTATUS st = BCryptEncrypt(
        hkey, reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(in)),
        static_cast<ULONG>(in_len), &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(out_ct_tag),
        static_cast<ULONG>(in_len), &cb_result, 0);
    BCryptDestroyKey(hkey);
    if (st == 0 && cb_result == static_cast<ULONG>(in_len)) {
      out_len = out_capacity;  // ciphertext + tag, both in place
      ok = true;
    }
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

bool AeadCng::open(const uint8_t key[32], const uint8_t nonce[12],
                   const uint8_t* in, size_t in_len, uint8_t* out_pt,
                   size_t& out_len) const {
  if (in == nullptr || out_pt == nullptr) return false;
  if (in_len < kMediaGcmTagBytes) return false;
  const size_t pt_len = in_len - kMediaGcmTagBytes;

  const BCRYPT_ALG_HANDLE alg = openAesGcmAlgorithm();
  if (alg == nullptr) return false;

  bool ok = false;
  const BCRYPT_KEY_HANDLE hkey = loadKey(alg, key);
  if (hkey != nullptr) {
    // GCM flow: the tag to be checked comes from info.pbTag (last 16
    // bytes of the input); BCryptDecrypt's input buffer is the
    // ciphertext part only. Decrypt into a local buffer first so the
    // caller's out buffer is written only on full success (never
    // partially written on an authentication failure).
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    // NOTE: the 26100 macro wraps its param as (&param), so pass the
    // struct NAME (not &info) — &info would expand to &&info.
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = const_cast<uint8_t*>(nonce);
    info.cbNonce = static_cast<ULONG>(kMediaNonceBytes);
    info.pbTag = const_cast<uint8_t*>(in) + pt_len;
    info.cbTag = static_cast<ULONG>(kMediaGcmTagBytes);

    std::vector<uint8_t> scratch(pt_len);
    ULONG cb_result = 0;
    const NTSTATUS st = BCryptDecrypt(
        hkey, reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(in)),
        static_cast<ULONG>(pt_len), &info, nullptr, 0,
        reinterpret_cast<PUCHAR>(scratch.data()),
        static_cast<ULONG>(pt_len), &cb_result, 0);
    BCryptDestroyKey(hkey);
    if (st == 0 && cb_result == static_cast<ULONG>(pt_len)) {
      for (size_t i = 0; i < pt_len; ++i) out_pt[i] = scratch[i];
      out_len = pt_len;
      ok = true;
    }
  }
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

}  // namespace remotewindows::media
