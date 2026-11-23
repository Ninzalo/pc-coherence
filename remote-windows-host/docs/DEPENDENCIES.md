# Windows Host — Dependencies

Pinned. No runtime downloads; all dependencies are vendored or system.

| Dependency | Version/commit | License | Purpose | Build |
|---|---|---|---|---|
| googletest | v1.15.2 (tag) | BSD-3 | unit tests | vendored in `vendor/googletest`, built by CMake |
| nlohmann/json | v3.11.3 (tag) | MIT | JSON codec for the v0.x wire format + test vectors | vendored header-only in `vendor/nlohmann_json/include` |
| CMake | ≥ 3.20 | BSD-3 | build | system (3.31.6 verified on the Mac mini dev host) |
| Ninja | any recent | MIT | generator | system (1.13.2 verified on the Mac mini dev host) |
| MSVC + Windows SDK 11 | VS 2022 17.x, SDK 10.0.22621.x (pin exact at runner setup) | MS license | Win32/WGC build | Windows runner only |
| WebRTC (native) | **NOT YET PINNED** — see ADR-0007 | BSD-style | media transport for video | not integrated in v0.1.0; integration requires a pinned upstream revision + reproducible build instructions recorded here before use |

## WebRTC build note (plan §3.1)

When the WebRTC integration starts: select an official upstream revision,
pin commit + hash, record license + GN/Ninja build instructions, verify
hardware H.264 encoding on the target GPU with profiling, and update this
file. No binary package URLs are assumed.

## Offline transfer

Vendor trees are committed, so an offline Windows machine can build from a
git clone/bundle without Internet.
