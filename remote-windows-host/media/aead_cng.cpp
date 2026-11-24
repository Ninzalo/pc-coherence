
// Windows CNG (BCrypt*) AES-256-GCM backend for the media channel
// (PROTOCOL.md §9.4, ADR-0008). WIN32-only translation unit: compiled
// only on Windows (see CMakeLists.txt), unit-tested only on the Windows
// runner — NOT RUN on macOS.
//
// SDK note (Windows SDK 10.0.26100.0, verified against the installed
// shared/bcrypt.h on the runner host):
//  - BCryptCreateKey is NOT declared in this SDK's bcrypt.h; key
//    material is loaded with
//    BCryptGenerateSymmetricKey(alg, &key, NULL, 0, secret, 32, 0).
//  - BCRYPT_CHAIN_MODE_GCM expands to the wide literal L"ChainingModeGCM"
//    and BCryptSetProperty's value parameter is typed PUCHAR, hence the
//    byte reinterpretation below (size includes the terminating null).

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
#include <cstdio>
#include <vector>

#include "media/aead_cng.hpp"
#include "media/media_framing.hpp"

namespace remotewindows::media {

namespace {

// TEMP DIAGNOSTIC (runner failure triage) — removed after triage.
void diag(const char* what, NTSTATUS st) {
  FILE* f = std::fopen("C:\\rwh\\aead_diag.txt", "a");
  if (f == nullptr) return;
  std::fprintf(f, "%s=%08lX\n", what, static_cast<unsigned long>(st));
  std::fclose(f);
}

// BCryptSetProperty takes the property value as PUCHAR even when the
// property is a wide string: reinterpret the L"..." literal bytes,
// keeping the terminating null (size passed by the caller).
inline PUCHAR propertyBytes(const wchar_t* w) {
  return reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(w));
}

// Opens the AES algorithm provider configured for GCM chaining mode.
// Returns NULL on failure; caller closes the handle.
BCRYPT_ALG_HANDLE openAesGcmAlgorithm() {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0) {
    diag("OpenProvider_fail", 0xFFFFFFFF);
    return nullptr;
  }
  const NTSTATUS st = BCryptSetProperty(
      alg, BCRYPT_CHAINING_MODE, propertyBytes(BCRYPT_CHAIN_MODE_GCM),
      static_cast<ULONG>(sizeof(BCRYPT_CHAIN_MODE_GCM)), 0);
  diag("SetProperty_GCM", st);
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
  diag("GenerateSymmetricKey", st);
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
    // No AAD (pPaddingInfo = NULL); the GCM tag is written at the END of
    // the output buffer by BCryptEncrypt.
    ULONG cb_result = 0;
    const NTSTATUS st = BCryptEncrypt(
        hkey, reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(in)),
        static_cast<ULONG>(in_len), nullptr,
        reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(nonce)),
        static_cast<ULONG>(kMediaNonceBytes),
        reinterpret_cast<PUCHAR>(out_ct_tag),
        static_cast<ULONG>(out_capacity), &cb_result, 0);
    diag("Encrypt", st);
    BCryptDestroyKey(hkey);
    if (st == 0 && cb_result == static_cast<ULONG>(out_capacity)) {
      out_len = cb_result;
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
    // Decrypt into a local buffer first so the caller's out buffer is
    // written only on full success (never partially written on an
    // authentication failure).
    std::vector<uint8_t> scratch(pt_len);
    ULONG cb_result = 0;
    const NTSTATUS st = BCryptDecrypt(
        hkey, reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(in)),
        static_cast<ULONG>(in_len), nullptr,
        reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(nonce)),
        static_cast<ULONG>(kMediaNonceBytes),
        reinterpret_cast<PUCHAR>(scratch.data()),
        static_cast<ULONG>(pt_len), &cb_result, 0);
    diag("Decrypt", st);
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
