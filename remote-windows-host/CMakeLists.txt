cmake_minimum_required(VERSION 3.20)
project(remote_windows_host CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- pinned vendored dependencies (see docs/DEPENDENCIES.md) -------------
add_subdirectory(vendor/googletest EXCLUDE_FROM_ALL)
# Make gtest_discover_tests() (GoogleTest.cmake) findable from subprojects.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vendor/googletest)
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/vendor/nlohmann_json/include)

# --- portable core (builds on macOS/Linux and Windows) -------------------
add_library(rwh_core STATIC
  core/sha256.cpp
  core/protocol_version.cpp
  core/protocol_types.cpp
  core/protocol_json.cpp
  core/protocol_validation.cpp
  core/fps_policy.cpp
  core/catalog.cpp
  core/catalog_service.cpp
  core/window_relationships.cpp
  core/window_registry.cpp
  core/input.cpp
  core/pairing.cpp
  core/power_policy.cpp
  core/authorization.cpp
  core/capture_gate.cpp
  core/capture_session_manager.cpp)
target_include_directories(rwh_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rwh_core PUBLIC nlohmann_json)

# --- portable LAN signaling (TCP framing + server/client) ----------------
add_library(rwh_network STATIC
  network/tcp_socket.cpp
  network/framing.cpp
  network/signaling_server.cpp
  network/signaling_client.cpp)
target_link_libraries(rwh_network PUBLIC rwh_core)

# --- portable media core (RW-H-007, PROTOCOL §9) --------------------------
# Binary framing, per-window stream keys (§9.3), stream registry (§9.5),
# HMAC-SHA256 (§9.2 handshake). Builds and unit-tests on any host.
add_library(rwh_media STATIC
  media/media_framing.cpp
  media/stream_keys.cpp
  media/stream_registry.cpp
  media/hmac_sha256.cpp)
target_link_libraries(rwh_media PUBLIC rwh_core)
if(WIN32)
  # Windows CNG AES-256-GCM backend (WIN32-only translation unit; the
  # class is declared in media/aead_cng.hpp under #ifdef _WIN32).
  target_sources(rwh_media PRIVATE media/aead_cng.cpp)
  target_link_libraries(rwh_media PRIVATE bcrypt)
endif()

# --- portable probe binary (dev/test helper; also used to prove a remote
#     build/test runner end-to-end: serve on Windows, connect from macOS)
add_executable(rwh-signaling-probe app/signaling_probe.cpp)
target_link_libraries(rwh-signaling-probe PRIVATE rwh_network)

# --- Windows-only targets --------------------------------------------------
# Win32 adapters (capture, windows, input, power, catalog) compile only with
# MSVC + Windows SDK. They are intentionally absent from the portable build;
# see docs/STATUS.md (NOT RUN on macOS).

# Win32 catalog adapters (RW-H-009): Start Menu IShellLinkW scan,
# allow-listed ShellExecuteExW launcher, OpenProcess liveness probe.
if(WIN32)
  add_library(rwh_win32_catalog STATIC
    win32/catalog/StartMenuSource.cpp
    win32/catalog/Win32Launcher.cpp
    win32/catalog/Win32ProcessProbe.cpp)
  target_link_libraries(rwh_win32_catalog PUBLIC rwh_core ole32 shell32)
endif()

# Win32 window source (RW-H-010 Stage 2, H-WINDOWS): SetWinEventHook hook
# thread + periodic EnumWindows reconciliation scan feeding the portable
# WindowRegistry (core/window_registry.hpp). Windows-only: there is no
# macOS port of this target (NOT RUN on macOS by design).
if(WIN32)
  add_library(rwh_win32_windows STATIC win32/windows/Win32WindowSource.cpp)
  target_link_libraries(rwh_win32_windows PUBLIC rwh_core user32)
endif()

# Win32 WGC capture adapter (RW-H-011 Stage 2, H-MEDIA): the
# Windows.Graphics.Capture implementation of the portable
# core::ICaptureSurfaceSource seam (per-window GraphicsCaptureItem +
# free-threaded D3D11 frame pool + per-surface worker thread, consent
# gate via GraphicsCaptureAccess::RequestAccessAsync). Windows-only:
# there is no macOS port of this target (NOT RUN on macOS by design).
if(WIN32)
  add_library(rwh_win32_capture STATIC win32/capture/WgcCaptureSource.cpp)
  target_link_libraries(rwh_win32_capture
                        PUBLIC rwh_core d3d11 dwmapi windowsapp)
endif()

# Windows Host application shell (H-FOUND, plan section 4.1). Console
# subsystem (selftest output must be capturable by the build/test runner);
# Win32 + .rc resources, MSVC only.
if(WIN32)
  add_executable(rwh-host
    app/main.cpp
    app/AppController.cpp
    app/SessionAuth.cpp
    app/TrayController.cpp
    app/Logger.cpp
    app/resources/en-US.rc)
  target_link_libraries(rwh-host PRIVATE
    rwh_network rwh_win32_catalog shell32 user32)
endif()

enable_testing()
add_subdirectory(tests)
