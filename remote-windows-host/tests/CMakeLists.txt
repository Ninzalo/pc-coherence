# Unit tests — portable core + network. Run: ctest --output-on-failure.
find_package(Threads REQUIRED)
include(GoogleTest)

set(RWH_TESTS
  sha256_test
  protocol_version_test
  protocol_json_test
  validation_test
  fps_policy_test
  catalog_test
  catalog_service_test
  window_relationships_test
  window_registry_test
  capture_pipeline_test
  input_test
  pairing_test
  power_policy_test
  authorization_test
  framing_test
  signaling_e2e_test
  session_auth_test)

# session_auth_test additionally compiles the portable app-layer session
# state machine + logger (RW-H-004) so the full handler is testable on
# any host. Other tests leave ${<t>_sources} empty.
set(session_auth_test_sources ../app/SessionAuth.cpp ../app/Logger.cpp)

foreach(t ${RWH_TESTS})
  add_executable(${t} ${t}.cpp ${${t}_sources})
  target_link_libraries(${t} PRIVATE
    rwh_network
    GTest::gtest
    GTest::gtest_main
    Threads::Threads)
  target_compile_definitions(${t} PRIVATE
    RWH_TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../protocol/test-vectors")
  gtest_discover_tests(${t} DISCOVERY_TIMEOUT 30)
endforeach()

# --- portable media core tests (RW-H-007, PROTOCOL §9) --------------------
set(RWH_MEDIA_TESTS
  media_framing_test
  media_stream_keys_test
  media_stream_registry_test
  hmac_sha256_test)
foreach(t ${RWH_MEDIA_TESTS})
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE
    rwh_media
    GTest::gtest
    GTest::gtest_main
    Threads::Threads)
  target_compile_definitions(${t} PRIVATE
    RWH_TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../protocol/test-vectors")
  gtest_discover_tests(${t} DISCOVERY_TIMEOUT 30)
endforeach()

# Windows CNG AES-256-GCM backend test (RW-H-007). Created ONLY on Windows:
# media/aead_cng.cpp (and this test) must NOT compile on macOS.
if(WIN32)
  add_executable(media_aead_cng_test media_aead_cng_test.cpp)
  target_link_libraries(media_aead_cng_test PRIVATE
    rwh_media
    bcrypt
    GTest::gtest
    GTest::gtest_main
    Threads::Threads)
  target_compile_definitions(media_aead_cng_test PRIVATE
    RWH_TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../protocol/test-vectors")
  gtest_discover_tests(media_aead_cng_test DISCOVERY_TIMEOUT 30)
endif()

# Win32 catalog adapters tests (RW-H-009). Created ONLY on Windows:
# win32/catalog/*.cpp (and these tests) must NOT compile on macOS.
if(WIN32)
  foreach(t startmenu_source_test win32_launcher_test)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE
      rwh_win32_catalog
      GTest::gtest
      GTest::gtest_main
      Threads::Threads)
    gtest_discover_tests(${t} DISCOVERY_TIMEOUT 30)
  endforeach()
endif()

# Win32 window-source integration test (RW-H-010 Stage 2, H-WINDOWS).
# Created ONLY on Windows: win32/windows/*.cpp (and this test) must NOT
# compile on macOS. Hermetic: the process creates its own top-level +
# owned dialog windows and cleans them up (see the file header). The test
# provides its own main() (no gtest_main).
if(WIN32)
  add_executable(win32_windows_registry_test win32_windows_registry_test.cpp)
  target_link_libraries(win32_windows_registry_test PRIVATE
    rwh_win32_windows
    GTest::gtest
    Threads::Threads
    user32)
  gtest_discover_tests(win32_windows_registry_test DISCOVERY_TIMEOUT 30)
endif()

# Win32 WGC capture integration test (RW-H-011 Stage 2, H-MEDIA).
# Created ONLY on Windows: win32/capture/*.cpp (and this test) must NOT
# compile on macOS. Hermetic + consent-gated: the process creates its
# OWN top-level window and drives the real WGC adapter through the
# portable manager; if consent is not granted (a USER ACTION), the typed
# ConsentRequired path is asserted and the live-capture assertions
# GTEST_SKIP; all waits are bounded (the test can never hang on UI).
if(WIN32)
  add_executable(win32_wgc_capture_test win32_wgc_capture_test.cpp)
  target_link_libraries(win32_wgc_capture_test PRIVATE
    rwh_win32_capture
    rwh_win32_windows
    GTest::gtest
    Threads::Threads
    gdi32
    user32)
  gtest_discover_tests(win32_wgc_capture_test DISCOVERY_TIMEOUT 30)
endif()
