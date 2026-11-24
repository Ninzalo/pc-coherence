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
