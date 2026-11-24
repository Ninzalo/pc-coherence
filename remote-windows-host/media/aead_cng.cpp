
// Windows CNG (BCrypt*) AES-256-GCM backend for the media channel
// (PROTOCOL.md §9.4, ADR-0008). WIN32-only translation unit: compiled
// only on Windows (see CMakeLists.txt), unit-tested only on the Windows
// runner — NOT RUN on macOS.

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

// Opens the AES algorithm provider configured for GCM chaining mode.
// Returns NULL on failure; caller closes the handle.
BCRYPT_ALG_HANDLE openAesGcmAlgorithm() {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
    return nullptr;
  // BCRYPT_CHAIN_MODE_GCM is the wide string "ChainingModeGCM"; the size
  // must include the terminating null (per CNG documentation).
  const NTSTATUS st = BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                                        BCRYPT_CHAIN_MODE_GCM,
                                        sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
  if (st != 0) {
    BCryptCloseAlgorithmProvider(alg, 0);
    return nullptr;
  }
  return alg;
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
  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptCreateKey(alg, &hkey, nullptr, 0,
                      reinterpret_cast<PUCHAR>(const_cast<const uint8_t*>(key)),
                      32, 0) == 0 && hkey != nullptr) {
    // No AAD (pAuthInfo = NULL, pbAuthData = NULL/0); GCM tag is written
    // at the END of the output buffer by BCryptEncrypt.
    ULONG cb_result = 0;
    const NTSTATUS st = BCryptEncrypt(
        hkey, reinterpret_cast<PUCHAR>(const_cast<const uint8_t*>(in)),
        static_cast<ULONG>(in_len), nullptr,
        reinterpret_cast<PUCHAR>(const_cast<const uint8_t*>(nonce)), 12,
        nullptr, 0,
        reinterpret_cast<PUCHAR>(out_ct_tag),
        static_cast<ULONG>(out_capacity), &cb_result, 0);
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
  BCRYPT_KEY_HANDLE hkey = nullptr;
  if (BCryptCreateKey(alg, &hkey, nullptr, 0,
                      reinterpret_cast<PUCHAR>(const_cast<const uint8_t*>(key)),
                      32, 0) == 0 && hkey != nullptr) {
    // Decrypt into a local buffer first so the caller's out buffer is
    // written only on full success (never partially written on an
    // authentication failure).
    std::vector<uint8_t> scratch(pt_len);
    ULONG cb_result = 0;
    const NTSTATUS st = BCryptDecrypt(
        hkey, reinterpret_cast<PUCHAR>(const_cast<const uint8_t*>(in)),
        static_cast<ULONG>(in_len), nullptr,
        reinterpret_cast<PUCHAR>(const_cast<const uint8_t*>(nonce)), 12,
        nullptr, 0, reinterpret_cast<PUCHAR>(scratch.data()),
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
