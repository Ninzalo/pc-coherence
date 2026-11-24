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
  window_relationships_test
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
