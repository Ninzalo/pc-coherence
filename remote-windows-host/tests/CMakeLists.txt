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
  signaling_e2e_test)

foreach(t ${RWH_TESTS})
  add_executable(${t} ${t}.cpp)
  target_link_libraries(${t} PRIVATE
    rwh_network
    GTest::gtest
    GTest::gtest_main
    Threads::Threads)
  target_compile_definitions(${t} PRIVATE
    RWH_TEST_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../protocol/test-vectors")
  gtest_discover_tests(${t} DISCOVERY_TIMEOUT 30)
endforeach()
